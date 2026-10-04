"""Record an explicitly reported manual test; never infer or perform one."""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import json
from matrix import PROJECT, contracts, digest, report_failures
from runner import source_hashes

def record(report_path, output, profile, build_profile, observation):
    report=json.loads(report_path.read_text(encoding='utf-8'))
    if report.get('schema')!=1:raise ValueError('Unsupported report schema.')
    if not observation.strip():raise ValueError('An actual manual observation is required.')
    if report['contracts']!=contracts():raise ValueError('Source compatibility contract changed since the tested build.')
    if report.get('source_sha256')!=source_hashes():raise ValueError('Source changed since the tested artifact was built.')
    if report['status']=='failed' or report_failures(report['profiles'],report['steps']):
        raise ValueError('Cannot calibrate failed regression gates.')
    if report['profiles'].get(profile,{}).get('status')!='pass':raise ValueError('Tested profile lacks offline proof.')
    artifact=report['artifacts'][build_profile]
    if digest(artifact['path'])!=artifact['sha256']:raise ValueError('Tested artifact changed.')
    baseline={'schema':1,'created_utc':datetime.now(timezone.utc).isoformat(),
        'contracts':report['contracts'],'compiler_sha256':report['compiler_sha256'],
        'profile_features':{k:v['features'] for k,v in report['profiles'].items()},
        'manual_observations':[{'profile':profile,'build_profile':build_profile,'asi_sha256':artifact['sha256'],
                                'observation':observation,'report_sha256':digest(report_path)}]}
    # A calibration is immutable. To replace it, choose a new filename deliberately.
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('x',encoding='utf-8') as stream:json.dump(baseline,stream,indent=2);stream.write('\n')
    return baseline

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=PROJECT/'build/compatibility-baseline.json')
    parser.add_argument('--tested-profile',required=True)
    parser.add_argument('--build-profile',choices=['Internal','Release'],default='Internal')
    parser.add_argument('--observation',required=True)
    args=parser.parse_args()
    record(args.report,args.output,args.tested_profile,args.build_profile,args.observation)
    print(f'Recorded the stated test only: {args.tested_profile}. Other profiles remain offline evidence, not manual passes.')
