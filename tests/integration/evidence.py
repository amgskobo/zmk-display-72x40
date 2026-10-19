# SPDX-License-Identifier: MIT
"""Record the selected owner bytes for an explicitly retained online build."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


root = Path(sys.argv[1]).resolve()
files = {}
for folder, directories, names in os.walk(root):
    directories[:] = sorted(name for name in directories if name not in ('.git', '__pycache__', 'ci-results'))
    for name in sorted(names):
        path = Path(folder) / name
        if path.is_file() and not path.is_symlink():
            files[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
head = subprocess.run(['git', '-c', 'safe.directory=' + str(root), '-C', str(root),
                       'rev-parse', 'HEAD'], capture_output=True, text=True)
compiler = subprocess.run(['cc', '--version'], capture_output=True, text=True)
json.dump({'schema': 1, 'files': files,
           'owner_head': {'exit': head.returncode, 'stdout': head.stdout, 'stderr': head.stderr},
           'host_compiler': {'exit': compiler.returncode, 'stdout': compiler.stdout,
                             'stderr': compiler.stderr}}, sys.stdout, indent=2)
print()
