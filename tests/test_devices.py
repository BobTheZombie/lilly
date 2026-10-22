"""Exercise provider protocol ordering and bounded failures without host udev."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

BIN=os.environ['LILLY_DEVICE_TEST_BINARY']
FAKE=Path(os.environ['LILLY_DEVICE_TEST_PROVIDER'])
class Devices(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.log=Path(self.tmp.name)/'commands'
        FAKE.write_text('''#!/usr/bin/env python3
import os,sys,time
with open(os.environ['LILLY_FAKE_LOG'],'a') as f:f.write(' '.join(sys.argv[1:])+'\\n')
if os.environ.get('LILLY_FAKE_HANG'): time.sleep(60)
if os.environ.get('LILLY_FAKE_FAIL') and sys.argv[1]=='trigger':sys.exit(7)
''')
        FAKE.chmod(0o755)
        self.env=dict(os.environ,LILLY_FAKE_LOG=str(self.log))
    def tearDown(self):
        FAKE.unlink(missing_ok=True);self.tmp.cleanup()
    def call(self,*args,**env):
        return subprocess.run([BIN,*args],env=dict(self.env,**env),capture_output=True,text=True,timeout=5)
    def test_coldplug_protocol_order(self):
        self.assertEqual(self.call('coldplug').returncode,0)
        self.assertEqual(self.log.read_text().splitlines(),[
            'control --timeout=1 --reload','trigger --type=subsystems --action=add',
            'trigger --type=devices --action=add','settle --timeout=30'])
    def test_failure_prevents_device_trigger_and_settle(self):
        self.assertEqual(self.call('coldplug',LILLY_FAKE_FAIL='1').returncode,7)
        self.assertNotIn('--type=devices',self.log.read_text())
        self.assertNotIn('settle',self.log.read_text())
    def test_settle_timeout_kills_child(self):
        self.assertEqual(self.call('--timeout','1','settle',LILLY_FAKE_HANG='1').returncode,124)
    def test_missing_provider_is_error(self):
        FAKE.unlink()
        self.assertEqual(self.call('ready').returncode,127)
    def test_info_argv_is_not_shell(self):
        self.assertEqual(self.call('info','--query=property','--path=/sys/a b;echo nope').returncode,0)
        self.assertIn('--path=/sys/a b;echo nope',self.log.read_text())
    def test_invalid_command_and_timeout(self):
        self.assertNotEqual(self.call('--timeout','0','settle').returncode,0)
        self.assertNotEqual(self.call('nonsense').returncode,0)
    def test_daemon_foreground(self):
        if os.geteuid():self.skipTest('root-only daemon entry')
        self.assertEqual(self.call('daemon').returncode,0)
        self.assertEqual(self.log.read_text(),'--resolve-names=early\n')
if __name__=='__main__':unittest.main()
