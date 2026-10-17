#!/usr/bin/env python3
#
# Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Package files given by a client: get-details-local, get-files-local and
# install-files of pk-backend-poldek.

import glob
import os
import unittest

from pkpoldek import Env, Helper, errors, fixture, items, require_root

INSTALLED = [('repo-a', 'foo-1.0-1.noarch'), ('repo-a', 'libbar-1.0-1.x86_64')]


def setUpModule():
    require_root()


class LocalTest(unittest.TestCase):
    def setUp(self):
        self.env = Env(INSTALLED, [('repo-a', {}), ('repo-s', {})])
        self.helper = Helper(self.env)
        self.helper.hello()

    def tearDown(self):
        self.assertEqual(self.helper.close(), 0)
        self.env.cleanup()

    def test_details(self):
        events = self.helper.run(
            'get-details-local', files=[fixture('repo-a', 'guiapp-1.0-1.noarch')]
        )
        self.assertEqual(errors(events), [])
        (details,) = items(events, 'details')
        self.assertEqual(details['package_id'], 'guiapp;1.0-1;noarch;local;')
        self.assertEqual(details['group'], 'utilities')
        self.assertEqual(details['license'], 'MIT')
        self.assertEqual(details['summary'], 'guiapp fixture package')
        self.assertGreater(details['size'], 0)

    def test_files(self):
        events = self.helper.run('get-files-local', files=[fixture('repo-a', 'epo-1.0-1.noarch')])
        self.assertEqual(errors(events), [])
        files = [(e['package_id'], e['files']) for e in events if e['ev'] == 'files']
        self.assertEqual(files, [('epo;2:1.0-1;noarch;local;', {'/usr/bin/epo': 'file'})])

    def test_not_a_package(self):
        path = os.path.join(self.env.dir, 'junk.rpm')
        with open(path, 'w') as f:
            f.write('not a package\n')
        for role in ('get-details-local', 'get-files-local', 'install-files'):
            args = {'files': [path]}
            if role == 'install-files':
                args['flags'] = []
            events = self.helper.run(role, **args)
            self.assertEqual([e[0] for e in errors(events)], ['invalid-package-file'], role)

    def test_install(self):
        before = glob.glob('/tmp/pk-poldek-files-*')
        events = self.helper.run(
            'install-files', flags=[], files=[fixture('repo-a', 'dapp-2.0-1.noarch')]
        )
        self.assertEqual(errors(events), [])
        # its dependency comes from a repository
        self.assertIn('dapp-2.0-1.noarch', self.env.installed())
        self.assertIn('dlib-2.0-1.noarch', self.env.installed())
        done = [e['package_id'] for e in events if e['ev'] == 'item-progress' and e['value'] == 100]
        self.assertIn('dapp;2.0-1;noarch;local;', done)
        self.assertIn('dlib;2.0-1;noarch;repo-a;', done)
        self.assertEqual(glob.glob('/tmp/pk-poldek-files-*'), before)
        # a local package id is only good in its own job
        events = self.helper.run('get-details', package_ids=['dapp;2.0-1;noarch;local;'])
        self.assertEqual([e[0] for e in errors(events)], ['package-not-found'])

    def test_install_several(self):
        # two files, one given twice and once more through a link
        files = [fixture('repo-a', 'guiapp-1.0-1.noarch'), fixture('repo-a', 'baz-1.1-1.noarch')]
        link = os.path.join(self.env.dir, 'link.rpm')
        os.symlink(files[0], link)
        events = self.helper.run('install-files', flags=[], files=files + [files[0], link])
        self.assertEqual(errors(events), [])
        self.assertIn('guiapp-1.0-1.noarch', self.env.installed())
        self.assertIn('baz-1.1-1.noarch', self.env.installed())

    def test_install_trusted(self):
        before = self.env.installed()
        events = self.helper.run(
            'install-files',
            flags=['only-trusted'],
            files=[fixture('repo-s', 'sunsigned-1.0-1.noarch')],
        )
        self.assertEqual([e[0] for e in errors(events)], ['missing-gpg-signature'])
        self.assertEqual(self.env.installed(), before)


if __name__ == '__main__':
    unittest.main()
