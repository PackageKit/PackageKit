#!/usr/bin/env python3
#
# Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# get-updates and get-update-detail of pk-backend-poldek against the fixture
# repositories, with foo-1.0, libbar-1.0.x86_64, baz-1.0 and glibc-1.0 installed.

import unittest

from pkpoldek import Env, Helper, errors, fixture, items, package_ids, require_root, session

INSTALLED = [
    ('repo-a', 'foo-1.0-1.noarch'),
    ('repo-a', 'libbar-1.0-1.x86_64'),
    ('repo-a', 'baz-1.0-1.noarch'),
    ('repo-a', 'glibc-1.0-1.noarch'),
]
BAZ = 'baz;1.1-1;noarch;repo-a;'
FOO = 'foo;1.1-1;noarch;repo-a;'
GLIBC = 'glibc;1.1-1;noarch;repo-a;'


def setUpModule():
    require_root()


class UpdatesTest(unittest.TestCase):
    options = None

    @classmethod
    def setUpClass(cls):
        cls.env = Env(INSTALLED, options=cls.options)
        cls.helper = Helper(cls.env)
        cls.helper.hello()

    @classmethod
    def tearDownClass(cls):
        assert cls.helper.close() == 0
        cls.env.cleanup()

    def updates(self, filters=()):
        events = self.helper.run('get-updates', filters=list(filters))
        self.assertEqual(errors(events), [])
        return package_ids(events)

    def severities(self):
        events = self.helper.run('get-updates', filters=[])
        return {
            i['package_id']: i['severity'] for i in items(events, 'packages') if 'severity' in i
        }

    def details(self, *ids):
        events = self.helper.run('get-update-detail', package_ids=list(ids))
        return errors(events), {
            item['package_id']: item for item in items(events, 'update-details')
        }


class GetUpdatesTest(UpdatesTest):
    def test_updates(self):
        # foo-1.1 is in both repositories, it is one update, from the one poldek kept;
        # its changelog since foo-1.0 names a CVE
        self.assertEqual(
            self.updates(), [(BAZ, 'update'), (FOO, 'update-security'), (GLIBC, 'update')]
        )
        self.assertEqual(self.updates(['devel']), [])

    def test_detail(self):
        errs, details = self.details(FOO, GLIBC)
        self.assertEqual(errs, [])
        foo = details[FOO]
        self.assertEqual(foo['updates'], ['foo;1.0-1;noarch;;installed'])
        self.assertEqual(foo['obsoletes'], [])
        self.assertEqual(
            foo['issues'],
            [
                {
                    'kind': 'cve',
                    'id': 'CVE-2026-0001',
                    'url': 'https://www.cve.org/CVERecord?id=CVE-2026-0001',
                }
            ],
        )
        # only what is newer than the installed foo-1.0
        self.assertIn('fix CVE-2026-0001 overflow', foo['changelog'])
        self.assertNotIn('initial', foo['changelog'])
        self.assertEqual(foo['update_text'], foo['changelog'])
        self.assertEqual(foo['state'], 'stable')
        self.assertEqual(foo['restart'], 'none')
        self.assertEqual(foo['time_issued'], '2026-02-01T00:00:00Z')
        self.assertEqual(details[GLIBC]['restart'], 'system')
        self.assertEqual(details[GLIBC]['issues'], [])

    def test_detail_not_found(self):
        errs, details = self.details('foo;9.9-1;noarch;repo-a;')
        self.assertEqual([e[0] for e in errs], ['update-not-found'])
        self.assertEqual(details, {})
        errs, details = self.details('garbage')
        self.assertEqual([e[0] for e in errs], ['package-id-invalid'])


class HeldTest(UpdatesTest):
    """A hold pattern in the configuration blocks the update."""

    options = {'hold': 'baz'}

    def test_held(self):
        self.assertEqual(
            self.updates(), [(BAZ, 'blocked'), (FOO, 'update-security'), (GLIBC, 'update')]
        )
        # blocked keeps the severity of the update it would be
        self.assertEqual(self.severities(), {BAZ: 'medium'})


class HeldSecurityTest(UpdatesTest):
    options = {'hold': 'foo'}

    def test_held_security_update(self):
        self.assertIn((FOO, 'blocked'), self.updates())
        self.assertEqual(self.severities(), {FOO: 'high'})


