#!/usr/bin/env python3
"""Boot the actual static C binary as PID 1 in disposable QEMU/TCG guests.
Requires QEMU, a Linux kernel with built-in cgroup v2/devtmpfs/devpts, and
static BusyBox. No host mount, root, networking or init replacement is used.
"""
import argparse
import gzip
from pathlib import Path
import stat
import subprocess
import tempfile


def archive(files):
    data=bytearray()
    for ino,(name,mode,payload) in enumerate(files+[('TRAILER!!!',0,b'')],1):
        encoded=name.encode()+b'\0'
        fields=[ino,mode,0,0,1,0,len(payload),0,0,0,0,len(encoded),0]
        data.extend(b'070701'+b''.join(f'{n:08x}'.encode() for n in fields))
        data.extend(encoded); data.extend(b'\0'*(-len(data)%4))
        data.extend(payload); data.extend(b'\0'*(-len(data)%4))
    return gzip.compress(data)

def unit(command,kind='oneshot',extra='',after=''):
    return f'[Unit]\nAfter={after}\n[Service]\nType={kind}\nExecStart={command}\n{extra}\n'.encode()

def image(busybox,case,lilly,probe):
    files=[]
    for d in ['bin','sbin','dev','proc','sys','run','etc','etc/lilly','etc/lilly/services','root']:
        files.append((d,stat.S_IFDIR|0o755,b''))
    files += [('bin/busybox',stat.S_IFREG|0o755,busybox.read_bytes()),
              ('lilly',stat.S_IFREG|0o755,lilly.read_bytes()),
              ('probe',stat.S_IFREG|0o755,probe.read_bytes())]
    for app in ['sh','sleep','echo','cat','kill','setsid','test','grep','mount']:
        files.append(('bin/'+app,stat.S_IFLNK|0o777,b'busybox'))
    if case=='invalid':
        init=b'#!/bin/sh\nmount -t devtmpfs devtmpfs /dev\n(sleep 3; kill -TERM 1) &\nexec /lilly\n'
        units={'bad.service':b'[Service]\nUnsupported=yes\n'}
    elif case=='idle':
        init=b'#!/bin/sh\nmount -t devtmpfs devtmpfs /dev\n(sleep 3; echo LILLY_IDLE_ALIVE; kill -TERM 1) &\nexec /lilly\n'
        units={'done.service':unit('/bin/echo LILLY_ONESHOT_DONE')}
    elif case=='reboot':
        init=None
        units={'reboot.service':unit('/lilly ctl /run/lilly/control reboot')}
    else:
        init=None
        units={
          'probe.service':unit('/probe',extra='CPUSchedulingPolicy=rr\nCPUSchedulingPriority=10\nCPUAffinity=0\nLimitRTTIME=200000'),
          'escape.service':unit('/bin/sh /escape'),
          'spin.service':unit('/probe spin',kind='simple',extra='CPUSchedulingPolicy=rr\nCPUSchedulingPriority=9\nLimitRTTIME=10000'),
          'check.service':unit('/bin/sh /check',after='probe.service escape.service'),
        }
        files.append(('escape',stat.S_IFREG|0o755,b'''#!/bin/sh
setsid /bin/sh -c 'echo ready > /run/escape-ready; exec /bin/sleep 60' &
echo $! > /run/escaped
while ! test -f /run/escape-ready; do sleep .1; done
echo LILLY_ESCAPE_STARTED
'''))
        files.append(('check',stat.S_IFREG|0o755,b'''#!/bin/sh
sleep 2
if test -c /dev/null; then echo LILLY_DEV_OK; fi
if kill -0 "$(cat /run/escaped)" 2>/dev/null; then
 echo LILLY_CGROUP_FAIL
else
 echo LILLY_CGROUP_OK
fi
/lilly ctl /run/lilly/control status
if ! /probe control-test; then echo LILLY_CONTROL_FAIL; fi
LILLY_CONTROL_SOCKET=/run/lilly/control /lilly run /etc/lilly/services
if test $? -ne 0; then echo LILLY_SOCKET_PRESERVED; fi
echo LILLY_BOOT_OK
/lilly ctl /run/lilly/control poweroff
'''))
    if init: files.append(('init',stat.S_IFREG|0o755,init))
    else: files.append(('init',stat.S_IFLNK|0o777,b'lilly'))
    for name,contents in units.items():
        files.append(('etc/lilly/services/'+name,stat.S_IFREG|0o644,contents))
    return archive(files)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--kernel',required=True,type=Path)
    parser.add_argument('--busybox',required=True,type=Path)
    parser.add_argument('--qemu',default='qemu-system-x86_64')
    parser.add_argument('--lilly',required=True,type=Path)
    parser.add_argument('--probe',required=True,type=Path)
    parser.add_argument('--log-dir',required=True,type=Path)
    args=parser.parse_args()
    logs=args.log_dir; logs.mkdir(parents=True,exist_ok=True)
    for case in ['boot','idle','invalid','reboot']:
        with tempfile.TemporaryDirectory() as tmp:
            initrd=Path(tmp)/'initrd.gz'; initrd.write_bytes(image(args.busybox,case,args.lilly,args.probe))
            cmd=[args.qemu,'-accel','tcg','-m','256M','-smp','2','-display','none',
                 '-serial','stdio','-monitor','none','-nic','none','-no-reboot',
                 '-kernel',str(args.kernel),'-initrd',str(initrd),
                 '-append','console=ttyS0 panic=-1 quiet rdinit=/init']
            try: r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
            except subprocess.TimeoutExpired as e:
                (logs/(case+'.log')).write_bytes(e.stdout or b'')
                raise RuntimeError(f'{case}: guest timeout; see {logs}') from e
            text=r.stdout.decode(errors='replace'); (logs/(case+'.log')).write_text(text)
            assert r.returncode==0, f'{case}: QEMU failed ({r.returncode}); see {logs}'
            assert 'Kernel panic' not in text, f'{case}: kernel panic'
            assert 'lilly: shutdown complete' in text, f'{case}: no orderly shutdown; see {logs}'
            if case=='boot':
                for marker in ['LILLY_BOOT_OK','LILLY_DEV_OK','LILLY_ESCAPE_STARTED','LILLY_CGROUP_OK','policy=2 priority=10 cpu=0 rttime=200000',
                               'fork_policy=0','LILLY_CONTROL_DELAY_OK','LILLY_CONTROL_AUTH_OK','LILLY_SOCKET_PRESERVED','spin.service status=9','lilly pid=1 state=running']:
                    assert marker in text, f'missing {marker}; see {logs}'
                assert 'LILLY_CGROUP_FAIL' not in text and 'LILLY_CONTROL_FAIL' not in text
            elif case=='idle':
                assert 'LILLY_IDLE_ALIVE' in text and 'LILLY_ONESHOT_DONE' in text
            elif case=='invalid': assert 'EMERGENCY: PID 1 retained' in text
            else: assert 'shutting down (reboot)' in text
            print(f'PASS QEMU {case}',flush=True)

if __name__=='__main__': main()
