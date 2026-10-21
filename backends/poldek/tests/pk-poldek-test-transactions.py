#!/usr/bin/env python3
#
# Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Transactions of pk-backend-poldek: real installs and removals with rpm in
# a test root, from the fixture repositories.

import fcntl
import glob
import os
import shutil
import signal
import unittest

from pkpoldek import FIXTURES, Env, Helper, Server, errors, fixture, package_ids, require_root

INSTALLED = [
    ('repo-a', 'foo-1.0-1.noarch'),
    ('repo-a', 'libbar-1.0-1.x86_64'),
    ('repo-a', 'baz-1.0-1.noarch'),
]


def A(nvr, repo='repo-a'):
    name, version, release_arch = nvr.rsplit('-', 2)
    release, arch = release_arch.rsplit('.', 1)
    return '%s;%s-%s;%s;%s;' % (name, version, release, arch, repo)


def I(nvr):  # noqa: E743 - reads like the info it stands for
    name, version, release_arch = nvr.rsplit('-', 2)
    release, arch = release_arch.rsplit('.', 1)
    return '%s;%s-%s;%s;;installed' % (name, version, release, arch)


def package_status(events):
    return [(e['package_id'], e['info']) for e in events if e['ev'] == 'package-status']


def item_statuses(events, package_id):
    return {
        e['status'] for e in events if e['ev'] == 'item-progress' and e['package_id'] == package_id
    }


def restarts(events):
    return [(e['package_id'], e['restart']) for e in events if e['ev'] == 'require-restart']


def setUpModule():
    require_root()


class TransactionTest(unittest.TestCase):
    installed = INSTALLED
    sources = None
    sync = False

    def setUp(self):
        self.env = Env(self.installed, self.sources)
        self.helper = Helper(self.env, sync=self.sync)
        self.helper.hello()

    def tearDown(self):
        self.assertEqual(self.helper.close(), 0)
        self.env.cleanup()

    def run_job(self, role, **args):
        return self.helper.run(role, **args)

    def install(self, *ids, flags=()):
        return self.run_job('install-packages', flags=list(flags), package_ids=list(ids))

    def update(self, *ids, flags=()):
        return self.run_job('update-packages', flags=list(flags), package_ids=list(ids))

    def remove(self, *ids, allow_deps=False, autoremove=False, flags=()):
        return self.run_job(
            'remove-packages',
            flags=list(flags),
            package_ids=list(ids),
            allow_deps=allow_deps,
            autoremove=autoremove,
        )

    def assertOk(self, events):
        self.assertEqual(errors(events), [])
        self.assertEqual(events[-1]['ev'], 'finished')

    def assertError(self, events, code):
        self.assertEqual([e[0] for e in errors(events)], [code], errors(events))

    def content(self, path):
        with open(os.path.join(self.env.root, path.lstrip('/'))) as f:
            return f.read().strip()


