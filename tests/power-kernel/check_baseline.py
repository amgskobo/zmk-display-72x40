# SPDX-License-Identifier: MIT
"""Require the reconnect assertion to reject the unchanged prior reporter."""
import argparse, hashlib, json, os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--workspace',type=Path,required=True)
p.add_argument('--baseline-revision',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args(); a.output.mkdir(parents=True,exist_ok=False)
source=a.output/'display_72x40_power.c'
source.write_bytes(subprocess.check_output(['git','show',a.baseline_revision+':src/display_72x40_power.c'],cwd=root))
result=subprocess.run(['bash',str(root/'tests/power-kernel/run.sh'),str(a.workspace)],env=dict(os.environ,ZMK_TEST_OUTPUT=str(a.output/'native'),DISPLAY_POWER_SOURCE=str(source)),capture_output=True,text=True)
(a.output/'console.log').write_text(result.stdout+result.stderr)
assert result.returncode!=0, 'prior reconnect counter unexpectedly passed'
for i in range(1,4):
 log=(a.output/f'native/run-{i}.log').read_text()
 assert 'old callback erased reconnect retries' in log, log
(a.output/'result.json').write_text(json.dumps(dict(baseline=a.baseline_revision,sha256=hashlib.sha256(source.read_bytes()).hexdigest(),exit_code=result.returncode,qualified_failures=3))+'\n')
print('PASS: unchanged prior reporter failed all three reconnect-budget assertions')
