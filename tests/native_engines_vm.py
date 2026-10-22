#!/usr/bin/env python3
"""Boot the source-built Lilly engines, never host device/login daemons."""
import argparse
from pathlib import Path
import os,re,stat,subprocess,tempfile
from vm_test import archive,unit

def main():
    p=argparse.ArgumentParser()
    for name in ('stage','kernel','busybox','lilly','servicectl','log-dir'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--qemu',required=True)
    a=p.parse_args();a.stage=a.stage.resolve();files={};seen=set()
    env=dict(os.environ,LD_LIBRARY_PATH=str(a.stage/'usr/lib/lilly/engines'))
    def put(name,data,mode=0o644):
        for parent in Path(name).parents:
            if str(parent)!='.':files.setdefault(str(parent),(stat.S_IFDIR|0o755,b''))
        files[name]=(stat.S_IFREG|mode,data)
    def elf(src,dest):
        src=src.resolve()
        if (src,dest) in seen:return
        seen.add((src,dest));put(dest,src.read_bytes(),0o755)
        out=subprocess.check_output(['ldd',str(src)],text=True,env=env)
        if 'not found' in out:raise RuntimeError(out)
        for name in re.findall(r'(?:=>\s+|^\s*)(/[^\s]+)',out,re.M):
            lib=Path(name)
            target=str(lib.relative_to(a.stage)) if lib.is_relative_to(a.stage) else name.lstrip('/')
            if target not in files:elf(lib,target)
    for d in ('dev','proc','sys','run','etc','root','etc/lilly/services','var/lib','var/log'):
        files[d]=(stat.S_IFDIR|0o755,b'')
    for f in a.stage.rglob('*'):
        if f.is_file():
            data=f.read_bytes();dest=str(f.relative_to(a.stage))
            if data[:4]==b'\x7fELF':elf(f,dest)
            else:put(dest,data,stat.S_IMODE(f.stat().st_mode))
    elf(Path('/usr/bin/dbus-daemon'),'usr/bin/dbus-daemon')
    elf(Path('/usr/bin/busctl'),'usr/bin/busctl')
    elf(Path('/usr/sbin/runuser'),'usr/bin/runuser')
    elf(Path('/usr/lib/x86_64-linux-gnu/security/pam_permit.so'),'pam_permit.so')
    put('bin/busybox',a.busybox.read_bytes(),0o755)
    for name in ('sh','sleep','echo','cat','test','grep','readlink','stat','id','touch'):
        files['bin/'+name]=(stat.S_IFLNK|0o777,b'busybox')
    put('lilly',a.lilly.read_bytes(),0o755)
    put('usr/bin/servicectl',a.servicectl.read_bytes(),0o755)
    files['init']=(stat.S_IFLNK|0o777,b'lilly')
    put('etc/passwd',b'root:x:0:0:root:/root:/bin/sh\nnobody:x:65534:65534:nobody:/:/bin/sh\n')
    groups='root:x:0:\nnogroup:x:65534:\nnobody:x:65534:\n'
    for i,name in enumerate(('disk','tty','input','audio','video','render','kvm','lp','cdrom','tape','dialout','kmem','sgx','users','optical'),100):groups+=f'{name}:x:{i}:\n'
    put('etc/group',groups.encode());put('etc/nsswitch.conf',b'passwd: files\ngroup: files\n')
    put('etc/machine-id',b'0123456789abcdef0123456789abcdef\n')
    put('etc/dbus-1/lilly.conf',b'''<busconfig><type>system</type><listen>unix:path=/run/dbus/system_bus_socket</listen><auth>EXTERNAL</auth>
<policy context="default"><allow user="*"/><deny own="*"/><allow send_destination="*"/><allow receive_sender="*"/></policy>
<policy user="root"><allow own="*"/></policy></busconfig>''')
    put('etc/pam.d/runuser',b'auth required /pam_permit.so\naccount required /pam_permit.so\nsession required /usr/lib/security/pam_lilly_login.so debug\n')
    put('etc/elogind/logind.conf',b'[Login]\nKillUserProcesses=no\nHandlePowerKey=ignore\nHandleLidSwitch=ignore\n')
    put('etc/udev/hwdb.d/99-lilly-native.hwdb',b'lilly:test\n LILLY_HWDB=native\n')
    put('etc/udev/rules.d/99-lilly-native.rules',b'''SUBSYSTEM!="block", GOTO="lilly_end"
KERNEL!="vda", GOTO="lilly_end"
ATTR{size}=="16384", ENV{LILLY_MATCH}="yes"
PROGRAM=="/bin/sh -c 'echo native-program'", RESULT=="native-program", ENV{LILLY_PROGRAM}="yes"
IMPORT{builtin}="hwdb lilly:test"
IMPORT{program}="/bin/sh -c 'echo LILLY_IMPORT=imported'"
ENV{LILLY_LIST}="one", ENV{LILLY_LIST}+="two"
SYMLINK+="lilly-native-disk", MODE:="0640"
MODE="0666"
RUN+="/bin/sh -c 'echo native-run > /run/lilly-rule-ran'"
LABEL="lilly_end"
''')
    units={
      'bus.service':unit('/usr/bin/dbus-daemon --nofork --nopidfile --config-file=/etc/dbus-1/lilly.conf',kind='simple',extra='RuntimeDirectory=dbus'),
      'udevd.service':unit('/usr/lib/lilly/engines/lilly-udevd --resolve-names=early',kind='simple'),
      'trigger.service':unit('/bin/sh /coldplug',after='udevd.service'),
      'syslog.service':unit('/bin/busybox syslogd -n -O /dev/console',kind='simple'),
      'logind.service':unit('/bin/sh /start-logind',kind='simple',extra='Restart=on-failure\nRestartSec=1',after='bus.service trigger.service'),
      'check.service':unit('/bin/sh /check',extra='TimeoutStartSec=60',after='logind.service syslog.service'),
    }
    for name,data in units.items():put('etc/lilly/services/'+name,data)
    put('start-logind',b'#!/bin/sh\nexport SYSTEMD_LOG_TARGET=console SYSTEMD_LOG_LEVEL=debug\nexec /usr/lib/lilly/engines/lilly-logind\n',0o755)
    put('coldplug',b'''#!/bin/sh
set -e
n=0
until /usr/bin/lilly-udevadm control --timeout=1 --reload; do
 n=$((n+1)); test "$n" -lt 20; sleep .1
done
/usr/bin/lilly-hwdb update
/usr/bin/lilly-hwdb query lilly:test | grep LILLY_HWDB=native
/usr/bin/lilly-udevadm trigger --type=subsystems --action=add
/usr/bin/lilly-udevadm trigger --type=devices --action=add
/usr/bin/lilly-udevadm settle --timeout=20
''',0o755)
    put('check',b'''#!/bin/sh
set -ex
trap '/lilly ctl /run/lilly/control poweroff' EXIT
/usr/bin/lilly-udevadm info --query=property --path=/sys/class/block/vda > /run/properties
cat /run/properties
for v in LILLY_MATCH=yes LILLY_PROGRAM=yes LILLY_IMPORT=imported LILLY_HWDB=native "LILLY_LIST=one two"; do grep -x "$v" /run/properties; done
test -L /dev/lilly-native-disk
test "$(stat -c %a /dev/vda)" = 640
grep native-run /run/lilly-rule-ran
echo LILLY_NATIVE_RULES_OK
n=0
until /usr/bin/busctl introspect org.freedesktop.login1 /org/freedesktop/login1 > /run/login1-api; do
 n=$((n+1)); test "$n" -lt 20; sleep .2
done
for method in ListSessions ListSessionsEx ListUsers ListSeats ListInhibitors CreateSessionWithPIDFD Inhibit CanPowerOff CanSuspend; do grep "$method" /run/login1-api; done
/usr/bin/lilly-loginctl list-seats
/usr/bin/busctl call org.freedesktop.login1 /org/freedesktop/login1 org.freedesktop.login1.Manager CanPowerOff
echo LILLY_LOGIN1_API_OK
/usr/bin/lilly-inhibit --what=sleep --who=lilly-native-test --why=validation --mode=block /bin/sh -c '/usr/bin/busctl call org.freedesktop.login1 /org/freedesktop/login1 org.freedesktop.login1.Manager ListInhibitors | grep lilly-native-test'
sleep .2
if /usr/bin/busctl call org.freedesktop.login1 /org/freedesktop/login1 org.freedesktop.login1.Manager ListInhibitors | grep lilly-native-test; then exit 1; fi
echo LILLY_LOGIN1_INHIBITOR_OK
/usr/bin/runuser -u nobody -- /bin/sh -c 'test "$(id -u)" = 65534 && test -n "$XDG_SESSION_ID" && test -w "$XDG_RUNTIME_DIR" && /usr/bin/lilly-loginctl --no-pager show-session "$XDG_SESSION_ID" -p Id -p User -p State && echo LILLY_LOGIN1_PAM_OK && if /usr/bin/busctl call org.freedesktop.login1 /org/freedesktop/login1 org.freedesktop.login1.Manager PowerOff b false; then exit 1; else echo LILLY_LOGIN1_AUTH_OK; fi' < /dev/ttyS0
echo LILLY_NATIVE_ENGINES_OK
trap - EXIT
/usr/bin/busctl call org.freedesktop.login1 /org/freedesktop/login1 org.freedesktop.login1.Manager PowerOff b false
sleep 30
''',0o755)
    with tempfile.TemporaryDirectory() as tmp:
        tmp=Path(tmp);initrd=tmp/'initrd.gz';disk=tmp/'disk.img'
        initrd.write_bytes(archive([(n,m,d) for n,(m,d) in sorted(files.items(),key=lambda item:(not stat.S_ISDIR(item[1][0]),len(Path(item[0]).parts),item[0]))]))
        with disk.open('wb') as f:f.truncate(8*1024*1024)
        args=[a.qemu,'-accel','tcg','-m','512M','-smp','2','-display','none','-serial','stdio','-monitor','none','-nic','none','-no-reboot',
              '-kernel',str(a.kernel),'-initrd',str(initrd),'-append','console=ttyS0 panic=-1 quiet rdinit=/init',
              '-drive',f'file={disk},format=raw,if=virtio']
        a.log_dir.mkdir(parents=True,exist_ok=True);log=a.log_dir/'native-engines.log'
        try:r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=100)
        except subprocess.TimeoutExpired as e:log.write_bytes(e.stdout or b'');raise RuntimeError(f'guest timeout: {log}') from e
        text=r.stdout.decode(errors='replace');log.write_text(text)
        assert r.returncode==0 and 'Kernel panic' not in text,str(log)
        for marker in ('LILLY_NATIVE_RULES_OK','LILLY_LOGIN1_API_OK','LILLY_LOGIN1_INHIBITOR_OK','LILLY_LOGIN1_PAM_OK','LILLY_LOGIN1_AUTH_OK','LILLY_NATIVE_ENGINES_OK','lilly: shutdown complete'):
            assert marker in text.splitlines(),f'missing {marker}: {log}'
        print('PASS standalone native rule engine, login1 D-Bus, inhibitor FD lifetime and full PAM session')
if __name__=='__main__':main()
