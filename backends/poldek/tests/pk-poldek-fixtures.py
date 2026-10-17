#!/usr/bin/env python3
#
# Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds the fixture repositories for the poldek tests: small packages from
# one spec template, built reproducibly, so build times, changelogs and
# package ids are the same on every run. Packages with "sign" are signed with
# one of two throwaway keys made here; OUTDIR/keys has the public ones.
#
# usage: pk-poldek-fixtures.py DESCRIPTION.json OUTDIR STAMP

import json
import os
import random
import shutil
import subprocess
import sys
import tempfile

# 2026-01-01, the build time and file mtimes of a package without its own
EPOCH = '1767225600'


def spec_text(pkg):
    lines = [
        '%define debug_package %{nil}',
        'Name: %s' % pkg['name'],
        'Version: %s' % pkg['version'],
        'Release: %s' % pkg.get('release', '1'),
    ]
    if 'epoch' in pkg:
        lines.append('Epoch: %s' % pkg['epoch'])
    lines.append('Summary: %s' % pkg.get('summary', pkg['name'] + ' fixture package'))
    if 'summary_pl' in pkg:
        lines.append('Summary(pl.UTF-8): %s' % pkg['summary_pl'])
    lines += [
        'License: %s' % pkg.get('license', 'GPL-2.0-or-later'),
        'Group: %s' % pkg.get('group', 'Applications/System'),
        'URL: %s' % pkg.get('url', 'https://example.com/' + pkg['name']),
    ]
    if pkg.get('arch', 'noarch') == 'noarch':
        lines.append('BuildArch: noarch')
    for tag in ('requires', 'provides', 'conflicts', 'obsoletes'):
        for value in pkg.get(tag, []):
            lines.append('%s: %s' % (tag.capitalize(), value))
    lines += [
        '%description',
        pkg.get('description', 'The %s fixture package used by tests.' % pkg['name']),
    ]
    # poldek takes a language only from a package with both a summary and a description in it
    if 'summary_pl' in pkg:
        lines += ['%description -l pl.UTF-8', 'Pakiet testowy %s.' % pkg['name']]
    subpackages = pkg.get('subpackages', [])
    for sub in subpackages:
        lines += [
            '%package -n ' + sub['name'],
            'Summary: %s fixture subpackage' % sub['name'],
            '%description -n ' + sub['name'],
            'The %s fixture subpackage used by tests.' % sub['name'],
        ]
    lines.append('%install')
    files = dict(pkg.get('files', {}))
    for sub in subpackages:
        files.update(sub['files'])
    for path, content in files.items():
        lines.append('install -d $RPM_BUILD_ROOT%s' % os.path.dirname(path))
        if isinstance(content, dict):
            # made by blob(), the same bytes on every run
            lines.append('cp %%{_sourcedir}/%s $RPM_BUILD_ROOT%s' % (blob_name(path), path))
        else:
            lines.append("printf '%%s\\n' '%s' > $RPM_BUILD_ROOT%s" % (content, path))
    lines.append('%files')
    lines += list(pkg.get('files', {}))
    for sub in subpackages:
        lines.append('%files -n ' + sub['name'])
        lines += list(sub['files'])
    lines.append('%changelog')
    for entry in pkg.get('changelog', []):
        lines.append('* %s Fixture Author <fixture@example.com>' % entry['date'])
        lines += ['- ' + text for text in entry['text']]
        lines.append('')
    return '\n'.join(lines) + '\n'


def blob_name(path):
    return path.strip('/').replace('/', '_') + '.blob'


def blob(path, content, topdir):
    """Incompressible but reproducible file contents."""
    sources = os.path.join(topdir, 'SOURCES')
    os.makedirs(sources, exist_ok=True)
    rng = random.Random(content.get('seed', 0))
    with open(os.path.join(sources, blob_name(path)), 'wb') as f:
        f.write(rng.randbytes(content['random_bytes']))


