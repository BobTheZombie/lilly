"""Offline-root configuration and boot publishing tests; no privileged host writes."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

SVC=os.environ['LILLY_TEST_SERVICECTL']
BOOT=os.environ['LILLY_TEST_BOOT']

def pe():
    data=bytearray(128);data[:2]=b'MZ';struct.pack_into('<I',data,60,64);data[64:70]=b'PE\0\0\x64\x86';return data

class AdminTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        (self.root/'usr/lib/lilly/system').mkdir(parents=True)
        (self.root/'usr/lib/lilly/system/test.service').write_text('[Service]\nExecStart=/bin/true\n')
        (self.root/'boot/efi').mkdir(parents=True)
        (self.root/'kernel').write_bytes(pe())
        (self.root/'loader.efi').write_bytes(pe())
        (self.root/'initrd').write_bytes(b'initrd bytes')
        (self.root/'cmdline').write_text('root=UUID=test rw\n')
    def tearDown(self): self.tmp.cleanup()
    def svc(self,*args): return subprocess.run([SVC,'--root',str(self.root),*args],capture_output=True,text=True,timeout=5)
    def boot(self,*args): return subprocess.run([BOOT,'--root',str(self.root),*args],capture_output=True,text=True,timeout=5)
    def generate(self,*extra): return self.boot('generate','--version','test-1','--kernel','/kernel','--initrd','/initrd','--cmdline','/cmdline',*extra)
    def test_enable_disable_idempotent(self):
        for _ in range(2): self.assertEqual(self.svc('enable','test').returncode,0)
        self.assertEqual(self.svc('is-enabled','test').stdout,'enabled\n')
        for _ in range(2): self.assertEqual(self.svc('disable','test').returncode,0)
        self.assertNotEqual(self.svc('is-enabled','test').returncode,0)
    def test_unknown_unit(self): self.assertNotEqual(self.svc('enable','missing').returncode,0)
    def test_path_traversal(self): self.assertNotEqual(self.svc('enable','../evil').returncode,0)
    def test_no_runtime_host_action_for_offline_root(self): self.assertNotEqual(self.svc('start','test').returncode,0)
    def test_list_unit_files(self):
        self.svc('enable','test')
        self.assertIn('test.service enabled',self.svc('list-unit-files').stdout)
    def test_enabled_symlink_rejected(self):
        (self.root/'etc/lilly').mkdir(parents=True)
        (self.root/'elsewhere').mkdir()
        (self.root/'etc/lilly/enabled').symlink_to(self.root/'elsewhere',target_is_directory=True)
        self.assertNotEqual(self.svc('enable','test').returncode,0)
        self.assertEqual(list((self.root/'elsewhere').iterdir()),[])
    def test_generate_entry_and_assets(self):
        r=self.generate('--set-default');self.assertEqual(r.returncode,0,r.stderr)
        entry=(self.root/'boot/efi/loader/entries/lilly-test-1.conf').read_text()
        self.assertIn('options root=UUID=test rw',entry)
        for line in entry.splitlines():
            if line.startswith(('linux ','initrd ')):
                path=self.root/'boot/efi'/line.split(' ',1)[1].lstrip('/')
                self.assertTrue(path.is_file())
        self.assertIn('default lilly-test-1.conf',(self.root/'boot/efi/loader/lilly.conf').read_text())
    def test_regenerate_keeps_previous_assets(self):
        self.assertEqual(self.generate().returncode,0)
        old=(self.root/'boot/efi/loader/entries/lilly-test-1.conf').read_text()
        self.assertEqual(self.generate().returncode,0)
        new=(self.root/'boot/efi/loader/entries/lilly-test-1.conf').read_text()
        self.assertNotEqual(old,new)
        path=next(x.split(' ',1)[1] for x in old.splitlines() if x.startswith('linux '))
        self.assertTrue((self.root/'boot/efi'/path.lstrip('/')).exists())
    def test_bad_kernel_preserves_entry(self):
        self.generate();entry=self.root/'boot/efi/loader/entries/lilly-test-1.conf';old=entry.read_bytes()
        (self.root/'kernel').write_bytes(b'not an EFI kernel')
        self.assertNotEqual(self.generate().returncode,0)
        self.assertEqual(entry.read_bytes(),old)
    def test_no_config_injection(self):
        (self.root/'cmdline').write_text('rw\nlinux /evil\n')
        self.assertNotEqual(self.generate().returncode,0)
    def test_esp_symlink_rejected(self):
        (self.root/'boot/efi').rmdir();(self.root/'elsewhere').mkdir()
        (self.root/'boot/efi').symlink_to(self.root/'elsewhere',target_is_directory=True)
        self.assertNotEqual(self.generate().returncode,0)
    def test_loader_install_preserves_fallback(self):
        path=self.root/'boot/efi/EFI/BOOT';path.mkdir(parents=True)
        (path/'BOOTX64.EFI').write_bytes(b'another bootloader')
        r=self.boot('install','--efi','/loader.efi','--fallback')
        self.assertNotEqual(r.returncode,0)
        self.assertEqual((path/'BOOTX64.EFI').read_bytes(),b'another bootloader')
    def test_loader_install(self):
        r=self.boot('install','--efi','/loader.efi','--fallback')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual((self.root/'boot/efi/EFI/BOOT/BOOTX64.EFI').read_bytes(),pe())
    def test_owned_fallback_can_update(self):
        for _ in range(2):
            r=self.boot('install','--efi','/loader.efi','--fallback')
            self.assertEqual(r.returncode,0,r.stderr)
    def test_update_installed_kernels(self):
        d=self.root/'usr/lib/modules/test-2';d.mkdir(parents=True)
        (d/'vmlinuz').write_bytes(pe())
        (self.root/'boot/initrd-test-2.img').write_bytes(b'initrd')
        r=self.boot('update','--cmdline','/cmdline','--set-default')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertTrue((self.root/'boot/efi/loader/entries/lilly-test-2.conf').exists())
    def test_source_symlink_rejected(self):
        (self.root/'kernel').unlink();(self.root/'kernel').symlink_to('/etc/passwd')
        self.assertNotEqual(self.generate().returncode,0)
    def test_missing_esp_not_created(self):
        (self.root/'boot/efi').rmdir();self.assertNotEqual(self.generate().returncode,0)
        self.assertFalse((self.root/'boot/efi').exists())

if __name__=='__main__': unittest.main()
