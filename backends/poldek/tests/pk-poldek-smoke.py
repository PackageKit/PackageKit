#!/usr/bin/env python3
#
# Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The poldek backend end to end: a real packagekitd from the build tree with
# DefaultBackend=poldek, driven with pkgcli. The helper manages the system it
# runs on, as it does in production, so this replaces /etc/poldek/poldek.conf
# with one for the fixture repositories and installs packages into /: run it
# in a throwaway container only, as root, after the tests built the fixtures.
#
# usage: pk-poldek-smoke.py BUILDDIR

import glob
import os
import shutil
import subprocess
import sys
import tempfile
import time

SOURCE_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
sys.path.insert(0, os.path.join(SOURCE_ROOT, 'tests', 'runner'))

from utils import (  # noqa: E402 - found through the path set up above
    _name_has_owner,
    start_polkitd_if_needed,
    start_system_bus_if_needed,
    terminate,
)

POLDEK_CONF = '/etc/poldek/poldek.conf'


def install_policy(build):
    """The D-Bus and polkit policy, where the bus and polkitd look for them."""
    for src, dst in (
        (
            os.path.join(build, 'data', 'org.freedesktop.packagekit1.conf'),
            '/usr/share/dbus-1/system.d/org.freedesktop.packagekit1.conf',
        ),
        (
            os.path.join(build, 'data', 'policy', 'org.freedesktop.packagekit.policy'),
            '/usr/share/polkit-1/actions/org.freedesktop.packagekit.policy',
        ),
        (
            os.path.join(SOURCE_ROOT, 'data', 'policy', 'org.freedesktop.packagekit.rules'),
            '/usr/share/polkit-1/rules.d/org.freedesktop.packagekit.rules',
        ),
    ):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(src, dst)


def write_poldek_conf(fixtures):
    os.makedirs('/var/cache/pk-poldek-smoke', exist_ok=True)
    with open(POLDEK_CONF, 'w') as f:
        f.write('[global]\ncachedir = /var/cache/pk-poldek-smoke\nuse sudo = no\n')
        f.write('unique package names = no\n')
        for repo in ('repo-a', 'repo-b'):
            f.write('\n[source]\nname = %s\ntype = dir\npath = %s/%s/\n' % (repo, fixtures, repo))


def write_pk_conf(build, tmp):
    """The build tree's test configuration and root, with the poldek backend."""
    root = os.path.join(tmp, 'root')
    shutil.copytree(os.path.join(build, 'tests', '_test-root'), root, symlinks=True)
    # the build directory of the backend has its manifest and helper
    backends = glob.glob(os.path.join(root, '**', 'packagekit', 'backends'), recursive=True)
    os.symlink(os.path.join(build, 'backends', 'poldek'), os.path.join(backends[0], 'poldek'))
    conf = os.path.join(tmp, 'PackageKit.conf')
    with open(os.path.join(build, 'tests', 'PackageKit.conf')) as f:
        lines = [line for line in f if not line.startswith(('RootDir=', 'DefaultBackend='))]
    with open(conf, 'w') as f:
        for line in lines:
            f.write(line)
            if line.strip() == '[Daemon]':
                f.write('RootDir=%s\nDefaultBackend=poldek\n' % root)
    return conf


class Smoke:
    def __init__(self, build, log):
        self.build = build
        self.log = log
        self.env = dict(os.environ, LD_LIBRARY_PATH=os.path.join(build, 'lib'), LC_ALL='C.UTF-8')
        self.failures = 0

    def pkgcli(self, *args, ok=True):
        cmd = [os.path.join(self.build, 'client', 'pkgcli'), *args]
        res = subprocess.run(cmd, env=self.env, capture_output=True, text=True, timeout=300)
        print('$ pkgcli %s -> %d\n%s%s' % (' '.join(args), res.returncode, res.stdout, res.stderr))
        if ok and res.returncode != 0:
            self.fail('pkgcli %s failed' % ' '.join(args))
        return res

    def installed(self, name):
        return subprocess.run(['rpm', '-q', name], capture_output=True, text=True).stdout.strip()

    def check(self, condition, what):
        print('%s: %s' % ('ok' if condition else 'FAILED', what))
        if not condition:
            self.failures += 1

    def fail(self, what):
        self.check(False, what)

    def run(self, fixtures):
        self.check('poldek' in self.pkgcli('backend').stdout, 'the daemon runs the poldek backend')
        self.pkgcli('refresh')
        self.check('foo' in self.pkgcli('search', 'name', 'foo').stdout, 'search finds foo')
        self.check('repo-b' in self.pkgcli('repo-list').stdout, 'repo-list shows repo-b')
        # the fixtures are not signed: only-trusted, the default, refuses them, and
        # pkgcli --yes says yes to installing them untrusted
        self.pkgcli('--yes', 'install', 'guiapp')
        self.check(self.installed('guiapp') == 'guiapp-1.0-1.noarch', 'guiapp is installed')
        with open(self.log) as f:
            self.check('missing-gpg-signature' in f.read(), 'only-trusted was tried first')
        subprocess.run(
            [
                'rpm',
                '-i',
                '--nodeps',
                '--noscripts',
                os.path.join(fixtures, 'repo-a', 'baz-1.0-1.noarch.rpm'),
            ],
            check=True,
        )
        self.check('baz' in self.pkgcli('list-updates').stdout, 'baz-1.1 is an update')
        self.pkgcli('--yes', 'update')
        self.check(self.installed('baz') == 'baz-1.1-1.noarch', 'baz is updated')
        self.pkgcli('--yes', 'remove', 'guiapp')
        self.check(not self.installed('guiapp').startswith('guiapp-'), 'guiapp is removed')


def main():
    build = os.path.abspath(sys.argv[1])
    if os.geteuid() != 0:
        print('must run as root, in a throwaway container')
        return 1
    subprocess.run(['ninja', '-C', build, 'backends/poldek/tests/fixtures.stamp'], check=True)
    install_policy(build)
    fixtures = os.path.join(build, 'backends', 'poldek', 'tests', 'fixtures')
    write_poldek_conf(fixtures)
    tmp = tempfile.mkdtemp(prefix='pk-poldek-smoke-')
    conf = write_pk_conf(build, tmp)
    log = open(os.path.join(tmp, 'daemon.log'), 'w')
    bus, bus_tmp = start_system_bus_if_needed(log)
    polkitd = start_polkitd_if_needed(log)
    daemon = subprocess.Popen(
        [os.path.join(build, 'src', 'packagekitd'), '--verbose', '--disable-timer']
        + ['--keep-environment', '--config=' + conf],
        stdout=log,
        stderr=subprocess.STDOUT,
    )
    smoke = Smoke(build, os.path.join(tmp, 'daemon.log'))
    try:
        for _ in range(80):
            if _name_has_owner('org.freedesktop.packagekit1'):
                break
            time.sleep(0.25)
        else:
            smoke.fail('packagekitd claims its bus name')
        if smoke.failures == 0:
            smoke.run(fixtures)
    finally:
        terminate(daemon)
        terminate(polkitd)
        terminate(bus)
        log.close()
        if smoke.failures:
            with open(os.path.join(tmp, 'daemon.log')) as f:
                print('--- daemon output ---\n' + f.read())
        shutil.rmtree(tmp, ignore_errors=True)
        if bus_tmp:
            shutil.rmtree(bus_tmp, ignore_errors=True)
    print('%d failed' % smoke.failures)
    return 1 if smoke.failures else 0


if __name__ == '__main__':
    sys.exit(main())
