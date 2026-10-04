"""Offline regional byte/feature gates. Never load or execute a game image."""
from pathlib import Path
import hashlib
import json
import re
import struct
import sys

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tests'))
import validate_runtime_corpus as corpus
import validate_vehicle_resolution as quality
import validate_vehicle_ownership as owners
import validate_vehicle_batch as batches
import validate_vehicle_retirement as retirement
import validate_vehicle_handle_link as handles
from runtime_profile_fields import runtime_profile_field
from test_driver_identity_profile import load_profile, proof_matches
from lua_frame_corpus import verify_lua_frame

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def normalized(text):
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    return re.sub(r'\s+', '', text)

def hook_definitions(text):
    """Retain native call ordering/ABI in hook bodies, not just signatures."""
    clean=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
    definitions=[]
    for match in re.finditer(r'\bstatic\s+[^;{}]*\bhooked_\w+\s*\([^;{}]*\)\s*\{',clean):
        depth=1; end=match.end()
        # Skip strings while balancing C blocks (diagnostic strings can contain braces).
        for token in re.finditer(r'"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27|[{}]',clean[end:]):
            if token.group()=='{':depth+=1
            elif token.group()=='}':depth-=1
            if depth==0:
                definitions.append(clean[match.start():end+token.end()]);break
        else: raise ValueError('Unterminated hook definition')
    return definitions

def contracts():
    """Conservative native-boundary fingerprint; policy bodies are regression-tested."""
    paths = ['src/modules/'+name for name in (
        '05_runtime_profiles.inc', '10_runtime_primitives.inc', '15_patch_transaction.inc',
        '60_engine_expansion.inc', '65_runtime_preflight.inc', '70_bootstrap_orchestration.inc',
        '80_runtime_entry.inc')]
    paths += ['src/internal/driver_identity_profile.inc', 'src/internal/lua_frame_sites.inc',
              'src/internal/population_capture_profile.inc', 'src/shadow_engine_patch.def']
    items = {p: normalized((PROJECT/p).read_text(encoding='utf-8')) for p in paths}
    shared = (PROJECT/'src/modules/00_shared_config_state.inc').read_text(encoding='utf-8')
    # Version-only releases do not invalidate the native compatibility baseline.
    items['shared-layout'] = normalized(re.sub(r'^#define PATCH_VERSION .*$', '', shared, flags=re.M))
    hook_signatures = []
    for p in (PROJECT/'src/modules').glob('*.inc'):
        text = p.read_text(encoding='utf-8')
        hook_signatures += hook_definitions(text)
    items['native-hook-definitions'] = normalized('\n'.join(sorted(hook_signatures)))
    # A weakened gate must not inherit approval from the gate it replaced.
    for p in sorted((PROJECT/'tools/compatibility').glob('*.py')):
        items[p.relative_to(PROJECT).as_posix()] = p.read_text(encoding='utf-8')
    for p in sorted((PROJECT/'tests').rglob('*')):
        if p.suffix in ('.py', '.ps1', '.c', '.h', '.lua', '.inc', '.def', '.json'):
            items[p.relative_to(PROJECT).as_posix()] = p.read_text(encoding='utf-8')
    for name in ('build.ps1', 'build-compatible.ps1', '.github/workflows/build-mod.yml'):
        items[name] = (PROJECT/name).read_text(encoding='utf-8')
    return {key: hashlib.sha256(value.encode()).hexdigest() for key,value in items.items()}

def required_steps():
    return {'compiler-version', 'build-Internal', 'build-Release', 'exports-Internal',
            'exports-Release', 'source-contract', 'python-regressions',
            'population-decoder-integration', 'lua-menu', 'lua-register-budget', 'lua-native-semantics', 'input-integrity'} | {
                p.stem for p in (PROJECT/'tests').glob('test_*.ps1')}

