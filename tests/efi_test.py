#!/usr/bin/env python3
"""OVMF -> native Lillyboot EFI -> EFI-stub Linux -> initramfs -> Lilly PID 1."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
from vm_test import image

p=argparse.ArgumentParser()
for name in ['kernel','busybox','lilly','probe','servicectl','logd','loader','boot-tool','ovmf-code','ovmf-vars','log-dir']:
    p.add_argument('--'+name,required=True,type=Path)
p.add_argument('--qemu',default='qemu-system-x86_64')
a=p.parse_args()
a.log_dir.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);esp=root/'boot/efi';esp.mkdir(parents=True)
    shutil.copyfile(a.kernel,root/'kernel')
    shutil.copyfile(a.loader,root/'loader.efi')
    (root/'initrd').write_bytes(image(a.busybox,'boot',a.lilly,a.probe,a.servicectl,a.logd))
    (root/'cmdline').write_text('console=ttyS0 panic=-1 quiet rdinit=/init\n')
    for command in [
        ['install','--efi','/loader.efi','--fallback'],
        ['generate','--version','vm-test','--kernel','/kernel','--initrd','/initrd','--cmdline','/cmdline','--set-default','--timeout','0'],
    ]:
        subprocess.run([str(a.boot_tool),'--root',str(root),*command],check=True)
    variables=root/'OVMF_VARS.fd';shutil.copyfile(a.ovmf_vars,variables)
    cmd=[a.qemu,'-machine','q35','-accel','tcg','-m','256M','-smp','2',
         '-display','none','-serial','stdio','-monitor','none','-nic','none','-no-reboot',
         '-drive',f'if=pflash,format=raw,readonly=on,file={a.ovmf_code}',
         '-drive',f'if=pflash,format=raw,file={variables}',
         '-drive',f'format=raw,file=fat:rw:{esp}']
    try:r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
    except subprocess.TimeoutExpired as e:
        (a.log_dir/'efi.log').write_bytes(e.stdout or b'');raise
    text=r.stdout.decode(errors='replace');(a.log_dir/'efi.log').write_text(text)
    assert r.returncode==0, f'UEFI QEMU failed; see {a.log_dir}'
    for marker in ['LILLYBOOT','lilly: PID 1 operational','LILLY_BOOT_OK','LILLY_CONTROL_AUTH_OK','lilly: shutdown complete']:
        assert marker in text, f'missing {marker}; see {a.log_dir}'
    assert 'Kernel panic' not in text
    print('PASS OVMF -> Lillyboot -> Linux EFI stub -> initramfs -> Lilly PID 1 -> poweroff')