class InstallTest(TransactionTest):
    def test_install_with_dependency(self):
        before = self.env.installed()
        events = self.install(A('dapp-2.0-1.noarch'))
        self.assertOk(events)
        self.assertEqual(
            self.env.installed(), sorted(before + ['dapp-2.0-1.noarch', 'dlib-2.0-1.noarch'])
        )
        progress = [e for e in events if e['ev'] == 'item-progress' and e['value'] == 100]
        self.assertEqual(
            sorted(e['package_id'] for e in progress),
            [A('dapp-2.0-1.noarch'), A('dlib-2.0-1.noarch')],
        )
        self.assertEqual([e['value'] for e in events if e['ev'] == 'percentage'][-1], 100)
        # rpm runs without being cancellable
        allow = [e['value'] for e in events if e['ev'] == 'allow-cancel']
        self.assertEqual(allow[-1], False)
        # the same helper sees what it installed
        installed = package_ids(self.run_job('resolve', filters=['installed'], values=['dlib']))
        self.assertEqual(installed, [(I('dlib-2.0-1.noarch'), 'installed')])

    def test_independent_packages(self):
        # one transaction, though poldek's default installs independent parts one by one
        self.assertOk(self.install(A('dlib-2.0-1.noarch'), A('guiapp-1.0-1.noarch')))
        self.assertIn('dlib-2.0-1.noarch', self.env.installed())
        self.assertIn('guiapp-1.0-1.noarch', self.env.installed())

    def test_progress_of_package_with_epoch(self):
        # rpm names it epo-2:1.0-1
        epo = A('epo-2:1.0-1.noarch')
        events = self.install(epo)
        self.assertOk(events)
        self.assertIn(
            {
                'ev': 'package-status',
                'job': events[0]['job'],
                'package_id': epo,
                'info': 'installing',
            },
            events,
        )
        done = [e for e in events if e['ev'] == 'item-progress' and e['status'] == 'install']
        self.assertEqual([e['value'] for e in done if e['package_id'] == epo][-1], 100)

    def test_unresolvable(self):
        self.assertError(self.install(A('broken-1.0-1.noarch')), 'dep-resolution-failed')
        self.assertNotIn('broken-1.0-1.noarch', self.env.installed())

    def test_conflict(self):
        before = self.env.installed()
        self.assertError(self.install(A('qux-1.0-1.noarch')), 'package-conflicts')
        self.assertEqual(self.env.installed(), before)

    def test_simulate(self):
        before = self.env.installed()
        events = self.install(A('foo-1.1-1.noarch'), A('dapp-2.0-1.noarch'), flags=['simulate'])
        self.assertOk(events)
        self.assertEqual(
            package_ids(events),
            [
                (A('dapp-2.0-1.noarch'), 'install'),
                (A('dlib-2.0-1.noarch'), 'install'),
                (A('foo-1.1-1.noarch'), 'update'),
            ],
        )
        self.assertEqual(self.env.installed(), before)

    def test_update(self):
        events = self.update(A('foo-1.1-1.noarch'))
        self.assertOk(events)
        self.assertIn('foo-1.1-1.noarch', self.env.installed())
        self.assertNotIn('foo-1.0-1.noarch', self.env.installed())
        self.assertEqual(self.content('/usr/bin/foo'), 'foo 1.1 from repo-a')
        # an update, not an install, as the plan has it
        self.assertEqual(
            package_status(events),
            [(A('foo-1.1-1.noarch'), 'updating'), (I('foo-1.0-1.noarch'), 'cleanup')],
        )
        self.assertEqual(item_statuses(events, A('foo-1.1-1.noarch')), {'update'})
        statuses = [e['status'] for e in events if e['ev'] == 'status']
        self.assertIn('update', statuses)
        self.assertNotIn('install', statuses)

    def test_losing_origin(self):
        before = self.env.installed()
        self.assertError(self.update(A('foo-1.1-1.noarch', 'repo-b')), 'package-not-found')
        self.assertEqual(self.env.installed(), before)

    def test_only_download(self):
        before = self.env.installed()
        self.assertOk(self.install(A('dapp-2.0-1.noarch'), flags=['only-download']))
        self.assertEqual(self.env.installed(), before)

    def test_rpm_locked(self):
        lock = os.path.join(self.env.root, 'var/lib/rpm/.rpm.lock')
        with open(lock, 'a') as f:
            fcntl.lockf(f, fcntl.LOCK_EX)
            self.assertError(self.install(A('dlib-2.0-1.noarch')), 'cannot-get-lock')
        self.assertOk(self.install(A('dlib-2.0-1.noarch')))

    def test_late_cancel_ignored(self):
        job = self.helper.start('install-packages', flags=[], package_ids=[A('dlib-2.0-1.noarch')])
        self.assertOk(self.helper.events(job))
        self.helper.send(op='cancel', job=job)
        self.assertOk(self.install(A('dlib-1.0-1.noarch'), flags=['allow-downgrade']))

    def test_download_packages(self):
        directory = os.path.join(self.env.dir, 'download')
        os.makedirs(directory)
        events = self.run_job(
            'download-packages', package_ids=[A('dlib-2.0-1.noarch')], directory=directory
        )
        self.assertOk(events)
        files = [(e['package_id'], e['files']) for e in events if e['ev'] == 'files']
        path = os.path.join(directory, 'dlib-2.0-1.noarch.rpm')
        self.assertEqual(files, [(A('dlib-2.0-1.noarch'), {path: 'file'})])
        self.assertTrue(os.path.exists(path))