def report_failures(profiles, steps):
    failures = []
    if set(profiles) != {p.name for p in corpus.PROFILES.values()}:
        failures.append('regional matrix must contain exactly five profiles')
    names = [row['name'] for row in steps]
    absent = required_steps() - set(names)
    if absent: failures.append('missing gates: '+', '.join(sorted(absent)))
    if len(names) != len(set(names)): failures.append('duplicate gate results')
    failures += [name for name,row in profiles.items() if row['status'] not in ('pass','missing-evidence')]
    failures += [row['name'] for row in steps if row['exit_code'] != 0]
    return failures

def verify_optional_bytes(image, source, name, features):
    checks = {}
    if features['player_relative_selection']:
        def rva(key): return int(runtime_profile_field(source,name,key).removesuffix('ULL').removesuffix('U'),16)
        binding=rva('local_player_binding_rva'); reader=rva('entity_position_reader_rva')
        code=image.read(binding,35)
        tail=bytes.fromhex('837808007418488b00488b004885c0740d488b40084c8b48104d8b09')
        if code[:3]!=bytes.fromhex('488b05') or code[7:]!=tail:
            raise ValueError('local-player binding proof mismatch')
        if image.read(reader,17)!=bytes.fromhex('8b415089028b41548942048b4158894208'):
            raise ValueError('entity-position reader proof mismatch')
        root=binding+7+struct.unpack('<i',code[3:7])[0]
        image.read(root,8)
        checks['player_anchor']={'binding_bytes':35,'reader_bytes':17,'root_rva':root}
    if features['lua_frame_repair_internal_only']:
        text=(PROJECT/'src/internal/lua_frame_sites.inc').read_text(encoding='utf-8')
        checks['lua_frame']=verify_lua_frame(image,source,text,name)
    return checks

def capabilities(source, name):
    def field(key): return runtime_profile_field(source, name, key)
    return {
        'vehicle_identity_and_finite_limiter': field('vehicle_light_update_rva') not in ('0','0ULL'),
        'driver_protection': field('driver_identity') != 'NULL',
        'player_relative_selection': field('local_player_binding_rva') not in ('0','0ULL'),
        'lua_frame_repair_internal_only': field('lua_frame_sites') != 'NULL',
        'extended_population_proof_internal_only': field('population_capture') != 'NULL',
    }

def pe_identity(data):
    if data[:2] != b'MZ': raise ValueError('missing DOS header')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe+4] != b'PE\0\0': raise ValueError('missing PE header')
    if struct.unpack_from('<H',data,pe+4)[0] != 0x8664: raise ValueError('not x64')
    opt = pe+24
    if struct.unpack_from('<H',data,opt)[0] != 0x20b: raise ValueError('not PE32+')
    return (struct.unpack_from('<I',data,opt+56)[0], struct.unpack_from('<I',data,pe+8)[0],
            struct.unpack_from('<I',data,opt+64)[0], struct.unpack_from('<I',data,opt+16)[0])

def checked_image(spec, profile, mapped):
    path=Path(spec['path'])
    if not path.is_file(): raise FileNotFoundError(f'missing image: {path.name}')
    actual=digest(path)
    if actual != spec['sha256'].lower(): raise ValueError(f'image hash changed: {path.name}')
    image=corpus.Image(path,mapped)
    expected=(profile.image_size,profile.timestamp,profile.checksum,profile.entry_rva)
    if pe_identity(image.data) != expected: raise ValueError('image belongs to another profile')
    if mapped and len(image.data) < profile.image_size: raise ValueError('truncated mapped image')
    return image,actual

