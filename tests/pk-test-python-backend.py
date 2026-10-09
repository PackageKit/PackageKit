#!/usr/bin/env python3
#
# Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Tests for the packagekit_backend library: a test backend defined below is
# started as a helper process with the protocol on a socketpair, and driven
# the way packagekitd would, without a daemon.

import json
import os
import signal
import socket
import subprocess
import sys
import time
import unittest

from packagekit_backend import (
    ERROR_INTERNAL_ERROR,
    ERROR_NOT_SUPPORTED,
    ERROR_TRANSACTION_CANCELLED,
    INFO_AVAILABLE,
    INFO_INSTALLED,
    STATUS_QUERY,
    Backend,
    PkError,
)


class TestBackend(Backend):
    name = 'test'
    description = 'Test backend'
    author = 'PackageKit'
    filters = ('installed',)
    groups = ('system',)
    batch_size = 2

    def search_name(self, filters, values):
        self.status(STATUS_QUERY)
        self.log(
            'info',
            'env LANG=%s http_proxy=%s' % (os.environ.get('LANG'), os.environ.get('http_proxy')),
        )
        for i, value in enumerate(values):
            self.package('%s;1.%d;x86_64;main;' % (value, i), INFO_AVAILABLE, 'A ' + value)
        self.package('ctx;%d;x86_64;main;' % self.ctx.uid, INFO_INSTALLED, str(filters))

    def get_details(self, package_ids):
        for package_id in package_ids:
            self.details(package_id, summary='s', license='MIT', description='d\ne')
        self.percentage(None)  # flushes the batch before it is sent
        self.percentage(100)

    def refresh_cache(self, force):
        if force:
            raise PkError(ERROR_NOT_SUPPORTED, 'forced refresh')
        raise ValueError('boom')

    def install_packages(self, flags, package_ids):
        self.allow_cancel(True)
        for _ in range(500):
            self.check_cancelled()
            time.sleep(0.01)
        raise AssertionError('not cancelled')

    def remove_packages(self, flags, package_ids, allow_deps, autoremove):
        # only SIGTERM gets us out of here
        time.sleep(30)


