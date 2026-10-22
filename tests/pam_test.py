"""The PAM fixture uses compile-time test directories, never host /run or /etc."""
import shutil
import subprocess
import sys
from pathlib import Path
binary,module,run,conf=sys.argv[1:]
module=str(Path(module).resolve())
for p in (run,conf):
    if Path(p).exists():shutil.rmtree(p)
try:
    result=subprocess.run([binary,module,run,conf])
    raise SystemExit(result.returncode)
finally:
    for p in (run,conf):
        if Path(p).exists():shutil.rmtree(p)
