"""Run ordered, reproducible QA and write a source-bound result manifest."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--soak-seconds',type=int,default=300)
args=parser.parse_args()
(root/'qa/logs').mkdir(parents=True,exist_ok=True)

def source_hash():
    digest=hashlib.sha256()
    paths=[root/'Makefile']
    for folder in ('desktop','boot','kernel','tools','tests','programs','vendor','assets'):
        paths.extend(p for p in (root/folder).rglob('*') if p.suffix in ('.c','.h','.S','.ld','.py','.pem','.ttf','.json','.inc'))
    for path in sorted(paths):digest.update(str(path.relative_to(root)).encode()+b'\0'+path.read_bytes())
    return digest.hexdigest()

steps=[('build',['make','iso']),('keyboard',['make','test-input']),('fat-parser',['make','test-fat']),('assembler',['make','test-assembler']),
       ('native-sanitizers',['make','test-native-sanitizers']),('network-parser',['make','test-net-parser']),
       ('renderer-sanitizers',['make','test-render-sanitizers']),('web-parser',['make','test-web-parser']),('font',['make','test-font']),('web-images',['make','test-web-images']),
       ('native-https',['python3','tests/https_native.py']),('native-web',['python3','tests/web_native.py']),('browser-comfort',['python3','tests/browser_comfort.py']),('window-manager',['python3','tests/windows08.py']),('desktop',['python3','tests/desktop.py']),
       ('usability',['python3','tests/usability.py']),('editor-clipboard',['python3','tests/editor_clipboard.py']),('typing-burst',['python3','tests/input_burst.py']),
       ('desktop-extensions',['python3','tests/desktop_extensions.py']),
       ('daily-workflow',['python3','tests/daily_workflow.py']),
       ('usb-storage',['python3','tests/usb_storage.py']),('usb-variants',['python3','tests/usb_variants.py']),
       ('failure-feedback',['python3','tests/failure_feedback.py']),
       ('cursor-correctness',['python3','tests/render_correctness.py']),('large-http',['python3','tests/network_long.py']),
       ('slow-http',['python3','tests/browser_responsiveness.py']),('fault-frames',['python3','tests/fault_reporting.py']),
       ('display-modes',['python3','tests/display_profiles.py']),('install',['make','test-install']),
       ('layout-persistence',['python3','tests/layout_persistence.py']),
       ('corrupt-state',['python3','tests/corrupt_state.py']),('profile-install',['python3','tests/profile_install.py']),
       ('usb-boot',['python3','tests/desktop.py','--usb']),('legacy',['make','test32']),
       ('performance',['python3','tests/render_perf.py','--output','qa/performance-final.json']),
       ('soak',['python3','tests/soak.py','--seconds',str(args.soak_seconds)])]
report={'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_sha256':source_hash(),'steps':[]}
manifest=root/'qa/latest-results.json'
for name,command in steps:
    begin=time.monotonic();log_path=root/'qa/logs'/f'{name}.log'
    print(f'RUN {name}',flush=True)
    with log_path.open('w') as output:
        result=subprocess.run(command,cwd=root,stdout=output,stderr=subprocess.STDOUT)
    record={'name':name,'command':command,'exit_code':result.returncode,'seconds':round(time.monotonic()-begin,2),'log':str(log_path.relative_to(root))}
    report['steps'].append(record);manifest.write_text(json.dumps(report,indent=2)+'\n')
    if result.returncode:
        print(log_path.read_text(errors='replace')[-2500:],flush=True)
        raise SystemExit(f'FAIL {name}; see {log_path}')
    print(f'PASS {name} ({record["seconds"]}s)',flush=True)
report['finished_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
report['source_unchanged']=source_hash()==report['source_sha256']
report['all_passed']=report['source_unchanged'] and all(step['exit_code']==0 for step in report['steps'])
manifest.write_text(json.dumps(report,indent=2)+'\n')
if not report['all_passed']:raise SystemExit('Sources changed during QA; rerun to validate the final source tree')
print(f'ALL {len(steps)} QA STEPS PASSED',flush=True)