class PythonBackendTest(unittest.TestCase):
    def setUp(self):
        self.sock, child = socket.socketpair()
        self.proc = subprocess.Popen(
            [sys.executable, __file__, '--helper'],
            pass_fds=[child.fileno()],
            env={**os.environ, 'PK_BACKEND_PROTOCOL_FD': str(child.fileno())},
        )
        child.close()
        self.sock.settimeout(10)
        self.reader = self.sock.makefile('r', encoding='utf-8')

    def tearDown(self):
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
        ctx = {'locale': 'de_DE.UTF-8', 'uid': 1000, 'proxy': {'http': 'proxy:3128'}}
        self.send(op='run', job=job, role=role, ctx=ctx, **args)
        events = []
        while True:
            ev = self.recv()
            self.assertEqual(ev['job'], job)
            events.append(ev)
            if ev['ev'] == 'finished':
                return events

    def exit(self):
        self.send(op='exit')
        self.assertEqual(self.proc.wait(timeout=10), 0)

    def test_hello(self):
        self.send(op='hello', protocol=1, daemon_version='2.0.0')
        hello = self.recv()
        self.assertEqual(hello['ev'], 'hello')
        self.assertEqual(hello['protocol'], 1)
        self.assertEqual(hello['name'], 'test')
        self.assertEqual(hello['description'], 'Test backend')
        self.assertEqual(hello['filters'], ['installed'])
        self.assertEqual(hello['groups'], ['system'])
        self.assertEqual(hello['mime_types'], [])
        self.assertFalse(hello['parallel'])
        self.assertEqual(
            hello['roles'],
            ['search-name', 'get-details', 'install-packages', 'remove-packages', 'refresh-cache'],
        )
        self.exit()

    def test_search_batches_ctx(self):
        events = self.run_job('1', 'search-name', filters=['installed'], values=['a', 'b', 'c'])
        self.assertEqual(
            [e['ev'] for e in events], ['status', 'log', 'packages', 'packages', 'finished']
        )
        self.assertEqual(events[0]['status'], 'query')
        self.assertEqual(events[1]['message'], 'env LANG=de_DE.UTF-8 http_proxy=http://proxy:3128')
        items = events[2]['items'] + events[3]['items']
        self.assertEqual(len(events[2]['items']), 2)
        self.assertEqual(
            [i['package_id'] for i in items],
            [
                'a;1.0;x86_64;main;',
                'b;1.1;x86_64;main;',
                'c;1.2;x86_64;main;',
                'ctx;1000;x86_64;main;',
            ],
        )
        self.assertEqual(
            items[0], {'package_id': 'a;1.0;x86_64;main;', 'info': 'available', 'summary': 'A a'}
        )
        self.assertEqual(items[3]['summary'], "['installed']")

    def test_details_flush_before_other_event(self):
        events = self.run_job('2', 'get-details', package_ids=['x;1;a;r;'])
        self.assertEqual(
            [e['ev'] for e in events], ['details', 'percentage', 'percentage', 'finished']
        )
        item = events[0]['items'][0]
        self.assertEqual(item['description'], 'd\ne')
        self.assertIsNone(item['size'])
        self.assertEqual(item['group'], 'unknown')
        self.assertIsNone(events[1]['value'])
        self.assertEqual(events[2]['value'], 100)

    def test_errors(self):
        events = self.run_job('3', 'refresh-cache', force=True)
        self.assertEqual([e['ev'] for e in events], ['error', 'finished'])
        self.assertEqual(events[0]['code'], ERROR_NOT_SUPPORTED)
        self.assertEqual(events[0]['details'], 'forced refresh')

        events = self.run_job('4', 'refresh-cache', force=False)
        self.assertEqual(events[0]['code'], ERROR_INTERNAL_ERROR)
        self.assertIn('ValueError: boom', events[0]['details'])

        events = self.run_job('5', 'get-updates', filters=[])
        self.assertEqual(events[0]['code'], ERROR_NOT_SUPPORTED)

        self.send(op='bogus', job='6')
        self.assertEqual(self.recv()['code'], ERROR_NOT_SUPPORTED)
        self.assertEqual(self.recv()['ev'], 'finished')
        self.exit()

    def test_cancel(self):
        ctx = {}
        self.send(op='run', job='7', role='install-packages', ctx=ctx, flags=[], package_ids=[])
        self.assertEqual(self.recv()['ev'], 'allow-cancel')
        self.send(op='cancel', job='other')  # not ours, ignored
        self.send(op='cancel', job='7')
        error = self.recv()
        self.assertEqual(error['code'], ERROR_TRANSACTION_CANCELLED)
        self.assertEqual(self.recv()['ev'], 'finished')
        # a late cancel must not affect the next job
        self.send(op='cancel', job='7')
        self.assertEqual(self.run_job('8', 'get-details', package_ids=[])[-1]['ev'], 'finished')
        self.exit()

    def test_sigterm(self):
        self.send(
            op='run',
            job='9',
            role='remove-packages',
            ctx={},
            flags=[],
            package_ids=[],
            allow_deps=False,
            autoremove=False,
        )
        time.sleep(0.2)
        self.proc.send_signal(signal.SIGTERM)
        error = self.recv()
        self.assertEqual(error['code'], ERROR_TRANSACTION_CANCELLED)
        self.assertEqual(self.recv()['ev'], 'finished')
        # idle: SIGTERM exits cleanly
        self.proc.send_signal(signal.SIGTERM)
        self.assertEqual(self.proc.wait(timeout=10), 0)

    def test_hangup(self):
        self.reader.close()
        self.sock.close()
        self.assertEqual(self.proc.wait(timeout=10), 0)

    def test_garbage(self):
        self.sock.sendall(b'not json\n')
        self.assertEqual(self.proc.wait(timeout=10), 1)


if __name__ == '__main__':
    if sys.argv[1:] == ['--helper']:
        TestBackend().main()
    else:
        unittest.main()
