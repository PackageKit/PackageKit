#!/usr/bin/env python3
#
# Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# upgrade-system of pk-backend-poldek.

import unittest

from pkpoldek import Env, Helper, errors, package_ids, require_root

INSTALLED = [
    ('repo-a', 'foo-1.0-1.noarch'),
    ('repo-a', 'libbar-1.0-1.x86_64'),
    ('repo-a', 'baz-1.0-1.noarch'),
    ('repo-a', 'glibc-1.0-1.noarch'),
]


def setUpModule():
    require_root()


class SystemTest(unittest.TestCase):
    sources = None
    options = None

    def setUp(self):
        self.env = Env(INSTALLED, self.sources, self.options)
        self.prepare()
        self.helper = Helper(self.env)
        self.helper.hello()

    def tearDown(self):
        self.assertEqual(self.helper.close(), 0)
        self.env.cleanup()

    def prepare(self):
        pass

    def assertOk(self, events):
        self.assertEqual(errors(events), [])

    def upgrade(self, flags=()):
        return self.helper.run(
            'upgrade-system', flags=list(flags), distro_id='', upgrade_kind='default'
        )


class UpgradeTest(SystemTest):
    options = {'hold': 'glibc'}

    def test_simulate(self):
        before = self.env.installed()
        events = self.upgrade(['simulate'])
        self.assertOk(events)
        # held packages stay as they are
        self.assertEqual(
            package_ids(events),
            [('baz;1.1-1;noarch;repo-a;', 'update'), ('foo;1.1-1;noarch;repo-a;', 'update')],
        )
        self.assertEqual(self.env.installed(), before)

    def test_upgrade(self):
        self.assertOk(self.upgrade())
        installed = self.env.installed()
        self.assertIn('baz-1.1-1.noarch', installed)
        self.assertIn('foo-1.1-1.noarch', installed)
        self.assertIn('glibc-1.0-1.noarch', installed)
        # and nothing is left to do
        events = self.upgrade(['simulate'])
        self.assertOk(events)
        self.assertEqual(package_ids(events), [])


if __name__ == '__main__':
    unittest.main()
