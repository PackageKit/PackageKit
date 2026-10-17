#!/usr/bin/env python3
#
# Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The read-only roles of pk-backend-poldek against the fixture repositories
# and a test root with foo-1.0, libbar-1.0.x86_64 and baz-1.0 installed.

import time
import unittest

from pkpoldek import Env, Helper, Server, errors, fixture, items, package_ids, require_root, session


def I(name, evr, arch='noarch'):  # noqa: E743 - reads like the info it stands for
    return ('%s;%s;%s;;installed' % (name, evr, arch), 'installed')


def A(name, evr, arch='noarch', repo='repo-a'):
    return ('%s;%s;%s;%s;' % (name, evr, arch, repo), 'available')


INSTALLED = [
    ('repo-a', 'foo-1.0-1.noarch'),
    ('repo-a', 'libbar-1.0-1.x86_64'),
    ('repo-a', 'baz-1.0-1.noarch'),
]


def setUpModule():
    require_root()


class QueryEnv:
    """One test root and helper for all tests of a class."""

    sources = None

    @classmethod
    def setUpClass(cls):
        cls.env = Env(INSTALLED, cls.sources)
        cls.helper = Helper(cls.env)
        cls.helper.hello()

    @classmethod
    def tearDownClass(cls):
        assert cls.helper.close() == 0
        cls.env.cleanup()

    def query(self, role, **args):
        events = self.helper.run(role, **args)
        self.assertEqual(errors(events), [])
        self.assertEqual(events[0], {'ev': 'status', 'job': events[0]['job'], 'status': 'query'})
        return package_ids(events)

    def resolve(self, *names, filters=()):
        return self.query('resolve', filters=list(filters), values=list(names))


