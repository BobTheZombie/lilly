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

def image(busybox,case,lilly,probe,servicectl=None,logd=None):
    files=[]
    for d in ['bin','sbin','dev','proc','sys','run','etc','etc/lilly','etc/lilly/services','root']:
        files.append((d,stat.S_IFDIR|0o755,b''))
    if case=='services':
        files=[f for f in files if f[0]!='etc/lilly/services']
        for d in ['usr','usr/lib','usr/lib/lilly','usr/lib/lilly/system','etc/lilly/system','etc/lilly/enabled','var','var/log']:
            files.append((d,stat.S_IFDIR|0o755,b''))
    files += [('bin/busybox',stat.S_IFREG|0o755,busybox.read_bytes()),
              ('lilly',stat.S_IFREG|0o755,lilly.read_bytes()),
              ('probe',stat.S_IFREG|0o755,probe.read_bytes())]
    for app in ['sh','sleep','echo','cat','kill','setsid','test','grep','mount','id']:
        files.append(('bin/'+app,stat.S_IFLNK|0o777,b'busybox'))
    if case=='services':
        init=None
        files.extend([('servicectl',stat.S_IFREG|0o755,servicectl.read_bytes()),
                      ('logd',stat.S_IFREG|0o755,logd.read_bytes())])
        units={
          'echo.socket':b'[Socket]\nListenStream=/run/lilly/echo\nService=echo.service\n',
          'echo.service':unit('/probe socket-echo',kind='simple'),
          'log.socket':b'[Socket]\nListenDatagram=/run/lilly/log\nService=log.service\n',
          'log.service':unit('/logd /var/log/lilly.log',kind='simple'),
          'identity.service':unit('/bin/sh -c "test $(id -u) = 65534 && test -w /run/lilly-nobody && echo LILLY_IDENTITY_OK > /run/lilly-nobody/ok"',extra='User=nobody\nGroup=nogroup\nRuntimeDirectory=lilly-nobody\nRuntimeDirectoryMode=0700'),
          'extra.service':unit('/bin/sh -c "echo extra > /run/extra"'),
          'worker.service':unit('/bin/sleep 60',kind='simple'),
          'check.service':unit('/bin/sh /service-check',after='echo.socket log.socket identity.service'),
        }
        for name in ['echo.socket','log.socket','identity.service','check.service']:
            files.append(('etc/lilly/enabled/'+name,stat.S_IFREG|0o644,b''))
        files.append(('etc/passwd',stat.S_IFREG|0o644,b'root:x:0:0:root:/root:/bin/sh\nnobody:x:65534:65534:nobody:/:/bin/sh\n'))
        files.append(('etc/group',stat.S_IFREG|0o644,b'root:x:0:\nnogroup:x:65534:\n'))
        files.append(('etc/nsswitch.conf',stat.S_IFREG|0o644,b'passwd: files\ngroup: files\n'))
        files.append(('service-check',stat.S_IFREG|0o755,b'''#!/bin/sh
set -e
test ! -e /run/extra
/servicectl list-unit-files
grep LILLY_IDENTITY_OK /run/lilly-nobody/ok
if /servicectl is-enabled extra.service; then exit 1; fi
/probe socket-client /run/lilly/echo
echo LILLY_STREAM_OK
/probe log-send /run/lilly/log
sleep 1
grep LILLY_SYSLOG_TEST /var/log/lilly.log
echo LILLY_LOG_OK
/servicectl --now enable extra.service
sleep 1
test -f /run/extra
/servicectl is-enabled extra.service
/servicectl disable extra.service
if /servicectl is-enabled extra.service; then exit 1; fi
/servicectl start worker.service
sleep 1
/servicectl status worker.service
/servicectl restart worker.service
sleep 1
/servicectl status worker.service
/servicectl stop worker.service
sleep 1
/servicectl status worker.service
echo LILLY_SERVICECTL_OK
/servicectl stop echo.socket
sleep 1
test ! -e /run/lilly/echo
echo LILLY_SOCKET_STOP_OK
/servicectl start echo.socket
sleep 1
/probe socket-client /run/lilly/echo
echo LILLY_SOCKET_RESTART_OK
/servicectl poweroff
'''))
    elif case=='invalid':
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
        directory='usr/lib/lilly/system/' if case=='services' else 'etc/lilly/services/'
        files.append((directory+name,stat.S_IFREG|0o644,contents))
    return archive(files)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--kernel',required=True,type=Path)
    parser.add_argument('--busybox',required=True,type=Path)
    parser.add_argument('--qemu',default='qemu-system-x86_64')
    parser.add_argument('--lilly',required=True,type=Path)
    parser.add_argument('--probe',required=True,type=Path)
    parser.add_argument('--servicectl',required=True,type=Path)
    parser.add_argument('--logd',required=True,type=Path)
    parser.add_argument('--log-dir',required=True,type=Path)
    args=parser.parse_args()
    logs=args.log_dir; logs.mkdir(parents=True,exist_ok=True)
    for case in ['boot','idle','invalid','reboot','services']:
        with tempfile.TemporaryDirectory() as tmp:
            initrd=Path(tmp)/'initrd.gz'; initrd.write_bytes(image(args.busybox,case,args.lilly,args.probe,args.servicectl,args.logd))
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
            elif case=='services':
                for marker in ['LILLY_STREAM_OK','LILLY_IDENTITY_OK','LILLY_LOG_OK','LILLY_SERVICECTL_OK','LILLY_SOCKET_STOP_OK','LILLY_SOCKET_RESTART_OK','worker.service inactive']:
                    assert marker in text, f'missing {marker}; see {logs}'
            elif case=='idle':
                assert 'LILLY_IDLE_ALIVE' in text and 'LILLY_ONESHOT_DONE' in text
            elif case=='invalid': assert 'EMERGENCY: PID 1 retained' in text
            else: assert 'shutting down (reboot)' in text
            print(f'PASS QEMU {case}',flush=True)

if __name__=='__main__': main()
