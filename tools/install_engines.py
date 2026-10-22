#!/usr/bin/env python3
"""Meson installation bridge: all payload stays below explicit DESTDIR."""
import os
from pathlib import Path
import subprocess
import sys
root=os.environ.get('DESTDIR')
if not root or Path(root).resolve()==Path('/'):
    raise SystemExit('Native engines require a staging DESTDIR; package/install through LPM')
engine,sources,build=sys.argv[1:]
subprocess.run([sys.executable,str(Path(__file__).with_name('build_engines.py')),
                'stage','--engine',engine,'--sources',sources,'--build',build,
                '--destdir',root],check=True)
