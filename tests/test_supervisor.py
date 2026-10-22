"""Integration tests run only ordinary child processes, never PID 1 or host services."""
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

BIN = str(Path(__file__).resolve().parents[1] / 'build/lilly')

class SupervisorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
    def tearDown(self):
        self.tmp.cleanup()
    def unit(self, name, command='/bin/true', unit='', service='Type=oneshot'):
        (self.root / (name + '.service')).write_text(
            f'[Unit]\n{unit}\n[Service]\n{service}\nExecStart={command}\n')
    def call(self, command='run', target=None):
        args = [BIN, command, str(self.root)]
        if target: args.append(target + '.service')
        return subprocess.run(args, text=True, capture_output=True, timeout=10)
    def test_oneshot(self):
        self.unit('ok')
        self.assertEqual(self.call().returncode, 0)
    def test_ordering_and_required_pull_in(self):
        self.unit('a', f'/usr/bin/touch {self.root}/ready')
        self.unit('b', f'/usr/bin/test -f {self.root}/ready', 'Requires=a.service\nAfter=a.service')
        self.unit('unused', '/bin/false')
        self.assertEqual(self.call('plan', 'b').stdout.splitlines(), ['a.service', 'b.service'])
        self.assertEqual(self.call(target='b').returncode, 0)
    def test_cycle(self):
        self.unit('a', unit='After=b.service')
        self.unit('b', unit='After=a.service')
        self.assertIn('cycle', self.call('verify').stderr)
    def test_missing_requirement(self):
        self.unit('a', unit='Requires=missing.service')
        self.assertNotEqual(self.call('verify').returncode, 0)
    def test_after_does_not_pull_in(self):
        self.unit('a', unit='After=missing.service')
        self.assertEqual(self.call().returncode, 0)
    def test_fail_closed(self):
        self.unit('a', service='Type=oneshot\nUser=root')
        self.assertIn('unsupported', self.call().stderr)
    def test_exec_failure(self):
        self.unit('a', '/nonexistent/lilly-test')
        r = self.call()
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('exec failed', r.stderr)
        self.assertNotIn('started', r.stderr)
    def test_failed_requirement_blocks_dependent(self):
        self.unit('a', '/bin/false')
        self.unit('b', f'/usr/bin/touch {self.root}/wrong', 'Requires=a.service\nAfter=a.service')
        self.assertNotEqual(self.call().returncode, 0)
        self.assertFalse((self.root / 'wrong').exists())
    def test_restart_is_bounded(self):
        self.unit('a', '/bin/false', service='Type=simple\nRestart=on-failure')
        r = self.call()
        self.assertEqual(r.stderr.count('started a.service'), 5)
        self.assertNotEqual(r.returncode, 0)
    def test_argv_quotes_without_shell(self):
        self.unit('a', f'/usr/bin/touch "{self.root}/hello world"')
        self.assertEqual(self.call().returncode, 0)
        self.assertTrue((self.root / 'hello world').exists())
    def test_invalid_quote(self):
        self.unit('a', '/bin/echo "unfinished')
        self.assertNotEqual(self.call('verify').returncode, 0)
    def test_term_stops_child(self):
        self.unit('a', '/bin/sleep 60', service='Type=simple')
        p = subprocess.Popen([BIN, 'run', str(self.root)], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
        try:
            line = p.stderr.readline()
            self.assertIn('started', line)
            pid = int(line.split('pid=')[1])
            p.send_signal(signal.SIGTERM)
            p.communicate(timeout=6)
            self.assertEqual(p.returncode, 0)
            with self.assertRaises(ProcessLookupError): os.kill(pid, 0)
        finally:
            if p.poll() is None:
                p.kill(); p.communicate()
    def test_duplicate_directive(self):
        self.unit('a', service='Type=simple\nType=oneshot')
        self.assertNotEqual(self.call('verify').returncode, 0)
    def test_requires_is_not_implicit_order(self):
        self.unit('a', unit='Requires=b.service')
        self.unit('b', unit='Requires=a.service')
        self.assertEqual(self.call('verify').returncode, 0)

if __name__ == '__main__': unittest.main()
