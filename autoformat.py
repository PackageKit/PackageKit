#!/usr/bin/env python3
#
# Copyright (C) 2015-2026 Matthias Klumpp <mak@debian.org>
#
# SPDX-License-Identifier: LGPL-2.1+
#
# Format all PackageKit source code in-place.
#

import argparse
import fnmatch
import os
import re
import shutil
import subprocess
import sys
import tempfile
from glob import glob

# Minimum version of clang-format that we need
MIN_CLANG_FORMAT_VERSION = 22

# Minimum version of black that we need
MIN_BLACK_VERSION = 26

# Directories (or single files) to format. Paths are relative to the source root.
INCLUDE_LOCATIONS = [
    'autoformat.py',
    'backends',
    'client',
    'contrib',
    'data',
    'docs',
    'lib',
    'python',
    'src',
    'tests',
]

# Files that must not be touched, as fnmatch patterns against the full path.
EXCLUDE_MATCH = [
    '*/build/*',
    '*/_build/*',
    '*/builddir/*',
    '*/subprojects/*',
    '*.gen.hh',
    '*.gen.h',
    # backends that are opted-out of autoformatting
    '*/backends/alpm/*',
    '*/backends/dnf5/*',
    '*/backends/freebsd/*',
    '*/backends/zypp/*',
]

C_LIKE_SUFFIXES = ('.c', '.h', '.cpp', '.hpp', '.cc', '.hh')

# Extra rules layered on top of .clang-format when formatting our public C headers.
#
# Headers are almost entirely declarations, and PackageKit writes them as a
# table: the return type, the name and the parameter list each get their own
# column, and every parameter goes on its own line.
HEADER_STYLE_RULES = [
    # line up the declared names into a column
    'AlignConsecutiveDeclarations: AcrossEmptyLinesAndComments',
    # line up the values of enum members and other consecutive assignments
    'AlignConsecutiveAssignments: AcrossComments',
    # declarations keep their return type on the same line as the name
    'PenaltyReturnTypeOnItsOwnLine: 1000',
    # keep one parameter per line instead of collapsing declarations that
    # happen to fit within the column limit
    'BinPackParameters: AlwaysOnePerLine',
]


def check_tool(name, version_re=None, want_major=None):
    """Verify a formatter is present and, if asked, has the expected major version."""

    if not shutil.which(name):
        print(
            'The `{}` formatter is not installed. Please install it to continue!'.format(name),
            file=sys.stderr,
        )
        return False

    if version_re is None:
        return True

    out = subprocess.run([name, '--version'], capture_output=True, text=True).stdout
    m = re.search(version_re, out)
    if not m:
        print('Unable to determine the version of `{}`.'.format(name), file=sys.stderr)
        return False

    major = int(m.group(1))
    if major < want_major:
        print(
            'Found `{}` {}, but we need at least {} to format this tree.'.format(
                name, major, want_major
            ),
            file=sys.stderr,
        )
        return False

    return True


def nearest_style_file(path):
    """Return the `.clang-format` clang-format would pick for `path`, if any."""

    directory = os.path.dirname(os.path.abspath(path))
    while True:
        candidate = os.path.join(directory, '.clang-format')
        if os.path.isfile(candidate):
            return candidate
        parent = os.path.dirname(directory)
        if parent == directory:
            return None
        directory = parent


def merge_style(style_fname, extra_rules):
    """Return `style_fname` with `extra_rules` layered on top, as YAML text."""

    key_re = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*):')

    def blocks(lines):
        result = []
        for line in lines:
            line = line.rstrip()
            if not line or line.lstrip().startswith('#') or line.startswith('---'):
                continue
            match = key_re.match(line)
            if match:
                result.append((match.group(1), [line]))
            elif result:
                result[-1][1].append(line)
        return result

    with open(style_fname, 'r') as f:
        base = blocks(f.readlines())
    extra = blocks(extra_rules)
    overrides = dict(extra)

    merged = []
    for key, lines in base:
        merged.extend(overrides.pop(key, lines) if key in overrides else lines)
    for key, lines in extra:
        if key in overrides:
            merged.extend(lines)

    return '\n'.join(merged) + '\n'


