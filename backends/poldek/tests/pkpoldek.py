#
# Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Shared code of the poldek tests: a test root with its own rpm database and
# poldek configuration over the fixture repositories, and a driver for the
# helper that speaks the protocol the way packagekitd does.

import contextlib
import fnmatch
import http.server
import json
import os
import shutil
import socket
import subprocess
import tempfile
import threading
import unittest
import urllib.parse

FIXTURES = os.environ.get('PK_POLDEK_FIXTURES', '')
HELPER = os.environ.get('PK_POLDEK_HELPER', '')
CTX = {'locale': 'C', 'uid': 0, 'network': False, 'interactive': False, 'proxy': {}}


def require_root():
    # rpm --root installs files and sets their owners
    if os.geteuid() != 0:
        raise unittest.SkipTest('needs root for rpm --root')


def fixture(repo, nvra):
    return os.path.join(FIXTURES, repo, nvra + '.rpm')


class Env:
    """A root directory with an rpm database and a poldek.conf for it."""

    def __init__(self, installed=(), sources=None, options=None):
        self.dir = tempfile.mkdtemp(prefix='pk-poldek-test-')
        self.root = os.path.join(self.dir, 'root')
        self.cache = os.path.join(self.dir, 'cache')
        self.conf = os.path.join(self.dir, 'poldek.conf')
        os.makedirs(self.root)
        os.makedirs(self.cache)
        self.rpm('--initdb')
        if installed:
            self.rpm('-i', '--nodeps', '--noscripts', *[fixture(r, n) for r, n in installed])
        self.write_conf(sources or [('repo-a', {}), ('repo-b', {})], options or {})

    def write_conf(self, sources, options):
        # poldek's defaults otherwise, as on a PLD system
        lines = ['[global]', 'cachedir = ' + self.cache]
        lines += ['%s = %s' % item for item in options.items()]
        for repo, opts in sources:
            opts = {'name': repo, 'type': 'dir', 'path': os.path.join(FIXTURES, repo) + '/', **opts}
            lines += ['', '[source]']
            lines += ['%s = %s' % item for item in opts.items()]
        with open(self.conf, 'w') as f:
            f.write('\n'.join(lines) + '\n')

    def rpm(self, *args):
        subprocess.run(['rpm', '--root', self.root, *args], check=True)

    def installed(self):
        out = subprocess.run(
            ['rpm', '--root', self.root, '-qa', '--qf', '%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}\n'],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        return sorted(out.split())

    def poldek_installed(self):
        """The installed packages as the poldek command line sees them, which shares the cache."""
        out = subprocess.run(
            [
                'poldek',
                '--conf',
                self.conf,
                '--root',
                self.root,
                '-q',
                '--noprogress',
                '--cmd',
                'ls -I',
            ],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        return sorted(out.split())

    def cleanup(self):
        shutil.rmtree(self.dir, ignore_errors=True)


class Helper:
    """pk-backend-poldek with the protocol on a socketpair."""

    def __init__(self, env=None, args=None, sync=False):
        self.sock, child = socket.socketpair()
        fds = [child.fileno()]
        if args is None:
            args = ['--config', env.conf, '--root', env.root]
        args = list(args)
        # where the helper confirms each cancel, see --test-sync-fd
        self.sync = None
        if sync:
            self.sync, sync_child = socket.socketpair()
            self.sync.settimeout(60)
            fds.append(sync_child.fileno())
            args.append('--test-sync-fd=%d' % sync_child.fileno())
        self.proc = subprocess.Popen(
            [HELPER, *args],
            pass_fds=fds,
            stdin=subprocess.DEVNULL,
            env={**os.environ, 'PK_BACKEND_PROTOCOL_FD': str(child.fileno())},
        )
        child.close()
        if sync:
            sync_child.close()
        self.sock.settimeout(60)
        self.reader = self.sock.makefile('r', encoding='utf-8')
        self.jobs = 0

    def send(self, **req):
        self.sock.sendall((json.dumps(req) + '\n').encode())

    def recv(self):
        line = self.reader.readline()
        if not line.endswith('\n'):
            raise AssertionError('helper hung up')
        return json.loads(line)

    def hello(self):
        self.send(op='hello', protocol=1, daemon_version='2.0.0')
        return self.recv()

    def start(self, role, ctx=None, **args):
        """Starts a job and returns its id."""
        self.jobs += 1
        job = str(self.jobs)
        self.send(op='run', job=job, role=role, ctx={**CTX, **(ctx or {})}, **args)
        self.state = {'errors': 0, 'percentage': None, 'remaining': None, 'allow_cancel': None}
        return job

    def check(self, ev):
        """What packagekitd expects of the events of a job, as they come."""
        state = self.state
        name, value = ev['ev'], ev.get('value')
        if name == 'error':
            state['errors'] += 1
            if state['errors'] > 1:
                raise AssertionError('second error: %r' % ev)
        elif name == 'percentage' and value is not None:
            if not 0 <= value <= 100 or (state['percentage'] or 0) > value:
                raise AssertionError('percentage out of range or down: %r' % ev)
            state['percentage'] = value
        elif name == 'item-progress' and not 0 <= value <= 100:
            raise AssertionError('item progress out of range: %r' % ev)
        elif name == 'download-size-remaining':
            state['remaining'] = value
        elif name == 'allow-cancel':
            state['allow_cancel'] = value
        elif name == 'status' and ev['status'] in ('install', 'remove') and state['allow_cancel']:
            raise AssertionError('cancellable while rpm runs: %r' % ev)
        elif name == 'finished' and state['errors'] == 0:
            if state['percentage'] not in (None, 100):
                raise AssertionError('succeeded at %d%%' % state['percentage'])
            if state['remaining'] not in (None, 0):
                raise AssertionError('succeeded with %d bytes to download' % state['remaining'])

    def events(self, job, until=None):
        """Events of @job up to finished, or up to the first one @until accepts."""
        events = []
        while True:
            ev = self.recv()
            if ev.get('job') != job:
                raise AssertionError('event for another job: %r' % ev)
            self.check(ev)
            events.append(ev)
            if ev['ev'] == 'finished' or (until is not None and until(ev)):
                return events

    def run(self, role, ctx=None, **args):
        """Runs a job and returns all its events, the last one is finished."""
        return self.events(self.start(role, ctx=ctx, **args))

    def wait_cancel_ack(self, job):
        line = self.sync.makefile('r').readline()
        if line != 'cancel-ack %s\n' % job:
            raise AssertionError('no cancel-ack for job %s: %r' % (job, line))

    def close(self):
        if self.proc.poll() is None:
            self.send(op='exit')
            try:
                self.proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.reader.close()
        self.sock.close()
        if self.sync is not None:
            self.sync.close()
        return self.proc.returncode


@contextlib.contextmanager
def session(env):
    """A helper started for @env; afterwards it has to exit cleanly, and @env is removed."""
    helper = Helper(env)
    try:
        helper.hello()
        yield helper
    finally:
        code = helper.close()
        env.cleanup()
    if code != 0:
        raise AssertionError('the helper exited with %d' % code)


class Server:
    """Copies of fixture repositories with pndir indexes, served over HTTP.

    A file whose name matches @gate is sent up to its middle, then the server
    waits for open_gate(); at_gate tells when it got there.
    """

    def __init__(self, repos, gate=None):
        self.dir = tempfile.mkdtemp(prefix='pk-poldek-http-')
        for repo in repos:
            shutil.copytree(os.path.join(FIXTURES, repo), os.path.join(self.dir, repo))
            self.make_index(repo)
        self.requests = []
        self.urls = []
        self.at_gate = threading.Event()
        self._gate = threading.Event()
        server = self

        class Handler(http.server.SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=server.dir, **kwargs)

            def log_message(self, *args):
                pass

            def translate_path(self, path):
                # a proxy is asked for the whole URL
                return super().translate_path(urllib.parse.urlsplit(path).path)

            def copyfile(self, src, dst):
                server.requests.append(os.path.basename(self.path))
                # as a proxy, it gets the whole URL
                server.urls.append(self.path)
                if gate is None or not fnmatch.fnmatch(os.path.basename(self.path), gate):
                    return super().copyfile(src, dst)
                data = src.read()
                # rounded up, so the progress there is 50 exactly or a bit more
                half = (len(data) + 1) // 2
                dst.write(data[:half])
                dst.flush()
                server.at_gate.set()
                server._gate.wait(60)
                dst.write(data[half:])

        self.httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.url = 'http://127.0.0.1:%d' % self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def make_index(self, repo):
        path = os.path.join(self.dir, repo)
        subprocess.run(
            ['poldek', '--noconf', '--cachedir', os.path.join(self.dir, '.idxcache')]
            + ['-s', path + '/', '--mkidx'],
            check=True,
            capture_output=True,
        )

    def source(self, repo):
        """The options of a [source] for @repo."""
        return {'type': 'pndir', 'path': '%s/%s/' % (self.url, repo)}

    def open_gate(self):
        self._gate.set()

    def close(self):
        self._gate.set()
        self.httpd.shutdown()
        self.httpd.server_close()
        shutil.rmtree(self.dir, ignore_errors=True)


def items(events, ev):
    """All items of the result events ev, in order."""
    return [item for e in events if e['ev'] == ev for item in e['items']]


def errors(events):
    return [(e['code'], e.get('details')) for e in events if e['ev'] == 'error']


def package_ids(events):
    return [(item['package_id'], item['info']) for item in items(events, 'packages')]