class SameSessionTest(TransactionTest):
    """What a transaction of the helper installed, asked about in the same helper."""

    sources = [('repo-u', {})]

    def test_file_dependency_of_new_packages(self):
        # fdep needs /usr/bin/fprov, from fprov
        self.assertOk(self.install('fdep;1.0-1;noarch;repo-u;'))
        events = self.run_job(
            'get-dependencies',
            filters=['installed'],
            package_ids=[I('fdep-1.0-1.noarch')],
            recursive=False,
        )
        self.assertEqual(package_ids(events), [(I('fprov-1.0-1.noarch'), 'installed')])
        events = self.run_job(
            'get-dependents',
            filters=['installed'],
            package_ids=[I('fprov-1.0-1.noarch')],
            recursive=False,
        )
        self.assertEqual(package_ids(events), [(I('fdep-1.0-1.noarch'), 'installed')])


class PriorityTest(TransactionTest):
    sources = [('repo-a', {'pri': '20'}), ('repo-b', {'pri': '10'})]

    def test_winner_installed(self):
        self.assertError(self.update(A('foo-1.1-1.noarch')), 'package-not-found')
        self.assertOk(self.update(A('foo-1.1-1.noarch', 'repo-b')))
        self.assertEqual(self.content('/usr/bin/foo'), 'foo 1.1 from repo-b')


class RemoveTest(TransactionTest):
    def test_remove(self):
        self.assertOk(self.remove(I('baz-1.0-1.noarch')))
        self.assertNotIn('baz-1.0-1.noarch', self.env.installed())

    def test_remove_needs_deps(self):
        before = self.env.installed()
        # foo needs libbar
        self.assertError(self.remove(I('libbar-1.0-1.x86_64')), 'dep-resolution-failed')
        self.assertEqual(self.env.installed(), before)
        self.assertOk(self.remove(I('libbar-1.0-1.x86_64'), allow_deps=True))
        self.assertEqual(self.env.installed(), ['baz-1.0-1.noarch'])

    def test_repeated_id_needs_deps(self):
        before = self.env.installed()
        libbar = I('libbar-1.0-1.x86_64')
        self.assertError(self.remove(libbar, libbar), 'dep-resolution-failed')
        self.assertEqual(self.env.installed(), before)

    def test_autoremove(self):
        before = self.env.installed()
        # foo needs libbar
        self.assertError(
            self.remove(I('libbar-1.0-1.x86_64'), autoremove=True), 'dep-resolution-failed'
        )
        self.assertEqual(self.env.installed(), before)
        # rpm does not record that libbar came in only for foo, it may have been
        # installed on its own: it stays
        events = self.remove(I('foo-1.0-1.noarch'), autoremove=True, flags=['simulate'])
        self.assertEqual(package_ids(events), [(I('foo-1.0-1.noarch'), 'remove')])
        self.assertOk(self.remove(I('foo-1.0-1.noarch'), autoremove=True))
        self.assertEqual(self.env.installed(), ['baz-1.0-1.noarch', 'libbar-1.0-1.x86_64'])

    def test_remove_simulate(self):
        before = self.env.installed()
        events = self.remove(I('libbar-1.0-1.x86_64'), allow_deps=True, flags=['simulate'])
        self.assertOk(events)
        self.assertEqual(
            package_ids(events),
            [(I('foo-1.0-1.noarch'), 'remove'), (I('libbar-1.0-1.x86_64'), 'remove')],
        )
        self.assertEqual(self.env.installed(), before)

    def test_remove_not_installed(self):
        self.assertError(self.remove(A('dlib-2.0-1.noarch')), 'package-not-installed')


class RestartTest(TransactionTest):
    installed = [('repo-a', 'glibc-1.0-1.noarch'), ('repo-a', 'baz-1.0-1.noarch')]

    def test_glibc_needs_reboot(self):
        glibc = A('glibc-1.1-1.noarch')
        # only what rpm changed: not a simulation, not a download
        self.assertEqual(restarts(self.update(glibc, flags=['simulate'])), [])
        self.assertEqual(restarts(self.update(glibc, flags=['only-download'])), [])
        events = self.update(glibc, A('baz-1.1-1.noarch'))
        self.assertOk(events)
        self.assertEqual(restarts(events), [(glibc, 'system')])


