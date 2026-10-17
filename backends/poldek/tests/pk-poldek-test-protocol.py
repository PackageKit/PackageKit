#!/usr/bin/env python3
#
# Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Protocol tests for pk-backend-poldek: the helper is started with the
# protocol on a socketpair and driven the way packagekitd would, without a
# daemon. None of these need poldek data.

import json
import os
import signal
import socket
import subprocess
import sys
import time
import unittest

HELPER = None


class ProtocolTest(unittest.TestCase):
    def setUp(self):
        self.start()

    def start(self, *args):
        self.sock, child = socket.socketpair()
        self.proc = subprocess.Popen(
            [HELPER, *args],
            pass_fds=[child.fileno()],
            stdin=subprocess.DEVNULL,
            env={**os.environ, 'PK_BACKEND_PROTOCOL_FD': str(child.fileno())},
        )
        child.close()
        self.sock.settimeout(10)
        self.reader = self.sock.makefile('r', encoding='utf-8')

    def tearDown(self):
        self.reader.close()
        self.sock.close()
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait()

    def send(self, **req):
        self.sock.sendall((json.dumps(req) + '\n').encode())

    def recv(self):
        line = self.reader.readline()
        self.assertTrue(line.endswith('\n'), 'helper hung up')
        return json.loads(line)

    def run_job(self, job, role, **args):
        self.send(op='run', job=job, role=role, ctx={'locale': 'C'}, **args)
        events = []
        while True:
            ev = self.recv()
            self.assertEqual(ev['job'], job)
            events.append(ev)
            if ev['ev'] == 'finished':
                return events

    def hello(self):
        self.send(op='hello', protocol=1, daemon_version='2.0.0')
        return self.recv()

    def exit(self):
        self.send(op='exit')
        self.assertEqual(self.proc.wait(timeout=10), 0)

    def test_hello(self):
        start = time.monotonic()
        hello = self.hello()
        # the daemon blocks on the handshake, so no poldek setup may happen before it
        self.assertLess(time.monotonic() - start, 2)
        self.assertEqual(hello['ev'], 'hello')
        self.assertEqual(hello['protocol'], 1)
        self.assertEqual(hello['name'], 'poldek')
        self.assertFalse(hello['parallel'])
        for key in ('roles', 'filters', 'groups', 'mime_types'):
            self.assertIsInstance(hello[key], list)
            for value in hello[key]:
                self.assertIsInstance(value, str)
                self.assertNotIn(value, ('', 'unknown'))
        self.assertEqual(len(set(hello['roles'])), len(hello['roles']))
        self.exit()

    def test_unsupported_role(self):
        self.hello()
        for job, role in (('1', 'get-distro-upgrades'), ('2', 'no-such-role')):
            events = self.run_job(job, role)
            self.assertEqual([e['ev'] for e in events], ['error', 'finished'])
            self.assertEqual(events[0]['code'], 'not-supported')
            self.assertIn(role, events[0]['details'])
        self.exit()

    def test_unknown_op(self):
        self.send(op='bogus')  # no job: ignored
        self.send(op='bogus', job='3')
        error = self.recv()
        self.assertEqual(
            (error['ev'], error['job'], error['code']), ('error', '3', 'not-supported')
        )
        self.assertEqual(self.recv(), {'ev': 'finished', 'job': '3'})
        self.assertEqual(self.hello()['ev'], 'hello')
        self.exit()

    def test_cancel_before_the_job(self):
        # a configuration that fails at once: the job runs, it is not cancelled
        self.tearDown()
        self.start('--config', '/nonexistent/poldek.conf')
        self.send(op='cancel', job='4')
        events = self.run_job('4', 'get-repo-list', filters=[])
        codes = [e['code'] for e in events if e['ev'] == 'error']
        self.assertEqual(codes, ['failed-config-parsing'])
        self.exit()

    def test_split_request(self):
        line = json.dumps({'op': 'hello', 'protocol': 1}) + '\n'
        for i in range(len(line)):
            self.sock.sendall(line[i].encode())
            time.sleep(0.002)
        self.assertEqual(self.recv()['ev'], 'hello')
        self.exit()

    def test_hangup(self):
        self.hello()
        self.reader.close()
        self.sock.close()
        self.assertEqual(self.proc.wait(timeout=10), 0)

    def test_sigterm_idle(self):
        self.hello()
        self.proc.send_signal(signal.SIGTERM)
        self.assertEqual(self.proc.wait(timeout=10), 0)

    def test_garbage(self):
        self.sock.sendall(b'not json\n')
        self.assertEqual(self.proc.wait(timeout=10), 1)

    def test_run_without_role(self):
        self.send(op='run', job='5', ctx={})
        self.assertEqual(self.proc.wait(timeout=10), 1)


if __name__ == '__main__':
    HELPER = sys.argv.pop(1)
    unittest.main()
