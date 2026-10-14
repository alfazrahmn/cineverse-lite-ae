"""Syntax-check and simulate the host adapter with the owner's Adobe headers."""
from pathlib import Path
import argparse,json,subprocess,re
ROOT=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--sdk',required=True);args=ap.parse_args()
sdk=Path(args.sdk).resolve();build=ROOT/'test-output';build.mkdir(exist_ok=True)
cmd=['g++','-std=c++17','-O2','-ffp-contract=off','-D_WIN32','-D_WIN64','-D_WINDOWS','-D__declspec(x)=',
 '-D__cdecl=','-D__stdcall=','-DCVL_ENABLE_CUDA=0','-include',str(ROOT/'tests/sdk_windows_crt.hpp'),
 '-I'+str(sdk/'Examples/Headers'),'-I'+str(sdk/'Examples/Headers/SP'),'-I'+str(sdk/'Examples/Util'),
 '-Wno-multichar',str(ROOT/'tests/native_host_test.cpp'),str(ROOT/'src/Engine.cpp'),'-o',str(build/'host_test')]
subprocess.run(cmd,check=True)
r=subprocess.run([str(build/'host_test')],check=True,capture_output=True,text=True)
print(r.stdout)
passed=int(re.search(r'PASSED (\d+)',r.stdout)[1])
(ROOT/'VALIDATION_SDK.json').write_text(json.dumps(dict(scope='Linux GCC host-callback simulation using the supplied Adobe SDK 26.5 headers',
 passed=passed,failed=0,native_windows_compiled=False,native_after_effects_executed=False,
 checks=[l.removeprefix('PASS ') for l in r.stdout.splitlines() if l.startswith('PASS ')]),indent=2)+'\n')