class PartialRestartTest(TransactionTest):
    installed = [('repo-a', 'glibc-1.0-1.noarch')]
    sources = [('repo-a', {}), ('repo-u', {})]

    def test_restart_of_what_rpm_changed(self):
        # zfail pulls in glibc-1.1, which rpm installs before zfail's %pre fails
        events = self.install('zfail;1.0-1;noarch;repo-u;')
        self.assertError(events, 'transaction-error')
        self.assertEqual(self.env.installed(), ['glibc-1.1-1.noarch'])
        self.assertEqual(restarts(events), [(A('glibc-1.1-1.noarch'), 'system')])
        # before the error, after which the daemon takes nothing
        kinds = [e['ev'] for e in events]
        self.assertLess(kinds.index('require-restart'), kinds.index('error'))


class KernelRestartTest(TransactionTest):
    installed = [('repo-u', 'kernel-1.0-1.noarch'), ('repo-u', 'kernel-headers-1.0-1.noarch')]
    sources = [('repo-u', {})]

    def test_kernel_image_needs_reboot(self):
        kernel = 'kernel;1.1-1;noarch;repo-u;'
        events = self.update(kernel, 'kernel-headers;1.1-1;noarch;repo-u;')
        self.assertOk(events)
        self.assertEqual(restarts(events), [(kernel, 'system')])


class ReinstallTest(TransactionTest):
    def test_already_installed(self):
        self.assertError(self.install(A('foo-1.0-1.noarch')), 'all-packages-already-installed')
        self.assertError(
            self.install(A('foo-1.0-1.noarch'), A('dlib-2.0-1.noarch')), 'package-already-installed'
        )

    def test_allow_reinstall(self):
        os.unlink(os.path.join(self.env.root, 'usr/bin/foo'))
        events = self.install(A('foo-1.0-1.noarch'), flags=['allow-reinstall'])
        self.assertOk(events)
        self.assertIn((A('foo-1.0-1.noarch'), 'reinstalling'), package_status(events))
        self.assertEqual(self.content('/usr/bin/foo'), 'foo 1.0 from repo-a')

    def test_just_reinstall(self):
        self.assertError(
            self.install(A('foo-1.1-1.noarch'), flags=['just-reinstall']), 'package-not-installed'
        )
        os.unlink(os.path.join(self.env.root, 'usr/bin/foo'))
        # the installed package id stands for the same version in a repository
        self.assertOk(self.install(I('foo-1.0-1.noarch'), flags=['just-reinstall']))
        self.assertEqual(self.content('/usr/bin/foo'), 'foo 1.0 from repo-a')

    def test_reinstall_with_others(self):
        self.assertError(
            self.install(A('foo-1.0-1.noarch'), A('dlib-2.0-1.noarch'), flags=['allow-reinstall']),
            'not-supported',
        )


class DowngradeTest(TransactionTest):
    installed = [
        ('repo-a', 'dapp-2.0-1.noarch'),
        ('repo-a', 'dlib-2.0-1.noarch'),
        ('repo-a', 'baz-1.1-1.noarch'),
        ('repo-a', 'foo-1.0-1.noarch'),
        ('repo-a', 'libbar-1.0-1.x86_64'),
    ]

    def test_downgrade(self):
        # dapp-1.0 needs dlib = 1.0, which is downgraded with it
        self.assertError(self.install(A('dapp-1.0-1.noarch')), 'package-already-installed')
        events = self.install(A('dapp-1.0-1.noarch'), flags=['allow-downgrade'])
        self.assertOk(events)
        self.assertIn((A('dapp-1.0-1.noarch'), 'downgrading'), package_status(events))
        self.assertIn((A('dlib-1.0-1.noarch'), 'downgrading'), package_status(events))
        installed = self.env.installed()
        self.assertIn('dapp-1.0-1.noarch', installed)
        self.assertIn('dlib-1.0-1.noarch', installed)

    def test_mixed(self):
        before = self.env.installed()
        events = self.install(
            A('foo-1.1-1.noarch'), A('baz-1.0-1.noarch'), flags=['allow-downgrade']
        )
        self.assertError(events, 'not-supported')
        self.assertEqual(self.env.installed(), before)