class QueryTest(QueryEnv, unittest.TestCase):
    def test_installed_and_available_ids(self):
        # the available foo-1.0 is the installed one and only shown as installed
        self.assertEqual(self.resolve('foo'), [I('foo', '1.0-1'), A('foo', '1.1-1')])

    def test_same_nevra_in_two_repos(self):
        # repo-a comes first in the configuration and wins
        self.assertEqual(self.resolve('foo', filters=['~installed']), [A('foo', '1.1-1')])
        self.assertEqual(self.resolve('bonly'), [A('bonly', '1.0-1', repo='repo-b')])

    def test_filter_installed(self):
        self.assertEqual(
            self.query('get-packages', filters=['installed']),
            [I('baz', '1.0-1'), I('foo', '1.0-1'), I('libbar', '1.0-1', 'x86_64')],
        )
        self.assertEqual(self.resolve('baz', filters=['~installed']), [A('baz', '1.1-1')])

    def test_filter_newest(self):
        self.assertEqual(self.resolve('dlib'), [A('dlib', '2.0-1'), A('dlib', '1.0-1')])
        self.assertEqual(self.resolve('dlib', filters=['newest']), [A('dlib', '2.0-1')])
        self.assertEqual(
            self.resolve('baz', filters=['newest']), [I('baz', '1.0-1'), A('baz', '1.1-1')]
        )
        # newest of the available ones is installed: no older available one stands in
        self.assertEqual(
            self.resolve('libbar', filters=['newest', 'arch']), [I('libbar', '1.0-1', 'x86_64')]
        )

    def test_filter_not_newest(self):
        self.assertEqual(self.resolve('dlib', filters=['~newest']), [A('dlib', '1.0-1')])
        # one version for each arch: each is the newest of its name and arch
        self.assertEqual(self.resolve('libbar', filters=['~newest']), [])

    def test_filter_arch(self):
        self.assertEqual(self.resolve('libbar', filters=['arch']), [I('libbar', '1.0-1', 'x86_64')])
        self.assertEqual(self.resolve('libbar', filters=['~arch']), [A('libbar', '1.0-1', 'i686')])

    def test_filter_devel(self):
        self.assertEqual(
            self.query('get-packages', filters=['devel']), [A('libbar-devel', '1.0-1')]
        )
        devel = self.query('get-packages', filters=['~devel'])
        self.assertNotIn(A('libbar-devel', '1.0-1'), devel)
        self.assertIn(A('guiapp', '1.0-1'), devel)

    def test_resolve_is_exact(self):
        for value in ('fo*', 'foo;', '*', 'FOO', 'fo'):
            self.assertEqual(self.resolve(value), [], value)
        self.assertEqual(self.resolve('foo;1.1-1;noarch;repo-a;'), [A('foo', '1.1-1')])
        self.assertEqual(self.resolve('epo'), [A('epo', '2:1.0-1')])

    def test_details(self):
        ids = [
            I('foo', '1.0-1')[0],
            A('guiapp', '1.0-1')[0],
            A('libbar-devel', '1.0-1')[0],
            A('epo', '2:1.0-1')[0],
        ]
        events = self.helper.run('get-details', package_ids=ids)
        self.assertEqual(errors(events), [])
        details = {item['package_id']: item for item in items(events, 'details')}
        self.assertEqual(list(details), ids)
        foo = details[ids[0]]
        self.assertEqual(foo['summary'], 'foo fixture package')
        self.assertEqual(foo['description'], 'The foo fixture package used by tests.')
        self.assertEqual(foo['url'], 'https://example.com/foo')
        self.assertEqual(foo['license'], 'GPL-2.0-or-later')
        self.assertEqual(foo['group'], 'admin')
        self.assertGreater(foo['size'], 0)
        # installed packages have nothing to download
        self.assertNotIn('download_size', foo)
        self.assertGreater(details[ids[1]]['download_size'], 0)
        self.assertEqual(details[ids[1]]['group'], 'utilities')
        self.assertEqual(details[ids[1]]['license'], 'MIT')
        self.assertEqual(details[ids[2]]['group'], 'devel')

    def test_files(self):
        events = self.helper.run(
            'get-files', package_ids=[I('foo', '1.0-1')[0], A('libbar-devel', '1.0-1')[0]]
        )
        self.assertEqual(errors(events), [])
        files = [(e['package_id'], e['files']) for e in events if e['ev'] == 'files']
        self.assertEqual(
            files,
            [
                (I('foo', '1.0-1')[0], {'/usr/bin/foo': 'file'}),
                (A('libbar-devel', '1.0-1')[0], {'/usr/include/bar.h': 'file'}),
            ],
        )

    def test_search_file(self):
        self.assertEqual(
            self.query('search-file', filters=[], values=['/usr/bin/foo']),
            [I('foo', '1.0-1'), A('foo', '1.1-1')],
        )
        self.assertEqual(
            self.query('search-file', filters=[], values=['bar.h']), [A('libbar-devel', '1.0-1')]
        )
        self.assertEqual(self.query('search-file', filters=[], values=['/usr/bin']), [])

    def test_what_provides(self):
        self.assertEqual(
            self.query('what-provides', filters=[], values=['libbar.so.1']),
            [I('libbar', '1.0-1', 'x86_64'), A('libbar', '1.0-1', 'i686')],
        )
        self.assertEqual(
            self.query('what-provides', filters=['~installed'], values=['/usr/bin/baz']),
            [A('baz', '1.1-1')],
        )

    def test_get_dependencies(self):
        def depends(pkg, recursive=False, filters=()):
            return self.query(
                'get-dependencies', filters=list(filters), package_ids=[pkg[0]], recursive=recursive
            )

        self.assertEqual(depends(A('dapp', '2.0-1')), [A('dlib', '2.0-1')])
        self.assertEqual(depends(A('dapp', '1.0-1')), [A('dlib', '1.0-1')])
        # installed libbar satisfies foo, the available i686 one is not needed
        self.assertEqual(depends(I('foo', '1.0-1')), [I('libbar', '1.0-1', 'x86_64')])
        self.assertEqual(depends(A('fooplug', '1.0-1')), [I('foo', '1.0-1')])
        self.assertEqual(
            depends(A('fooplug', '1.0-1'), recursive=True),
            [I('foo', '1.0-1'), I('libbar', '1.0-1', 'x86_64')],
        )
        self.assertEqual(depends(A('fooplug', '1.0-1'), filters=['~installed']), [])

    def test_get_dependents(self):
        def required(pkg, recursive=False, filters=()):
            return self.query(
                'get-dependents', filters=list(filters), package_ids=[pkg[0]], recursive=recursive
            )

        libbar = I('libbar', '1.0-1', 'x86_64')
        self.assertEqual(required(libbar, filters=['installed']), [I('foo', '1.0-1')])
        self.assertEqual(
            required(libbar), [I('foo', '1.0-1'), A('foo', '1.1-1'), A('libbar-devel', '1.0-1')]
        )
        self.assertEqual(
            required(libbar, recursive=True),
            [
                I('foo', '1.0-1'),
                A('foo', '1.1-1'),
                A('fooplug', '1.0-1'),
                A('libbar-devel', '1.0-1'),
            ],
        )

    def test_invalid_ids(self):
        cases = [
            ('garbage', 'package-id-invalid'),
            ('foo;1.0-1;noarch;;', 'package-id-invalid'),
            ('foo;9.9-1;noarch;repo-a;', 'package-not-found'),
            # the copy poldek dropped for the one from repo-a
            ('foo;1.1-1;noarch;repo-b;', 'package-not-found'),
            ('foo;1.0-1;noarch;local;', 'package-not-found'),
            ('baz;1.1-1;noarch;;installed', 'package-not-found'),
        ]
        for package_id, code in cases:
            for role in ('get-details', 'get-files'):
                events = self.helper.run(role, package_ids=[package_id])
                self.assertEqual([e[0] for e in errors(events)], [code], (role, package_id))
                self.assertEqual(items(events, 'details'), [])

    def test_blank_search(self):
        for role in ('search-name', 'search-details'):
            self.assertEqual(self.query(role, filters=[], values=[' ']), [])

    def test_search_name(self):
        self.assertEqual(
            self.query('search-name', filters=[], values=['FOO']),
            [I('foo', '1.0-1'), A('foo', '1.1-1'), A('fooplug', '1.0-1')],
        )
        self.assertEqual(
            self.query('search-name', filters=[], values=['lib devel']),
            [A('libbar-devel', '1.0-1')],
        )

    def test_search_details(self):
        for values in (['example.com/guiapp'], ['MIT'], ['fixture', 'GUIAPP']):
            self.assertEqual(
                self.query('search-details', filters=[], values=values),
                [A('guiapp', '1.0-1')],
                values,
            )

    def test_search_group(self):
        def group(*values):
            return self.query('search-group', filters=[], values=list(values))

        self.assertEqual(group('utilities'), [A('guiapp', '1.0-1')])
        self.assertEqual(group('devel'), [A('libbar-devel', '1.0-1')])
        self.assertEqual(
            group('libraries'), [I('libbar', '1.0-1', 'x86_64'), A('libbar', '1.0-1', 'i686')]
        )
        # foo-1.1 of repo-b lost to the one of repo-a
        self.assertEqual(group('repo:repo-b'), [A('bonly', '1.0-1', repo='repo-b')])
        self.assertEqual(group('unknown'), [])

    def test_repo_list(self):
        events = self.helper.run('get-repo-list', filters=[])
        self.assertEqual(errors(events), [])
        repos = [(r['repo_id'], r['enabled']) for r in items(events, 'repo-details')]
        self.assertEqual(repos, [('repo-a', True), ('repo-b', True)])


