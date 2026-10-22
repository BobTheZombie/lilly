#!/usr/bin/env python3
"""Actual udev/PAM/util-linux in an isolated guest. Only trusted distro ELFs
are inspected with ldd; binaries and their libraries are copied into initramfs.
No daemon, PAM policy, uevent or mount is changed on the host.
"""
import argparse
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
from vm_test import archive,unit

def main():
    p=argparse.ArgumentParser()
    for arg in ('kernel','busybox','lilly','devices','udevd','udevadm','pam-module','pam-test','log-dir'):
        p.add_argument('--'+arg,required=True,type=Path)
    p.add_argument('--qemu',required=True)
    p.add_argument('--runuser',default='/usr/sbin/runuser',type=Path)
    args=p.parse_args()
    files={}
    def directory(name,mode=0o755):files[name]=(stat.S_IFDIR|mode,b'')
    def put(name,data,mode=0o644):
        for parent in Path(name).parents:
            if str(parent)!='.':directory(str(parent))
        files[name]=(stat.S_IFREG|mode,data)
    def elf(source,dest):
        source=source.resolve()
        put(dest,source.read_bytes(),0o755)
        out=subprocess.run(['ldd',str(source)],text=True,capture_output=True,check=True).stdout
        if 'not found' in out:raise RuntimeError(out)
        for lib in re.findall(r'(?:=>\s+|^\s*)(/[^\s]+)',out,re.M):
            name=lib.lstrip('/')
            if name not in files:
                elf(Path(lib),name)
    for d in ('dev','proc','sys','run','etc','root','etc/lilly/services','etc/udev/rules.d','var/log'):directory(d)
    put('bin/busybox',args.busybox.read_bytes(),0o755)
    for app in ('sh','sleep','echo','cat','test','grep','readlink','basename','id','dirname','insmod'):
        files['bin/'+app]=(stat.S_IFLNK|0o777,b'busybox')
    put('lilly',args.lilly.read_bytes(),0o755)
    put('init',b'#!/bin/sh\necho LILLY_DEVICE_FIXTURE\n/bin/busybox ls -l /etc/lilly/services\n/lilly plan /etc/lilly/services\nexec /lilly init /etc/lilly/services\n',0o755)
    elf(args.devices,'usr/bin/lilly-devices')
    elf(args.udevd,'usr/bin/udevd');elf(args.udevadm,'usr/bin/udevadm')
    elf(args.pam_module,'pam_lilly.so');elf(args.pam_test,'pam-test')
    elf(args.runuser,'usr/bin/runuser')
    # These are the existing system PAM modules, used only by a guest policy.
    for name in ('pam_permit.so',):
        candidates=list(Path('/usr/lib').glob('**/security/'+name))
        if not candidates:raise RuntimeError('Install Linux-PAM modules to run this test')
        elf(candidates[0],'usr/lib/security/'+name)
    put('etc/passwd',b'root:x:0:0:root:/root:/bin/sh\nnobody:x:65534:65534:nobody:/:/bin/sh\n')
    put('etc/group',b'root:x:0:\nnogroup:x:65534:\n')
    put('etc/nsswitch.conf',b'passwd: files\ngroup: files\n')
    put('etc/pam.d/runuser',b'auth required /usr/lib/security/pam_permit.so\naccount required /usr/lib/security/pam_permit.so\nsession required /pam_lilly.so\n')
    put('etc/udev/rules.d/99-lilly-test.rules',b'SUBSYSTEM=="block", KERNEL=="vda", ENV{LILLY_TEST}="present", SYMLINK+="lilly-disk"\n')
    put('etc/lilly/services/udevd.service',unit('/usr/bin/lilly-devices daemon',kind='simple'))
    put('etc/lilly/services/ready.service',unit('/usr/bin/lilly-devices --timeout 15 coldplug',extra='TimeoutStartSec=20',after='udevd.service'))
    put('etc/lilly/services/check.service',unit('/bin/sh /check',extra='TimeoutStartSec=50',after='ready.service'))
    put('check',b'''#!/bin/sh
set -ex
trap '/lilly ctl /run/lilly/control poweroff' EXIT
test -b /dev/vda
test -L /dev/lilly-disk
/usr/bin/lilly-devices info --query=property --path=/sys/class/block/vda | grep LILLY_TEST=present
echo LILLY_UDEV_COLDPLUG_OK
echo 'SUBSYSTEM=="block", KERNEL=="vda", ENV{LILLY_RELOAD}="yes", SYMLINK+="lilly-disk"' > /etc/udev/rules.d/99-lilly-test.rules
/usr/bin/lilly-devices reload
/usr/bin/lilly-devices trigger --action=change --subsystem-match=block
/usr/bin/lilly-devices --timeout 10 settle
/usr/bin/lilly-devices info --query=property --path=/sys/class/block/vda | grep LILLY_RELOAD=yes
echo LILLY_UDEV_RELOAD_OK
/pam-test /pam_lilly.so /run/pam-test /run/pam-conf
echo LILLY_PAM_OK
/usr/bin/runuser -u nobody -- /bin/sh -c 'test "$(id -u)" = 65534 && test -n "$XDG_SESSION_ID" && test -d "$XDG_RUNTIME_DIR" && test -w "$XDG_RUNTIME_DIR" && echo LILLY_RUNUSER_OK'
# Kernel hot-removal must remove provider database/symlink state as well.
dev=$(readlink -f /sys/class/block/vda/device)
echo 1 > "$dev/../remove"
sleep 1
/usr/bin/lilly-devices --timeout 10 settle
test ! -e /sys/class/block/vda
test ! -L /dev/lilly-disk
echo LILLY_UDEV_REMOVE_OK
/lilly ctl /run/lilly/control poweroff
''',0o755)
    with tempfile.TemporaryDirectory() as tmp:
        tmp=Path(tmp);initrd=tmp/'initrd.gz';disk=tmp/'disk.img'
        initrd.write_bytes(archive([(n,m,d) for n,(m,d) in sorted(files.items(),key=lambda item:(not stat.S_ISDIR(item[1][0]),len(Path(item[0]).parts),item[0]))]))
        with disk.open('wb') as f:f.truncate(8*1024*1024)
        cmd=[args.qemu,'-accel','tcg','-m','384M','-smp','2','-display','none','-serial','stdio','-monitor','none','-nic','none','-no-reboot',
             '-kernel',str(args.kernel),'-initrd',str(initrd),'-append','console=ttyS0 panic=-1 quiet rdinit=/init',
             '-drive',f'file={disk},format=raw,if=virtio']
        args.log_dir.mkdir(parents=True,exist_ok=True)
        log=args.log_dir/'devices-pam.log'
        try:r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=100)
        except subprocess.TimeoutExpired as e:
            log.write_bytes(e.stdout or b'');raise RuntimeError(f'guest timeout; see {log}') from e
        text=r.stdout.decode(errors='replace');log.write_text(text)
        assert r.returncode==0 and 'Kernel panic' not in text, str(log)
        for marker in ('LILLY_UDEV_COLDPLUG_OK','LILLY_UDEV_RELOAD_OK','LILLY_PAM_OK','LILLY_RUNUSER_OK','LILLY_UDEV_REMOVE_OK','lilly: shutdown complete'):
            assert marker in text,f'missing {marker}: {log}'
        print('PASS actual udev rules/coldplug/reload/removal, Linux-PAM and util-linux runuser')
if __name__=='__main__':main()