def make_keys(outdir):
    """Two signing keys, each in a gnupg home of its own."""
    keys = os.path.join(outdir, 'keys')
    os.makedirs(keys)
    for name in ('fixture', 'other'):
        home = os.path.join(keys, name + '.gnupg')
        os.makedirs(home, mode=0o700)
        uid = '%s key <%s@example.com>' % (name.capitalize(), name)
        subprocess.run(
            ['gpg', '--homedir', home, '--batch', '--passphrase', '', '--quick-gen-key', uid]
            + ['ed25519', 'sign', 'never'],
            check=True,
            capture_output=True,
        )
        with open(os.path.join(keys, name + '.asc'), 'wb') as f:
            out = subprocess.run(
                ['gpg', '--homedir', home, '--armor', '--export', name + '@example.com'],
                check=True,
                capture_output=True,
            ).stdout
            f.write(out)
    return keys


def sign(path, key, keys):
    home = os.path.join(keys, key + '.gnupg')
    subprocess.run(
        ['rpmsign', '--define', '_gpg_path ' + home]
        + ['--define', '_gpg_name %s@example.com' % key, '--addsign', path],
        check=True,
        capture_output=True,
    )


def corrupt(path):
    """Flips a byte of the payload, so the digests no longer match."""
    with open(path, 'r+b') as f:
        f.seek(-100, os.SEEK_END)
        byte = f.read(1)
        f.seek(-100, os.SEEK_END)
        f.write(bytes([byte[0] ^ 0xFF]))


def rpm_path(pkg, outdir):
    return os.path.join(
        outdir,
        '%s-%s-%s.%s.rpm'
        % (pkg['name'], pkg['version'], pkg.get('release', '1'), pkg.get('arch', 'noarch')),
    )


def build(pkg, outdir, topdir):
    for path, content in pkg.get('files', {}).items():
        if isinstance(content, dict):
            blob(path, content, topdir)
    spec = os.path.join(topdir, '%s-%s-%s.spec' % (pkg['name'], pkg['version'], pkg.get('arch')))
    with open(spec, 'w') as f:
        f.write(spec_text(pkg))
    cmd = [
        'rpmbuild',
        '-bb',
        '--quiet',
        '--define',
        '_topdir ' + topdir,
        '--define',
        '_rpmdir ' + outdir,
        '--define',
        '_build_name_fmt %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm',
        '--define',
        '_buildhost fixture.example.com',
        '--define',
        'build_mtime_policy clamp_to_source_date_epoch',
        '--define',
        'use_source_date_epoch_as_buildtime 1',
        '--define',
        '_binary_payload w9.gzdio',
    ]
    # BuildArch only works for noarch: rpmbuild refuses foreign arches there
    if pkg.get('arch', 'noarch') != 'noarch':
        cmd += ['--target', pkg['arch'] + '-pld-linux']
    env = dict(os.environ, SOURCE_DATE_EPOCH=pkg.get('source_date_epoch', EPOCH))
    subprocess.run(cmd + [spec], check=True, env=env, stdout=subprocess.DEVNULL)


def main():
    desc, outdir, stamp = sys.argv[1:4]
    with open(desc) as f:
        repos = json.load(f)
    shutil.rmtree(outdir, ignore_errors=True)
    os.makedirs(outdir)
    keys = make_keys(outdir)
    for repo, pkgs in repos.items():
        repodir = os.path.join(outdir, repo)
        os.makedirs(repodir)
        with tempfile.TemporaryDirectory() as topdir:
            for pkg in pkgs:
                build(pkg, repodir, topdir)
                if 'sign' in pkg:
                    sign(rpm_path(pkg, repodir), pkg['sign'], keys)
                if pkg.get('corrupt'):
                    corrupt(rpm_path(pkg, repodir))
    with open(stamp, 'w') as f:
        f.write('')


if __name__ == '__main__':
    main()