class PriorityTest(QueryEnv, unittest.TestCase):
    """The repositories' priorities swapped."""

    sources = [('repo-a', {'pri': '20'}), ('repo-b', {'pri': '10'})]

    def test_same_nevra_in_two_repos(self):
        self.assertEqual(
            self.resolve('foo', filters=['~installed']), [A('foo', '1.1-1', repo='repo-b')]
        )


class RepoListTest(unittest.TestCase):
    def test_disabled_source(self):
        env = Env(sources=[('repo-a', {}), ('repo-b', {'noauto': 'yes'})])
        with session(env) as helper:
            events = helper.run('get-repo-list', filters=[])
            repos = [(r['repo_id'], r['enabled']) for r in items(events, 'repo-details')]
            self.assertEqual(repos, [('repo-a', True), ('repo-b', False)])
            # packages of a disabled repository are not available
            self.assertEqual(package_ids(helper.run('resolve', filters=[], values=['bonly'])), [])

    def test_password_not_shown(self):
        url = 'https://alice:s3cret@127.0.0.1:1/th/'
        env = Env(
            sources=[('repo-a', {}), ('repo-b', {'type': 'pndir', 'path': url, 'noauto': 'yes'})]
        )
        with session(env) as helper:
            events = helper.run('get-repo-list', filters=[])
            descriptions = [r['description'] for r in items(events, 'repo-details')]
            self.assertIn('127.0.0.1:1/th/', descriptions[1])
            self.assertNotIn('s3cret', descriptions[1])


