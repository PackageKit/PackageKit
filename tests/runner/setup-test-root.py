#!/usr/bin/env python3
#
# Copyright (C) 2025-2026 Matthias Klumpp <matthias@tenstral.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

"""Lay out a fake root directory for running PackageKit from the build tree.

The daemon is pointed at this directory with the RootDir key of the test
configuration and then treats it as "/": backend modules are loaded from the
installed module directory below it, and all state is written below it, so a
test run never touches the host system.

The backend modules built in the tree are symlinked into place; the state
directories are created empty.
"""

import os
import sys
import argparse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, help='the fake root directory to populate')
    parser.add_argument(
        '--backend-dir',
        required=True,
        help='the installed backend module directory, e.g. /usr/lib/packagekit/backends',
    )
    parser.add_argument('--stamp', required=True, help='stamp file to touch when done')
    parser.add_argument(
        '--link',
        action='append',
        default=[],
        metavar='INSTALLED=SOURCE',
        help='symlink INSTALLED (an installed path, e.g. /usr/share/PackageKit/helpers/x) '
        'inside the root to SOURCE',
    )
    parser.add_argument('modules', nargs='*', help='backend modules to link into the root')
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    backend_dir = os.path.join(root, args.backend_dir.lstrip('/'))
    os.makedirs(backend_dir, exist_ok=True)
    # drop links to modules that no longer exist, they would shadow a manifest
    wanted = {os.path.basename(m) for m in args.modules}
    for name in os.listdir(backend_dir):
        path = os.path.join(backend_dir, name)
        if os.path.islink(path) and name not in wanted:
            os.unlink(path)
    for module in args.modules:
        module = os.path.abspath(module)
        link = os.path.join(backend_dir, os.path.basename(module))
        if os.path.islink(link) or os.path.exists(link):
            os.unlink(link)
        os.symlink(module, link)

    for entry in args.link:
        installed, source = entry.split('=', 1)
        link = os.path.join(root, installed.lstrip('/'))
        os.makedirs(os.path.dirname(link), exist_ok=True)
        if os.path.islink(link) or os.path.exists(link):
            os.unlink(link)
        os.symlink(os.path.abspath(source), link)

    # the daemon expects its state directory to exist, as it does on an installed system
    os.makedirs(os.path.join(root, 'var', 'lib', 'PackageKit'), exist_ok=True)

    with open(args.stamp, 'w') as f:
        f.write('')
    return 0


if __name__ == '__main__':
    sys.exit(main())
