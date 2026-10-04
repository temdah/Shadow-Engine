"""Run bounded copies of regional compiler/verifier/VM instructions in this
test process. Never load a game DLL or attach to a game. Compiler error calls
alone are replaced with rejection markers; successful paths remain native.
This does not establish startup exclusion or full Lua/GC lifecycle safety.
"""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from lua_frame_corpus import REFERENCE, parse_sites, verify_lua_frame
from validate_runtime_corpus import Image

PROJECT=Path(__file__).resolve().parents[1]

class Executable:
    def __init__(self,code,result,*args):
        self.k=c.WinDLL('kernel32',use_last_error=True)
        self.k.SetErrorMode(3)
        self.k.VirtualAlloc.argtypes=[c.c_void_p,c.c_size_t,c.c_uint32,c.c_uint32]
        self.k.VirtualAlloc.restype=c.c_void_p
        self.k.VirtualProtect.argtypes=[c.c_void_p,c.c_size_t,c.c_uint32,c.POINTER(c.c_uint32)]
        self.k.VirtualFree.argtypes=[c.c_void_p,c.c_size_t,c.c_uint32]
        self.k.FlushInstructionCache.argtypes=[c.c_void_p,c.c_void_p,c.c_size_t]
        self.address=self.k.VirtualAlloc(None,len(code),0x3000,4)
        if not self.address:raise OSError(c.get_last_error())
        c.memmove(self.address,code,len(code));old=c.c_uint32()
        if not self.k.VirtualProtect(self.address,len(code),0x20,c.byref(old)):
            self.close();raise OSError(c.get_last_error())
        if not self.k.FlushInstructionCache(c.c_void_p(-1),self.address,len(code)):
            self.close();raise OSError(c.get_last_error())
        self.call=c.WINFUNCTYPE(result,*args)(self.address)
    def close(self):
        if self.address:self.k.VirtualFree(self.address,0,0x8000);self.address=None

def run(inputs,name):
    spec=json.loads(inputs.read_text())['profiles'][name]
    image=Image(Path(spec['path']),spec['mapped'])
    assert hashlib.sha256(image.data).hexdigest()==spec['sha256']
    source=(PROJECT/'src/internal/lua_frame_sites.inc').read_text()
    profiles=(PROJECT/'src/modules/05_runtime_profiles.inc').read_text()
    verify_lua_frame(image,profiles,source,name)
    reference=json.loads(REFERENCE.read_text())['profiles'][name]
    sites=parse_sites(source,reference['table'])
    def code(start,size,repaired):
        result=bytearray(image.read(start,size))
        if repaired:
            for site in sites.values():
                offset=site['rva']-start
                if 0<=offset<size:
                    assert offset+site['size']<=size
                    result[offset:offset+site['size']]=site['after']
        return bytes(result)
    checks=0;observations={}
    for reserve in (False,True):
        for repaired in (False,True):
            role='compiler_reserve_read' if reserve else 'compiler_check_read'
            start=sites[role]['rva']-(0x1d if reserve else 0x16)
            body=bytearray(code(start,0x7c if reserve else 0x55,repaired))
            error,epilogue=(0x33,0x59) if reserve else (0x29,0x4a)
            body[error:error+16]=bytes.fromhex('c7414401000000e9')+struct.pack('<i',epilogue-error-12)+b'\x90'*4
            fn=Executable(bytes(body),None,c.c_void_p,c.c_int)
            try:
                for count in range(2,252):
                    for flags in range(8):
                        proto=(c.c_ubyte*96)();state=(c.c_ubyte*80)()
                        proto[11]=(2<<3)|flags;proto[12]=2
                        struct.pack_into('<Q',state,0,c.addressof(proto))
                        fn.call(c.addressof(state),count)
                        rejected=struct.unpack_from('<I',state,0x44)[0]
                        wanted=2 if count>=250 else count
                        actual=proto[12] if repaired else proto[11]>>3
                        assert rejected==int(count>=250)
                        assert actual==(wanted if repaired else wanted%32)
                        assert proto[11]&7==flags
                        assert struct.unpack_from('<I',state,0x3c)[0]==(count if reserve and not rejected else 0)
                        checks+=4
                        if not reserve and flags==0 and count in (31,32,33,51,249,250):
                            observations[f'{"repaired" if repaired else "native"}_{count}']=actual
            finally:fn.close()
    precheck=Executable(code(sites['precheck_count_ceiling']['rva']-8,0x80,True),c.c_int,c.c_void_p)
    operand=Executable(code(sites['operand_rk_bound']['rva']-0x20,0x4d,True),c.c_int,c.c_void_p,c.c_int,c.c_int)
    top=Executable(bytes.fromhex('56488bf1')+code(sites['precall_top_read']['rva'],14,True)+bytes.fromhex('5ec3'),c.c_uint64,c.c_void_p)
    try:
        terminal=c.c_uint32(30)
        for count in range(2,256):
            for flags in range(8):
                proto=(c.c_ubyte*96)();proto[11]=flags;proto[12]=count;proto[10]=3<<4
                struct.pack_into('<Q',proto,24,c.addressof(terminal))
                struct.pack_into('<H',proto,76,1);struct.pack_into('<H',proto,74,2)
                expected=count<=250 and 3+(flags&1)<=count and not (flags&4 and not flags&1)
                assert precheck.call(c.addressof(proto))==int(expected)
                assert top.call(c.addressof(proto))==count*16
                for reg in (count-1,count):
                    assert operand.call(c.addressof(proto),reg,2)==int(reg<count)
                    assert operand.call(c.addressof(proto),reg,3)==int(reg<count)
                assert operand.call(c.addressof(proto),0x101,3)==1
                assert operand.call(c.addressof(proto),0x102,3)==0
                checks+=8
    finally:
        precheck.close();operand.close();top.close()
    return dict(profile=name,checks=checks,observations=observations,
                scope='Actual isolated regional compiler/verifier/VM-top instructions; not full runtime acceptance.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs',type=Path,required=True)
    parser.add_argument('--profile')
    args=parser.parse_args()
    if args.profile:print(json.dumps(run(args.inputs,args.profile)))
    else:
        for name in json.loads(REFERENCE.read_text())['profiles']:
            subprocess.run([sys.executable,'-B',__file__,'--inputs',str(args.inputs),'--profile',name],check=True,timeout=60)