class LocalSourceTest(unittest.TestCase):
    """A repository named local: its ids look like those of package files."""

    def test_source_named_local(self):
        with session(Env(sources=[('repo-b', {'name': 'local'})])) as helper:
            events = helper.run('resolve', filters=[], values=['bonly'])
            self.assertEqual(package_ids(events), [('bonly;1.0-1;noarch;local;', 'available')])
            events = helper.run('get-details', package_ids=['bonly;1.0-1;noarch;local;'])
            self.assertEqual(errors(events), [])
            self.assertEqual(len(items(events, 'details')), 1)


class OwnRequirementTest(unittest.TestCase):
    """selfy 1.0 and 2.0 both provide what they require."""

    def test_no_dependency_on_itself(self):
        env = Env([('repo-u', 'selfy-1.0-1.noarch')], [('repo-u', {})])
        with session(env) as helper:
            for role, package_id in (
                ('get-dependencies', 'selfy;2.0-1;noarch;repo-u;'),
                ('get-dependents', 'selfy;2.0-1;noarch;repo-u;'),
                ('get-dependents', 'selfy;1.0-1;noarch;;installed'),
            ):
                events = helper.run(role, filters=[], package_ids=[package_id], recursive=False)
                self.assertEqual(errors(events), [])
                self.assertEqual(package_ids(events), [], (role, package_id))


def U(name, version='1.0'):  # noqa: N802 - an id, like A and I
    return '%s;%s-1;noarch;repo-u;' % (name, version)


def INSTALLED_U(name):  # noqa: N802
    return '%s;1.0-1;noarch;;installed' % name


def dependency_ids(helper, role, package_id, filters=()):
    events = helper.run(role, filters=list(filters), package_ids=[package_id], recursive=False)
    assert errors(events) == [], errors(events)
    return sorted(p[0] for p in package_ids(events))


