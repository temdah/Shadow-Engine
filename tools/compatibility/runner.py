"""Local reproducible builder and regression orchestrator. No game/remote actions."""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile
from matrix import PROJECT, contracts, digest, evaluate, run_matrix, corpus

def write_json(path, value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')

def prepare(path):
    if path.exists(): raise ValueError('Inputs already exist; preserve and edit the existing local file.')
    workspace=PROJECT.parent
    manifest=workspace/'Support/Inputs/runtime-corpus.json'
    entries=corpus.manifest_records(manifest)
    profiles={}
    for p in corpus.PROFILES.values():
        image=Path(entries[p.sha256]['absoluteResearchPath']); mapped=False
        if p.name=='supported-04DF': image=workspace/'Support/Evidence/RuntimeCaptures/Disrupt_b64_runtime_image.bin';mapped=True
        if p.explicit: image=workspace/'Support/Evidence/RuntimeCaptures/DisruptAsia_E1505F9F_runtime_image_20260824.bin';mapped=True
        profiles[p.name]=None if p.name=='a4ee-signed-variant' else {'path':str(image),'sha256':digest(image),'mapped':mapped}
    lua=PROJECT/'build/tests/lua_menu_runner.exe'
    registers=workspace/'Support/Evidence/Logs/shadow-engine-v2.0.76-quality-settings/lua51-registers.exe'
    write_json(path,{'schema':1,'corpus_manifest':str(manifest),'profiles':profiles,
        'source_reference':str(workspace/'Support/Evidence/RuntimeBackups/ShadowEngineRegional_v1.2.0_preinstall_20260824_014249/SourceSnapshot_final-five-profile-asia-builder-fix'),
        'global_rebuilt_image':str(workspace/'Support/Evidence/RuntimeCaptures/Disrupt_b64_runtime_rebuilt.dll'),
        'nexus_known':str(workspace/'Support/Evidence/RuntimeBackups/DisruptRegional_20260824_010907/TroploNexusTools.ipe'),
        'nexus_updated':str(workspace/'Support/Evidence/ShadowEngine/nexus-1.1.13-20261003/TroploNexusTools.ipe'),
        'lua_runner':{'path':str(lua),'sha256':digest(lua)},
        'lua_register_inspector':{'path':str(registers),'sha256':digest(registers)}})
    print(f'Prepared local inputs: {path}\nA4EE remains null until an unpacked image is supplied. No calibration was created.')

def source_hashes():
    paths=list((PROJECT/'src').rglob('*'))+list((PROJECT/'tests').rglob('*'))+list((PROJECT/'tools').rglob('*.py'))
    paths += [PROJECT/'build.ps1',PROJECT/'build-compatible.ps1',PROJECT/'package-internal-tools.ps1',
              PROJECT/'packaging/ASI_README.md',PROJECT/'packaging/internal-tools/modconfig.json',
              PROJECT/'.github/workflows/build-mod.yml']
    return {p.relative_to(PROJECT).as_posix():digest(p) for p in sorted(set(paths))
            if p.is_file() and p.suffix in ('.c','.h','.inc','.py','.lua','.ps1','.def','.md','.yml','.json')}

def build(args):
    inputs=json.loads(args.inputs.read_text(encoding='utf-8'))
    if inputs.get('schema')!=1: raise ValueError('unsupported input schema')
    output=(args.output or PROJECT/'build/compatibility'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')).resolve()
    if not output.is_relative_to(PROJECT/'build'): raise ValueError('Output must be a new directory under ShadowEngine/build.')
    output.mkdir(parents=True,exist_ok=False)
    report={'schema':1,'created_utc':datetime.now(timezone.utc).isoformat(),'steps':[],
            'contracts':contracts(),'source_sha256':source_hashes(),'compiler_sha256':digest(args.compiler)}
    # An interruption or unexpected exception leaves an explicitly failed record.
    report.update(status='failed',automatic_release_gate=False,blockers=['Pipeline did not complete.'])
    write_json(output/'report.json',report)
    def step(name, command):
        started=time.monotonic()
        log=output/(name+'.log')
        try:
            with log.open('w',encoding='utf-8') as stream:
                completed=subprocess.run([str(x) for x in command],cwd=PROJECT,stdout=stream,stderr=subprocess.STDOUT,
                                         timeout=300,env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'})
            code=completed.returncode
        except (OSError,subprocess.TimeoutExpired) as error:
            with log.open('a',encoding='utf-8') as stream: stream.write(str(error))
            code=1
        report['steps'].append({'name':name,'exit_code':code,'seconds':round(time.monotonic()-started,3),'log':log.name})
        write_json(output/'report.json',report)
        print(f'{"PASS" if code==0 else "FAIL"} {name}',flush=True)
        return code==0
    def ps(script,*arguments):
        return [args.powershell,'-NoProfile','-NonInteractive','-File',PROJECT/script,*arguments]
    py=[sys.executable,'-B']
    pinned_inputs = {
        'global_rebuilt_image':'d8004145fb17bd6a9073466ebb9bfc19589aad7d08d8ec3fd45733a5d8ad0855',
        'nexus_known':'8120389ba4d144dc465e9fed2648b5690a6bbf0cdf5b052ef6ee3b738be5dcfc',
        'nexus_updated':'03875bacfad447397305b9d5f19bcb07dc4473b99f7d1354604b4a2bb1dfe727'}
    errors=[]
    for key,expected in pinned_inputs.items():
        if digest(inputs[key])!=expected:errors.append(key+' identity changed')
    report['steps'].append({'name':'input-integrity','exit_code':int(bool(errors)),'errors':errors})
    write_json(output/'report.json',report)
    if errors:
        print(f'FAILED input integrity: {output / "report.json"}')
        return 1
    step('compiler-version',[args.compiler,'-v'])
    if '0.9.27' not in (output/'compiler-version.log').read_text(encoding='utf-8',errors='replace'):
        raise ValueError('Expected TinyCC 0.9.27')
    for profile in ('Internal','Release'):
        if not step('build-'+profile,ps('build.ps1','-CompilerPath',args.compiler,'-Profile',profile,'-VerifyReproducible')):
            write_json(output/'report.json',{**report,'status':'failed','automatic_release_gate':False})
            return 1
        directory='build' if profile=='Internal' else 'build_release'
        step('exports-'+profile,py+[PROJECT/'tests/validate_release.py','--asi',PROJECT/directory/'ShadowEnginePatch.asi','--profile',profile])
        target=output/profile/'ShadowEnginePatch.asi';target.parent.mkdir()
        shutil.copy2(PROJECT/directory/'ShadowEnginePatch.asi',target)
    step('source-contract',py+[PROJECT/'tests/validate_refactor.py','--baseline',inputs['source_reference'],'--candidate',PROJECT,
         '--policy-target-30-b4-21','--lifecycle-hotfix','--intersection-diagnostic','--vehicle-ownership-diagnostic'])
    step('python-regressions',py+['-m','unittest','discover','-s','tests','-p','test_*.py'])
    # Every existing native gate is included. Real ETW/DXGI and game execution are never requested.
    for script in sorted((PROJECT/'tests').glob('test_*.ps1')):
        arguments=['-CompilerPath',args.compiler]
        if script.stem=='test_nexus_compatibility':arguments += ['-KnownHost',inputs['nexus_known'],'-UpdatedHost',inputs['nexus_updated']]
        if script.stem=='test_population_capture':arguments += ['-RuntimeImage',inputs['global_rebuilt_image'],'-ArtifactDirectory',output/'population-fixtures']
        step(script.stem,ps(script.relative_to(PROJECT),*arguments))
    step('population-decoder-integration',py+[PROJECT/'tests/test_population_capture_decoder.py','--fixtures',output/'population-fixtures'])
    lua=inputs['lua_runner']
    if digest(lua['path'])!=lua['sha256']:raise ValueError(f'Desktop Lua runner hash changed. Failed report: {output / "report.json"}')
    step('lua-menu',[lua['path'],PROJECT/'tests/internal_tools_menu.lua'])
    registers=inputs['lua_register_inspector']
    if digest(registers['path'])!=registers['sha256']:raise ValueError('Lua register inspector identity changed')
    # Stock desktop execution can pass scripts that violate WD1's packed frame.
    # Check every prototype, including module roots, in every shipped Lua file.
    step('lua-register-budget',[registers['path'],*sorted((PROJECT/'src/internal').glob('*.lua'))])
    step('lua-native-semantics',py+[PROJECT/'tests/validate_lua_frame_native.py','--inputs',args.inputs.resolve()])
    try: report['profiles']=run_matrix(inputs)
    except (OSError,ValueError,KeyError,AssertionError) as error:
        report['profiles']={}
        report['steps'].append({'name':'regional-source-contract','exit_code':1,'reason':str(error) or type(error).__name__})
    baseline=json.loads(args.baseline.read_text(encoding='utf-8')) if args.baseline.exists() else None
    if report['source_sha256']!=source_hashes():
        report['steps'].append({'name':'source-changed-during-build','exit_code':1})
    report.update(evaluate(report['profiles'],report['steps'],report['contracts'],report['compiler_sha256'],baseline))
    report['manual_reference']=baseline.get('manual_observations',[]) if baseline else []
    report['artifacts']={}
    version=re.search(r'#define PATCH_VERSION "([^"]+)"',(PROJECT/'src/modules/00_shared_config_state.inc').read_text()).group(1)
    # Frozen candidate artifacts exist even when a missing image blocks release qualification.
    # Failed regression gates never produce an installable ZIP.
    for profile in ('Internal','Release'):
        asi=output/profile/'ShadowEnginePatch.asi'
        report['artifacts'][profile]={'path':str(asi),'sha256':digest(asi)}
        if report['status']=='failed':continue
        label='qualified' if report['automatic_release_gate'] else 'candidate'
        package=output/f'ShadowEngine-v{version}-{profile}-{label}.zip'
        with zipfile.ZipFile(package,'x',zipfile.ZIP_DEFLATED) as z:
            z.write(asi,'bin/ShadowEnginePatch.asi')
            z.write(PROJECT/'packaging/ASI_README.md','README.md')
        report['artifacts'][profile].update(package=str(package),package_sha256=digest(package))
    write_json(output/'report.json',report)
    print(f'{report["status"].upper()}: {output/"report.json"}')
    for name,row in report['profiles'].items():print(f'  {name}: {row["status"]}')
    for blocker in report['blockers']:print('  '+blocker)
    return 0 if report['automatic_release_gate'] else 1 if report['status']=='failed' else 2

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs',type=Path,required=True)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--compiler',type=Path)
    parser.add_argument('--powershell')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--prepare-inputs',action='store_true')
    args=parser.parse_args()
    if args.prepare_inputs:prepare(args.inputs);return 0
    if not args.compiler or not args.powershell:parser.error('--compiler and --powershell required')
    lock=PROJECT/'build/compatibility.run.lock'
    lock.parent.mkdir(exist_ok=True)
    try:
        with lock.open('x') as stream:stream.write(str(os.getpid()))
    except FileExistsError:raise ValueError('Another compatibility run is active, or its lock needs review.')
    try:return build(args)
    finally:lock.unlink()

if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,KeyError) as error:
        print(f'FAILED: {error}',file=sys.stderr);raise SystemExit(1)
