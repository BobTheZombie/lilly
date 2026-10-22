"""Integration tests run only ordinary child processes, never PID 1 or host services."""
import os
from pathlib import Path
import signal
import socket
import sys
import subprocess
import tempfile
import time
import unittest

BIN = os.environ['LILLY_TEST_BINARY']

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

    def test_after_failure_is_not_requires(self):
        self.unit('a', '/bin/false')
        self.unit('b', f'/usr/bin/touch {self.root}/ran', 'After=a.service')
        self.assertNotEqual(self.call().returncode,0)
        self.assertTrue((self.root/'ran').exists())
    def test_parallel_oneshots(self):
        for name, peer in [('a', 'b'), ('b', 'a')]:
            script=self.root / (name + '.py')
            script.write_text('import pathlib,time\n'
                + f'root=pathlib.Path({str(self.root)!r})\n'
                + f'(root / {name!r}).touch()\n'
                + 'deadline=time.monotonic()+2\n'
                + f'while not (root / {peer!r}).exists():\n'
                + ' if time.monotonic()>deadline: raise SystemExit(1)\n time.sleep(.01)\n')
            self.unit(name, f'{sys.executable} {script}')
        self.assertEqual(self.call().returncode, 0)
    def test_oneshot_timeout(self):
        self.unit('a', '/bin/sleep 60', service='Type=oneshot\nTimeoutStartSec=1\nTimeoutStopSec=1')
        t=time.monotonic(); r=self.call()
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('start timeout', r.stderr)
        self.assertLess(time.monotonic()-t, 4)
    def test_rt_priority_validation(self):
        self.unit('a', service='Type=simple\nCPUSchedulingPolicy=rr\nCPUSchedulingPriority=0')
        self.assertNotEqual(self.call('verify').returncode, 0)
    def test_nonrt_priority_validation(self):
        self.unit('a', service='Type=simple\nCPUSchedulingPolicy=other\nCPUSchedulingPriority=1')
        self.assertNotEqual(self.call('verify').returncode, 0)
    def test_affinity_and_nice(self):
        cpu=min(os.sched_getaffinity(0))
        script=self.root / 'probe.py'
        script.write_text(f'import os; assert os.sched_getaffinity(0)=={{{cpu}}}; assert os.getpriority(os.PRIO_PROCESS,0)>=10')
        self.unit('a', f'{sys.executable} {script}', service=f'Type=oneshot\nCPUAffinity={cpu}\nNice=10')
        self.assertEqual(self.call().returncode, 0)
    def test_bad_cpu_range(self):
        self.unit('a', service='Type=oneshot\nCPUAffinity=2-1')
        self.assertNotEqual(self.call('verify').returncode, 0)
    def test_bad_rt_limit(self):
        self.unit('a', service='Type=oneshot\nLimitRTTIME=unlimited')
        self.assertNotEqual(self.call('verify').returncode, 0)
    def test_restart_always(self):
        self.unit('a', service='Type=simple\nRestart=always', unit='StartLimitBurst=3')
        r=self.call()
        self.assertEqual(r.stderr.count('started a.service'), 3)
        self.assertIn('start limit hit', r.stderr)
    def socket_available(self):
        try: s=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET); s.close()
        except OSError as e: self.skipTest(f'host disallows Unix sockets: {e}')
    def test_control_socket_and_delayed_request(self):
        self.socket_available()
        self.unit('a', '/bin/sleep 60', service='Type=simple')
        path=str(self.root / 'control')
        p=subprocess.Popen([BIN,'run',str(self.root)], env={**os.environ,'LILLY_CONTROL_SOCKET':path},
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            self.assertIn('started',p.stderr.readline())
            with socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET) as c:
                c.connect(path); time.sleep(.1); c.send(b'status')
                self.assertIn(b'a.service running',c.recv(32768))
            r=subprocess.run([BIN,'ctl',path,'stop'],capture_output=True,text=True,timeout=5)
            self.assertEqual(r.stdout,'accepted\n')
            p.communicate(timeout=5)
            self.assertEqual(p.returncode,0)
            self.assertFalse(Path(path).exists())
        finally:
            if p.poll() is None: p.kill(); p.communicate()
    def test_socket_not_overwritten(self):
        self.socket_available()
        path=self.root / 'control'; path.write_text('keep')
        self.unit('a')
        r=subprocess.run([BIN,'run',str(self.root)],env={**os.environ,'LILLY_CONTROL_SOCKET':str(path)},capture_output=True,timeout=5)
        self.assertNotEqual(r.returncode,0)
        self.assertEqual(path.read_text(),'keep')
    def test_requires_runtime_failure(self):
        self.unit('a', '/bin/sh -c "sleep .2; exit 1"',service='Type=simple')
        self.unit('b','/bin/sleep 60','Requires=a.service\nAfter=a.service',service='Type=simple')
        t=time.monotonic(); r=self.call()
        self.assertNotEqual(r.returncode,0)
        self.assertIn('stopping b.service',r.stderr)
        self.assertLess(time.monotonic()-t,3)
    def test_shutdown_reverse_after_order(self):
        for name in ['a','b']:
            script=self.root / (name+'.py')
            script.write_text('import pathlib,signal,time,sys\n'
                + f'root=pathlib.Path({str(self.root)!r})\n'
                + 'def stop(sig,frame):\n'
                + f' with (root/"stopped").open("a") as f: f.write({name!r}+"\\n")\n'
                + ' time.sleep(.1)\n sys.exit(0)\n'
                + 'signal.signal(signal.SIGTERM,stop)\n'
                + f'(root/{(name+"-ready")!r}).touch()\n'
                + 'while True: time.sleep(.1)\n')
            self.unit(name,f'{sys.executable} {script}',
                      'Requires=a.service\nAfter=a.service' if name=='b' else '',service='Type=simple')
        p=subprocess.Popen([BIN,'run',str(self.root)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            end=time.monotonic()+3
            while not (self.root/'b-ready').exists():
                if p.poll() is not None or time.monotonic()>end: self.fail('services did not become ready')
                time.sleep(.01)
            p.send_signal(signal.SIGTERM); p.communicate(timeout=6)
            self.assertEqual(p.returncode,0)
            self.assertEqual((self.root/'stopped').read_text().splitlines(),['b','a'])
        finally:
            if p.poll() is None: p.kill(); p.communicate()
    def test_init_rejected_outside_pid1(self):
        r=self.call('init')
        self.assertNotEqual(r.returncode,0)
        self.assertIn('requires PID 1',r.stderr)

if __name__ == '__main__': unittest.main()