class DependencyKindsTest(unittest.TestCase):
    """File dependencies of installed packages, and rich dependencies with rpm's meaning."""

    @classmethod
    def setUpClass(cls):
        names = ['fprov', 'fdep', 'vprov', 'vdep', 'ifcond']
        cls.env = Env([('repo-u', n + '-1.0-1.noarch') for n in names], [('repo-u', {})])
        cls.helper = Helper(cls.env)
        cls.helper.hello()

    @classmethod
    def tearDownClass(cls):
        assert cls.helper.close() == 0
        cls.env.cleanup()

    def query(self, role, package_id, filters=()):
        """Names and versions of what @role gives for @package_id."""
        ids = dependency_ids(self.helper, role, package_id, filters)
        return [' '.join(i.split(';')[:2]) for i in ids]

    def test_file_dependency(self):
        self.assertEqual(self.query('get-dependencies', INSTALLED_U('fdep')), ['fprov 1.0-1'])
        self.assertEqual(
            self.query('get-dependents', INSTALLED_U('fprov'), ['installed']), ['fdep 1.0-1']
        )

    def test_file_provided_without_the_file(self):
        # vprov provides /usr/bin/vfile, which it has not
        self.assertEqual(self.query('get-dependencies', INSTALLED_U('vdep')), ['vprov 1.0-1'])
        self.assertEqual(
            self.query('get-dependents', INSTALLED_U('vprov'), ['installed']), ['vdep 1.0-1']
        )

    def test_and(self):
        self.assertEqual(
            self.query('get-dependencies', U('richapp')), ['leftdep 1.0-1', 'rightdep 1.0-1']
        )
        self.assertIn('richapp 1.0-1', self.query('get-dependents', U('leftdep')))

    def test_or_met_by_installed(self):
        # (fprov or rightdep) with fprov installed needs nothing available
        self.assertEqual(self.query('get-dependencies', U('orapp')), ['fprov 1.0-1'])

    def test_each_dependency_on_its_own(self):
        # fprov is installed, leftdep is not: one does not stand for the other
        for name in ('mixapp', 'mixrichapp'):
            self.assertEqual(
                self.query('get-dependencies', U(name)), ['fprov 1.0-1', 'leftdep 1.0-1'], name
            )
        self.assertEqual(
            self.query('get-dependencies', U('ortwoapp')), ['fprov 1.0-1', 'rangedep 2.0-1']
        )

    def test_with_of_alternatives(self):
        # ((rangedep or leftdep) with rangedep < 2.5)
        self.assertEqual(
            self.query('get-dependencies', U('orwithapp')), ['rangedep 1.0-1', 'rangedep 2.0-1']
        )

    def test_with_partly_met_by_itself(self):
        # selfwithapp provides swcap and requires (swcap with swother)
        self.assertEqual(self.query('get-dependencies', U('selfwithapp')), ['swprov 1.0-1'])

    def test_or_of_a_part_installed(self):
        # (fprov or (vprov and leftdep)): vprov is installed, leftdep is not
        self.assertEqual(self.query('get-dependencies', U('orandapp')), ['fprov 1.0-1'])
        self.assertIn('orandapp 1.0-1', self.query('get-dependents', U('fprov')))
        self.assertNotIn('orandapp 1.0-1', self.query('get-dependents', U('vprov')))
        self.assertNotIn('orandapp 1.0-1', self.query('get-dependents', U('leftdep')))

    def test_part_met_by_itself(self):
        # selfapp provides selfpart and requires (selfpart and leftdep)
        self.assertEqual(self.query('get-dependencies', U('selfapp')), ['leftdep 1.0-1'])
        self.assertEqual(self.query('get-dependents', U('selfprov')), [])

    def test_condition_met_without_a_package(self):
        # by rpm itself, and by what the package provides
        for name in ('rpmlibapp', 'selfcondapp'):
            self.assertEqual(self.query('get-dependencies', U(name)), ['leftdep 1.0-1'], name)

    def test_with(self):
        # (rangedep >= 1.5 with rangedep < 2.5): one package meeting both
        self.assertEqual(self.query('get-dependencies', U('rangeapp')), ['rangedep 2.0-1'])
        # rangeapp needs 2.0 only; ortwoapp has the same with, orwithapp takes any below 2.5
        for version, requirers in (
            ('1.0', ['orwithapp 1.0-1']),
            ('2.0', ['ortwoapp 1.0-1', 'orwithapp 1.0-1', 'rangeapp 1.0-1']),
            ('3.0', []),
        ):
            self.assertEqual(
                self.query('get-dependents', U('rangedep', version)), requirers, version
            )

    def test_if(self):
        # ifcond is installed: the if branch applies, the condition is no dependency
        self.assertEqual(self.query('get-dependencies', U('condapp')), ['thendep 1.0-1'])
        self.assertEqual(self.query('get-dependencies', U('elseapp')), ['thendep 1.0-1'])
        self.assertEqual(
            self.query('get-dependents', U('thendep')), ['condapp 1.0-1', 'elseapp 1.0-1']
        )
        self.assertEqual(self.query('get-dependents', U('elsedep')), [])
        self.assertEqual(self.query('get-dependents', U('ifcond')), [])

    def test_else(self):
        # without ifcond installed: the else branch, or nothing
        with session(Env(sources=[('repo-u', {})])) as helper:
            for name, deps in (('condapp', []), ('elseapp', [U('elsedep')])):
                self.assertEqual(dependency_ids(helper, 'get-dependencies', U(name)), deps, name)


class OfflineTest(unittest.TestCase):
    """A repository that cannot be reached, with nothing in the cache."""

    def test_roles_without_indexes(self):
        source = {'type': 'pndir', 'path': 'http://127.0.0.1:1/repo-a/'}
        # one attempt to connect, not poldek's hundred
        env = Env(sources=[('repo-a', source)], options={'vfile retries': '1'})
        with session(env) as helper:
            path = fixture('repo-a', 'guiapp-1.0-1.noarch')
            for role in ('get-details-local', 'get-files-local'):
                self.assertEqual(errors(helper.run(role, files=[path])), [], role)
            events = helper.run('get-repo-list', filters=[])
            self.assertEqual(errors(events), [])
            self.assertEqual([r['repo_id'] for r in items(events, 'repo-details')], ['repo-a'])
            # what needs the repositories fails, and says why
            events = helper.run('refresh-cache', force=False)
            self.assertNotEqual(errors(events), [])
            self.assertEqual(errors(events)[0][0], 'repo-not-available', errors(events))