class UpgradeWithDowngradeAllowedTest(TransactionTest):
    installed = [('repo-a', 'dapp-1.0-1.noarch'), ('repo-a', 'dlib-1.0-1.noarch')]

    def test_plain_update(self):
        # allowing a downgrade does not make an update one
        events = self.update(A('dapp-2.0-1.noarch'), flags=['allow-downgrade'])
        self.assertOk(events)
        self.assertEqual(self.env.installed(), ['dapp-2.0-1.noarch', 'dlib-2.0-1.noarch'])


class TrustedTest(TransactionTest):
    """only-trusted against the test root's keyring, with packages in a local directory."""

    sources = [('repo-a', {}), ('repo-s', {})]
    flags = ['only-trusted']

    def S(self, name):
        return A('%s-1.0-1.noarch' % name, 'repo-s')

    def import_key(self, name='fixture'):
        self.env.rpm('--import', os.path.join(FIXTURES, 'keys', name + '.asc'))

    def check(self, name, code, flags=None):
        before = self.env.installed()
        events = self.install(self.S(name), flags=self.flags if flags is None else flags)
        if code is None:
            self.assertOk(events)
        else:
            self.assertError(events, code)
            self.assertEqual(self.env.installed(), before)
        return events

    def test_key_not_in_root(self):
        # the key is nowhere but in the fixtures: the root's keyring decides
        self.check('strust', 'gpg-failure')
        self.import_key()
        self.check('strust', None)
        self.assertIn('strust-1.0-1.noarch', self.env.installed())

    def test_unsigned(self):
        self.import_key()
        self.check('sunsigned', 'missing-gpg-signature')
        # a dependency counts as much as the package asked for
        self.check('sbar', 'missing-gpg-signature')

    def test_other_key(self):
        self.import_key()
        self.check('sother', 'gpg-failure')

    def test_corrupt(self):
        self.import_key()
        self.check('scorrupt', 'bad-gpg-signature')

    def test_untrusted_allowed(self):
        self.check('sunsigned', None, flags=[])
        self.assertIn('sunsigned-1.0-1.noarch', self.env.installed())


class TrustedDownloadTest(TrustedTest):
    """The same checks for only-trusted downloads, which install nothing."""

    flags = ['only-trusted', 'only-download']

    def test_key_not_in_root(self):
        self.check('strust', 'gpg-failure')
        self.import_key()
        self.check('strust', None)
        self.assertNotIn('strust-1.0-1.noarch', self.env.installed())

    def test_untrusted_allowed(self):
        self.check('sunsigned', None, flags=['only-download'])
        self.assertNotIn('sunsigned-1.0-1.noarch', self.env.installed())


class RemoteTest(TransactionTest):
    """The repositories over HTTP, with pndir indexes and poldek's download cache."""

    repos = ['repo-a']
    gate = None
    source_options = {}

    def setUp(self):
        self.server = Server(self.repos, self.gate)
        self.sources = [
            (repo, {**self.server.source(repo), **self.source_options}) for repo in self.repos
        ]
        try:
            super().setUp()
        except Exception:
            self.server.close()
            raise

    def tearDown(self):
        try:
            super().tearDown()
        finally:
            self.server.close()

    def cached(self, nvra):
        found = glob.glob(os.path.join(self.env.cache, '*', nvra + '.rpm'))
        return found[0] if found else None