def run_clang_format(sources, style=None, check_only=False):
    """Run clang-format over `sources`, optionally with an explicit style file."""

    if not sources:
        return True

    command = ['clang-format', '--dry-run', '--Werror'] if check_only else ['clang-format', '-i']
    if style:
        command.append('--style=file:{}'.format(style))
    command.extend(sources)
    return subprocess.run(command).returncode == 0


def format_c_sources(sources, check_only=False):
    """Format C/C++ sources with clang-format.

    Sources are normally handed to clang-format without a --style, so that it
    resolves the nearest `.clang-format` itself and the C++ backends keep their
    own style. Our own C headers are the exception: they get the base style
    plus HEADER_STYLE_RULES.
    """

    if not sources:
        return True

    root_style = nearest_style_file(os.path.join(os.getcwd(), '.clang-format'))

    headers = []
    others = []
    for filename in sources:
        # only our own headers, i.e. those that a subdirectory .clang-format
        # (the C++ backends) has not claimed for a different language
        if filename.endswith('.h') and nearest_style_file(filename) == root_style:
            headers.append(filename)
        else:
            others.append(filename)

    ok = run_clang_format(others, check_only=check_only)

    if headers:
        with tempfile.NamedTemporaryFile(mode='w', suffix='.clang-format') as fp:
            fp.write(merge_style(root_style, HEADER_STYLE_RULES))
            fp.flush()
            ok = run_clang_format(headers, style=fp.name, check_only=check_only) and ok

    return ok


def format_python_sources(sources, check_only=False):
    """Format Python sources with Black."""

    if not sources:
        return True

    command = [
        'black',
        '-S',  # no string normalization
        '-l',
        '100',  # line length
        '-t',
        'py311',  # minimum Python target
    ]
    if check_only:
        command.append('--check')
    command.extend(sources)
    return subprocess.run(command).returncode == 0


def collect_sources(current_dir, locations):
    """Return (c_sources, py_sources) below the given locations."""

    c_sources = []
    py_sources = []

    for location in locations:
        path = os.path.join(current_dir, location)
        if os.path.isfile(path):
            candidates = [path]
        elif os.path.isdir(path):
            candidates = glob(path + '/**/*', recursive=True)
        else:
            print('Skipping `{}`: no such file or directory.'.format(location), file=sys.stderr)
            continue

        for filename in candidates:
            if not os.path.isfile(filename):
                continue
            if any(fnmatch.fnmatch(filename, pattern) for pattern in EXCLUDE_MATCH):
                continue

            if filename.endswith(C_LIKE_SUFFIXES):
                c_sources.append(filename)
            elif filename.endswith('.py'):
                py_sources.append(filename)

    return sorted(set(c_sources)), sorted(set(py_sources))


def run(current_dir, args):
    if not check_tool('clang-format', r'version (\d+)', MIN_CLANG_FORMAT_VERSION):
        return 1
    if not check_tool('black', r'black, (\d+)', MIN_BLACK_VERSION):
        return 1

    locations = args.paths if args.paths else INCLUDE_LOCATIONS
    c_sources, py_sources = collect_sources(current_dir, locations)

    if not c_sources and not py_sources:
        print('Nothing to format.', file=sys.stderr)
        return 1

    ok = format_python_sources(py_sources, check_only=args.check)
    ok = format_c_sources(c_sources, check_only=args.check) and ok

    if args.check and not ok:
        print(
            '\nSome files are not formatted. Run ./autoformat.py to fix them.',
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Format PackageKit source code in-place.')
    parser.add_argument(
        '--check',
        action='store_true',
        help='do not modify anything, just report files that need formatting',
    )
    parser.add_argument(
        'paths',
        nargs='*',
        help='only format these files/directories (default: the whole tree)',
    )
    args = parser.parse_args()

    thisfile = __file__
    if not os.path.isabs(thisfile):
        thisfile = os.path.normpath(os.path.join(os.getcwd(), thisfile))
    thisdir = os.path.normpath(os.path.dirname(thisfile))

    # make explicit paths absolute before we chdir away from the caller's cwd
    args.paths = [os.path.abspath(p) for p in args.paths]
    os.chdir(thisdir)

    sys.exit(run(thisdir, args))