class SourceNameEncodingTest(unittest.TestCase):
    def test_source_name_not_utf8(self):
        env = Env(sources=[('repo-b', {})])
        with open(env.conf, 'rb') as f:
            conf = f.read()
        with open(env.conf, 'wb') as f:
            f.write(conf.replace(b'name = repo-b', b'name = r\xb1'))
        with session(env) as helper:
            events = helper.run('resolve', filters=[], values=['bonly'])
            ((package_id, info),) = package_ids(events)
            self.assertEqual(package_id, 'bonly;1.0-1;noarch;r\ufffd;')
            events = helper.run('get-details', package_ids=[package_id])
            self.assertEqual(errors(events), [])
            events = helper.run('search-group', filters=[], values=['repo:r\ufffd'])
            self.assertIn((package_id, 'available'), package_ids(events))


class RepoUTest(unittest.TestCase):
    def test_devel_by_name(self):
        # python3 and the like are in Development groups, but no development files
        with session(Env(sources=[('repo-u', {})])) as helper:
            events = helper.run('get-packages', filters=['~devel'])
            self.assertIn(('pytool;1.0-1;noarch;repo-u;', 'available'), package_ids(events))

    def test_description_language(self):
        tool = 'tool;1.1-1;noarch;repo-u;'
        with session(Env(sources=[('repo-u', {})])) as helper:
            for locale, summary in (
                ('pl_PL.UTF-8', 'narzędzie testowe'),
                ('C', 'tool fixture package'),
            ):
                events = helper.run('get-details', ctx={'locale': locale}, package_ids=[tool])
                self.assertEqual(items(events, 'details')[0]['summary'], summary, locale)


class PndirTest(unittest.TestCase):
    """A pndir index has only the files of directories something requires; the rest are loaded."""

    def test_full_path(self):
        server = Server(['repo-a'])
        try:
            with session(Env(INSTALLED, [('repo-a', server.source('repo-a'))])) as helper:
                for role in ('search-file', 'what-provides'):
                    events = helper.run(role, filters=[], values=['/usr/share/dlib/v'])
                    self.assertEqual(
                        sorted(p[0] for p in package_ids(events)),
                        ['dlib;1.0-1;noarch;repo-a;', 'dlib;2.0-1;noarch;repo-a;'],
                        role,
                    )
        finally:
            server.close()


class StateTest(unittest.TestCase):
    def setUp(self):
        self.env = Env(INSTALLED)
        self.helper = Helper(self.env)

    def tearDown(self):
        self.assertEqual(self.helper.close(), 0)
        self.env.cleanup()

    def installed(self):
        events = self.helper.run('get-packages', filters=['installed'])
        summaries = {item['package_id']: item.get('summary') for item in items(events, 'packages')}
        # each its own, in a later helper too
        for package_id, summary in summaries.items():
            self.assertEqual(summary, package_id.split(';')[0] + ' fixture package')
        return [pkg[0] for pkg in package_ids(events)]

    def test_rpmdb_changed_by_someone_else(self):
        before = self.installed()
        self.assertNotIn(I('guiapp', '1.0-1')[0], before)
        self.env.rpm('-i', '--nodeps', '--noscripts', fixture('repo-a', 'guiapp-1.0-1.noarch'))
        self.env.rpm('-e', '--nodeps', '--noscripts', 'baz')
        # the same helper, no refresh in between
        after = self.installed()
        self.assertIn(I('guiapp', '1.0-1')[0], after)
        self.assertNotIn(I('baz', '1.0-1')[0], after)
        # the cache poclidek writes on exit is not stale for the command line either
        self.assertEqual(self.helper.close(), 0)
        self.assertEqual(self.env.poldek_installed(), self.env.installed())
        self.helper = Helper(self.env)
        self.assertEqual(self.installed(), after)


class LazySetupTest(unittest.TestCase):
    def test_bad_config_fails_the_job_not_the_hello(self):
        helper = Helper(args=['--config', '/nonexistent/poldek.conf', '--root', '/nonexistent'])
        try:
            start = time.monotonic()
            self.assertEqual(helper.hello()['ev'], 'hello')
            self.assertLess(time.monotonic() - start, 2)
            events = helper.run('resolve', filters=[], values=['foo'])
            self.assertEqual([e[0] for e in errors(events)], ['failed-config-parsing'])
        finally:
            self.assertEqual(helper.close(), 0)


if __name__ == '__main__':
    unittest.main()