class DownloadTest(RemoteTest):
    def test_download_into_cache(self):
        # the daemon gives no directory when the packages are to stay in the backend's cache
        events = self.run_job('download-packages', package_ids=[A('dlib-2.0-1.noarch')])
        self.assertOk(events)
        files = [(e['package_id'], e['files']) for e in events if e['ev'] == 'files']
        path = self.cached('dlib-2.0-1.noarch')
        self.assertIsNotNone(path)
        self.assertEqual(files, [(A('dlib-2.0-1.noarch'), {path: 'file'})])

    def test_download_progress(self):
        events = self.install(A('dapp-2.0-1.noarch'))
        self.assertOk(events)
        self.assertIn('dapp-2.0-1.noarch', self.env.installed())
        statuses = [e['status'] for e in events if e['ev'] == 'status']
        self.assertLess(statuses.index('download'), statuses.index('install'))
        for pkg in (A('dapp-2.0-1.noarch'), A('dlib-2.0-1.noarch')):
            values = [
                e['value']
                for e in events
                if e['ev'] == 'item-progress'
                and e['package_id'] == pkg
                and e['status'] == 'download'
            ]
            self.assertEqual((values[0], values[-1]), (0, 100), pkg)
            self.assertEqual(values, sorted(values), pkg)
            self.assertIn(
                {
                    'ev': 'package-status',
                    'job': events[0]['job'],
                    'package_id': pkg,
                    'info': 'downloading',
                },
                events,
            )
        remaining = [e['value'] for e in events if e['ev'] == 'download-size-remaining']
        self.assertGreater(remaining[0], 0)
        self.assertEqual(remaining[-1], 0)

    def test_cached_not_fetched_again(self):
        self.assertOk(self.install(A('dapp-2.0-1.noarch'), flags=['only-download']))
        self.assertTrue(self.cached('dapp-2.0-1.noarch'))
        fetched = list(self.server.requests)
        self.assertIn('dapp-2.0-1.noarch.rpm', fetched)
        self.assertOk(self.install(A('dapp-2.0-1.noarch')))
        self.assertEqual(
            [r for r in self.server.requests if r.endswith('.rpm')],
            [r for r in fetched if r.endswith('.rpm')],
        )
        self.assertIn('dapp-2.0-1.noarch', self.env.installed())


class ProxyTest(RemoteTest):
    """The repository on a host that is not there, reached through the client's proxy."""

    def setUp(self):
        self.server = Server(self.repos, self.gate)
        self.sources = [('repo-a', {'type': 'pndir', 'path': 'http://repo.invalid/repo-a/'})]
        TransactionTest.setUp(self)

    def test_install_through_proxy(self):
        ctx = {'proxy': {'http': self.server.url.split('//', 1)[1]}}
        events = self.helper.run(
            'install-packages', ctx=ctx, flags=[], package_ids=[A('dlib-2.0-1.noarch')]
        )
        self.assertOk(events)
        self.assertIn('dlib-2.0-1.noarch', self.env.installed())
        self.assertIn('http://repo.invalid/repo-a/dlib-2.0-1.noarch.rpm', self.server.urls)


class RemoteRefreshTest(RemoteTest):
    def test_refresh_index(self):
        self.assertEqual(package_ids(self.run_job('resolve', filters=[], values=['bonly'])), [])
        repo = os.path.join(self.server.dir, 'repo-a')
        shutil.copy(fixture('repo-b', 'bonly-1.0-1.noarch'), repo)
        self.server.make_index('repo-a')
        self.assertOk(self.run_job('refresh-cache', force=False))
        self.assertEqual(
            package_ids(self.run_job('resolve', filters=[], values=['bonly'])),
            [('bonly;1.0-1;noarch;repo-a;', 'available')],
        )
        self.assertOk(self.run_job('refresh-cache', force=True))


class NoAutoUpTest(RemoteTest):
    source_options = {'autoup': 'no'}

    def test_refresh_skips_it(self):
        # as poldek --up does
        del self.server.requests[:]
        self.assertOk(self.run_job('refresh-cache', force=False))
        self.assertEqual(self.server.requests, [])


class TrustedRemoteTest(RemoteTest):
    repos = ['repo-s']

    def S(self, name):
        return A('%s-1.0-1.noarch' % name, 'repo-s')

    def import_key(self):
        self.env.rpm('--import', os.path.join(FIXTURES, 'keys', 'fixture.asc'))

    def test_cold_cache(self):
        self.import_key()
        self.assertError(
            self.install(self.S('sunsigned'), flags=['only-trusted']), 'missing-gpg-signature'
        )
        self.assertOk(self.install(self.S('strust'), flags=['only-trusted']))
        self.assertIn('strust-1.0-1.noarch', self.env.installed())

    def test_warm_cache(self):
        self.import_key()
        self.assertOk(self.install(self.S('sunsigned'), flags=['only-download']))
        fetched = len(self.server.requests)
        # the file already in the cache is checked, not fetched again
        for flags in (['only-trusted'], ['only-trusted', 'only-download']):
            self.assertError(
                self.install(self.S('sunsigned'), flags=flags), 'missing-gpg-signature'
            )
        self.assertEqual(len(self.server.requests), fetched)

    def test_warm_cache_tampered(self):
        # a file in the cache that no longer matches its digests is downloaded again
        self.import_key()
        self.assertOk(self.install(self.S('sblob'), flags=['only-download']))
        cached = self.cached('sblob-1.0-1.noarch')
        for flags in (['only-download'], ['only-trusted', 'only-download'], ['only-trusted']):
            # a byte of the payload, so the file is still a package, with a wrong digest
            with open(cached, 'r+b') as f:
                f.seek(-100, os.SEEK_END)
                byte = f.read(1)
                f.seek(-100, os.SEEK_END)
                f.write(bytes([byte[0] ^ 0xFF]))
            fetched = len(self.server.requests)
            events = self.install(self.S('sblob'), flags=flags)
            self.assertOk(events)
            self.assertGreater(len(self.server.requests), fetched, flags)
            remaining = [e['value'] for e in events if e['ev'] == 'download-size-remaining']
            self.assertGreater(max(remaining), 0, flags)
            # an install takes the file out of the cache afterwards
            if 'only-download' in flags:
                self.env.rpm('-K', '--nosignature', cached)
        self.assertIn('sblob-1.0-1.noarch', self.env.installed())