def run_matrix(inputs):
    source=(PROJECT/'src/modules/05_runtime_profiles.inc').read_text(encoding='utf-8')
    shared=(PROJECT/'src/modules/00_shared_config_state.inc').read_text(encoding='utf-8')
    diagnostic=(PROJECT/'src/modules/18_vehicle_light_diagnostics.inc').read_text(encoding='utf-8')
    lifetime=(PROJECT/'src/modules/18_vehicle_owner_lifetime.inc').read_text(encoding='utf-8')
    for validator,body in ((owners,diagnostic),(batches,diagnostic),(retirement,lifetime),(handles,diagnostic)):
        validator.verify_source(body,source)
    quality.verify_source(shared)
    asia=corpus.parse_asia_map(source)
    tails=corpus.parse_tail_patches(shared,corpus.integer_define(shared,'EXTRA_LOCAL_MAPS'))
    manifest=corpus.manifest_records(Path(inputs['corpus_manifest']))
    expected_names={p.name for p in corpus.PROFILES.values()}
    if set(inputs['profiles']) != expected_names: raise ValueError('input manifest must name exactly the five known profiles')
    results={}
    for p in corpus.PROFILES.values():
        result={'status':'failed','features':capabilities(source,p.name),'checks':{}}
        results[p.name]=result
        try:
            raw_path=Path(manifest[p.sha256]['absoluteResearchPath'])
            raw,raw_hash=checked_image({'path':str(raw_path),'sha256':p.sha256},p,False)
            result['raw_sha256']=raw_hash
            spec=inputs['profiles'][p.name]
            if spec is None:
                result.update(status='missing-evidence',reason='Unpacked runtime image unavailable; an old successful log is not a current byte proof.')
                continue
            image,hash_value=checked_image(spec,p,spec['mapped'])
            result['image_sha256']=hash_value
            result['checks']['core']=corpus.validate_exact(p,image,asia,tails,True)
            result['checks']['quality_consumers']=quality.verify_image(image,corpus.resolve(p,0x306e60,asia))
            for key,validator,table in (('vehicle_owner',owners,owners.UPDATES),('vehicle_batch',batches,batches.BATCHES),
                                        ('vehicle_retirement',retirement,retirement.RETIREMENTS),('vehicle_handle',handles,handles.COPIES)):
                rva=table[p.name]
                result['checks'][key]=validator.verify_image(image,rva) if rva else {'status':'disabled-unmapped'}
            if result['features']['driver_protection']:
                # Existing independent Global proof fixture fixes both addresses and bytes.
                if p.name!='supported-04DF': raise ValueError('new driver profile requires an independent regional validator')
                _,spans,values=load_profile()
                if not proof_matches(image.read,spans,values): raise ValueError('driver proof/class metadata mismatch')
                result['checks']['driver']={'spans':len(spans),'bytes':sum(len(b) for _,b in spans),'pass':True}
            result['checks'].update(verify_optional_bytes(image,source,p.name,result['features']))
            result['status']='pass'
        except (OSError,ValueError,KeyError,AssertionError,struct.error) as error:
            result['reason']=str(error) or type(error).__name__
    return results

def evaluate(profiles, steps, current_contract, compiler_hash, baseline):
    failures=report_failures(profiles,steps)
    blockers=[]
    if failures: blockers.append('Failed gates: '+', '.join(failures))
    missing=[name for name,row in profiles.items() if row['status']!='pass']
    if missing: blockers.append('Incomplete regional evidence: '+', '.join(missing))
    if baseline is None:
        blockers.append('Initial manual calibration has not been recorded.')
    else:
        observations=baseline.get('manual_observations',[])
        if baseline.get('schema')!=1 or not observations or any(
                o.get('profile') not in profiles or o.get('build_profile') not in ('Internal','Release')
                or not o.get('observation','').strip() or not re.fullmatch('[0-9a-f]{64}',o.get('asi_sha256',''))
                or not re.fullmatch('[0-9a-f]{64}',o.get('report_sha256','')) for o in observations):
            blockers.append('Invalid calibration record.')
        if baseline.get('contracts')!=current_contract:
            blockers.append('Native compatibility boundary changed; review and recalibrate the affected boundary.')
        if baseline.get('compiler_sha256')!=compiler_hash:
            blockers.append('Compiler identity changed; recalibration required.')
        if baseline.get('profile_features')!={k:v['features'] for k,v in profiles.items()}:
            blockers.append('Regional feature coverage changed; review required.')
    return {'status':'failed' if failures else 'incomplete' if blockers else 'pass',
            'automatic_release_gate':not blockers,'blockers':blockers,
            'scope':'Offline compatibility/regression assurance with a recorded manual reference; not per-build visual or FPS proof.'}