class UnresolvableTest(unittest.TestCase):
    def test_missing_dependency_blocks(self):
        # stuck-1.1 needs nonexistent-capability, the kernel ones nothing new
        installed = [
            ('repo-u', 'stuck-1.0-1.noarch'),
            ('repo-u', 'kernel-1.0-1.noarch'),
            ('repo-u', 'kernel-headers-1.0-1.noarch'),
        ]
        stuck = 'stuck;1.1-1;noarch;repo-u;'
        with session(Env(installed, [('repo-u', {})])) as helper:
            events = helper.run('get-updates', filters=[])
            self.assertEqual(errors(events), [])
            self.assertEqual(
                package_ids(events),
                [
                    ('kernel;1.1-1;noarch;repo-u;', 'update'),
                    ('kernel-headers;1.1-1;noarch;repo-u;', 'update'),
                    (stuck, 'blocked'),
                ],
            )
            # blocked keeps the severity of the security update it would be
            severities = {i['package_id']: i.get('severity') for i in items(events, 'packages')}
            self.assertEqual(severities[stuck], 'high')


class TwoVersionsInstalledTest(unittest.TestCase):
    def test_newer_than_both_is_blocked(self):
        # poldek gives up on a name installed in several versions, with an empty plan
        installed = [('repo-u', 'rangedep-1.0-1.noarch'), ('repo-u', 'rangedep-2.0-1.noarch')]
        with session(Env(installed, [('repo-u', {})])) as helper:
            events = helper.run('get-updates', filters=[])
            self.assertEqual(errors(events), [])
            self.assertEqual(package_ids(events), [('rangedep;3.0-1;noarch;repo-u;', 'blocked')])

    def test_newest_installed_is_no_update(self):
        # rangedep 1.0 and 3.0 side by side, as kernels are; 3.0 is the newest there is
        installed = [('repo-u', 'rangedep-1.0-1.noarch'), ('repo-u', 'rangedep-3.0-1.noarch')]
        with session(Env(installed, [('repo-u', {})])) as helper:
            events = helper.run('get-updates', filters=[])
            self.assertEqual(errors(events), [])
            self.assertEqual(package_ids(events), [])


class SubpackageTest(unittest.TestCase):
    """poldek gives a subpackage only "- see tool-1.1-1's log", the details are tool's."""

    def test_detail(self):
        doc = 'tool-doc;1.1-1;noarch;repo-u;'
        with session(Env([('repo-u', 'tool-doc-1.0-1.noarch')], [('repo-u', {})])) as helper:
            events = helper.run('get-updates', filters=[])
            self.assertEqual(package_ids(events), [(doc, 'update-security')])
            events = helper.run('get-update-detail', package_ids=[doc])
            self.assertEqual(errors(events), [])
            detail = items(events, 'update-details')[0]
            self.assertIn('fix CVE-2026-0002 in the docs viewer', detail['changelog'])
            self.assertNotIn("'s log", detail['changelog'])
            self.assertEqual([i['id'] for i in detail['issues']], ['CVE-2026-0002'])


class RestartTest(unittest.TestCase):
    def test_kernel_image_needs_reboot(self):
        installed = [('repo-u', 'kernel-1.0-1.noarch'), ('repo-u', 'kernel-headers-1.0-1.noarch')]
        kernel = 'kernel;1.1-1;noarch;repo-u;'
        headers = 'kernel-headers;1.1-1;noarch;repo-u;'
        with session(Env(installed, [('repo-u', {})])) as helper:
            events = helper.run('get-update-detail', package_ids=[kernel, headers])
            restart = {d['package_id']: d['restart'] for d in items(events, 'update-details')}
            self.assertEqual(restart, {kernel: 'system', headers: 'none'})


class StateTest(unittest.TestCase):
    def test_rpmdb_changed_by_someone_else(self):
        env = Env(INSTALLED)
        with session(env) as helper:
            ids = [p[0] for p in package_ids(helper.run('get-updates', filters=[]))]
            self.assertIn(BAZ, ids)
            env.rpm('-U', '--nodeps', '--noscripts', fixture('repo-a', 'baz-1.1-1.noarch'))
            ids = [p[0] for p in package_ids(helper.run('get-updates', filters=[]))]
            self.assertNotIn(BAZ, ids)
            self.assertIn(FOO, ids)


if __name__ == '__main__':
    unittest.main()