class CancelTest(RemoteTest):
    """Cancel while the server holds back the second half of big."""

    gate = 'big-*.rpm'
    sync = True
    BIG = A('big-1.0-1.noarch')

    def start_and_reach_gate(self):
        job = self.helper.start('install-packages', flags=[], package_ids=[self.BIG])
        # 50 exactly: the first half has arrived, and nothing more can
        events = self.helper.events(
            job,
            until=lambda e: e['ev'] == 'item-progress'
            and e['package_id'] == self.BIG
            and e['value'] == 50,
        )
        self.assertNotEqual(events[-1]['ev'], 'finished', events)
        return job, events

    def check_cancelled(self, events):
        self.assertError(events, 'transaction-cancelled')
        values = [
            e['value'] for e in events if e['ev'] == 'item-progress' and e['package_id'] == self.BIG
        ]
        self.assertEqual(values[-1], 50)
        self.assertNotIn('big-1.0-1.noarch', self.env.installed())

    def test_cancel(self):
        job, events = self.start_and_reach_gate()
        self.helper.send(op='cancel', job=job)
        self.helper.wait_cancel_ack(job)
        self.server.open_gate()
        events += self.helper.events(job)
        self.check_cancelled(events)
        # and the helper goes on
        self.assertOk(self.install(A('dlib-2.0-1.noarch')))

    def test_cancel_keeps_installed_cache(self):
        job, events = self.start_and_reach_gate()
        self.helper.send(op='cancel', job=job)
        self.helper.wait_cancel_ack(job)
        self.server.open_gate()
        events += self.helper.events(job)
        self.check_cancelled(events)
        # the helper reloads the changed rpm database on exit, poclidek saves it to its cache
        self.env.rpm('-i', '--nodeps', '--noscripts', fixture('repo-a', 'dlib-2.0-1.noarch'))
        self.assertEqual(self.helper.close(), 0)
        self.assertEqual(self.env.poldek_installed(), self.env.installed())

    def test_sigterm(self):
        job, events = self.start_and_reach_gate()
        self.helper.proc.send_signal(signal.SIGTERM)
        self.helper.wait_cancel_ack('sigterm')
        self.server.open_gate()
        events += self.helper.events(job)
        self.check_cancelled(events)
        self.assertEqual(self.helper.proc.wait(timeout=30), 0)


class RefreshTest(TransactionTest):
    sources = [('repo-a', {})]

    def test_refresh_sees_new_packages(self):
        repo = os.path.join(self.env.dir, 'repo-c')
        os.makedirs(repo)
        with open(self.env.conf, 'a') as f:
            f.write('\n[source]\nname = repo-c\ntype = dir\npath = %s/\n' % repo)
        self.assertEqual(package_ids(self.run_job('resolve', filters=[], values=['bonly'])), [])
        shutil.copy(fixture('repo-b', 'bonly-1.0-1.noarch'), repo)
        self.assertOk(self.run_job('refresh-cache', force=False))
        self.assertEqual(
            package_ids(self.run_job('resolve', filters=[], values=['bonly'])),
            [('bonly;1.0-1;noarch;repo-c;', 'available')],
        )


if __name__ == '__main__':
    unittest.main()
