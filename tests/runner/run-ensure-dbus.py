#!/usr/bin/env python3
#
# Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

"""Small helper to ensure a test that needs it has a running D-Bus instance"""

import os
import sys
import shutil
import argparse
import subprocess

from utils import terminate, system_bus_reachable, start_system_bus_if_needed


def main():
    parser = argparse.ArgumentParser(
        description='Run a PackageKit test and ensure D-Bus is available.'
    )
    parser.add_argument('test_binary')
    args = parser.parse_args()

    bus_proc = None
    bus_tmpdir = None

    if system_bus_reachable():
        os.execv(args.test_binary, sys.argv[1:])

    try:
        bus_proc, bus_tmpdir = start_system_bus_if_needed(None)

        proc = subprocess.run([args.test_binary], check=False)
        if proc.returncode != 0:
            return proc.returncode
    finally:
        terminate(bus_proc)
        if bus_tmpdir is not None:
            shutil.rmtree(bus_tmpdir, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
