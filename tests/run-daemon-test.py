#!/usr/bin/env python3
#
# Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
#
# Licensed under the GNU Lesser General Public License Version 2.1
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA

"""Run the PackageKit daemon tests.

The pk-test-daemon binary exercises the client library against a running
PackageKit daemon (using the dummy backend) over the system D-Bus.
This script forwards the test binary's output straight to stdout/stderr
and keeps the daemon's own output in a buffer that is only printed
when something goes wrong.

The script reuses a running system bus and polkit authority if present, and
otherwise starts private ones just for the test run (tearing them down again
afterwards).

Prerequisites:
  * /usr/share/dbus-1/system.d/org.freedesktop.PackageKit.conf  (D-Bus policy)
  * /usr/share/polkit-1/actions/org.freedesktop.packagekit.policy  (polkit actions)
  * run as the configured PackageKit user (root by default)

When a prerequisite is missing the script exits with code 77 (meson's "skip"
code) instead of failing, so it stays out of the way on machines that cannot
run it.
"""

import os
import sys
import time
import shutil
import argparse
import tempfile
import subprocess

# meson interprets this exit code as "test skipped"
EXIT_SKIP = 77

PK_BUS_NAME = 'org.freedesktop.PackageKit'
DBUS_CONF = '/usr/share/dbus-1/system.d/org.freedesktop.PackageKit.conf'
POLKIT_POLICY = '/usr/share/polkit-1/actions/org.freedesktop.packagekit.policy'


def info(message):
    """Print a progress message."""
    print(message, flush=True)


def system_bus_reachable():
    """Whether a system D-Bus daemon looks reachable."""
    if os.environ.get('DBUS_SYSTEM_BUS_ADDRESS'):
        return True
    return os.path.exists('/run/dbus/system_bus_socket') or os.path.exists(
        '/var/run/dbus/system_bus_socket'
    )


def check_prerequisites():
    """Return a list of human-readable reasons the test cannot run, if any."""
    reasons = []
    if not os.path.exists(DBUS_CONF):
        reasons.append(
            'Missing D-Bus policy {!r}. Install it with:\n'
            '    sudo cp data/org.freedesktop.PackageKit.conf '
            '/usr/share/dbus-1/system.d/'.format(DBUS_CONF)
        )
    if not os.path.exists(POLKIT_POLICY):
        reasons.append(
            'Missing polkit policy {!r}. Install it with:\n'
            '    sudo cp policy/org.freedesktop.packagekit.policy '
            '/usr/share/polkit-1/actions/'.format(POLKIT_POLICY)
        )
    if not shutil.which('gdbus'):
        reasons.append('gdbus is missing. Please install it!')
    if not system_bus_reachable() and shutil.which('dbus-daemon') is None:
        reasons.append('No system bus running and dbus-daemon is not available ' 'to start one.')
    return reasons


def name_has_owner(name):
    """Whether a bus name currently has an owner on the system bus."""
    res = subprocess.run(
        [
            'gdbus',
            'call',
            '--system',
            '--dest',
            'org.freedesktop.DBus',
            '--object-path',
            '/org/freedesktop/DBus',
            '--method',
            'org.freedesktop.DBus.NameHasOwner',
            name,
        ],
        capture_output=True,
        text=True,
    )
    return res.returncode == 0 and 'true' in res.stdout


def start_system_bus_if_needed(out):
    """Start a private system bus if none is reachable.

    Returns (proc, tmpdir): proc is the dbus-daemon we launched (or None if an
    existing bus is reused), tmpdir is the temporary runtime dir to clean up.
    """
    if system_bus_reachable():
        info('Reusing the already-running system bus.')
        return None, None
    tmpdir = tempfile.mkdtemp(prefix='pk-test-bus-')
    socket_path = os.path.join(tmpdir, 'system_bus_socket')
    address = 'unix:path=' + socket_path
    info('No system bus found; starting a private one at {}.'.format(address))
    proc = subprocess.Popen(
        ['dbus-daemon', '--system', '--nofork', '--nopidfile', '--address=' + address],
        stdout=out,
        stderr=subprocess.STDOUT,
    )
    os.environ['DBUS_SYSTEM_BUS_ADDRESS'] = address
    for _ in range(40):
        if os.path.exists(socket_path):
            break
        time.sleep(0.25)
    return proc, tmpdir


def start_polkitd_if_needed(out):
    """Start polkitd only if no polkit authority is on the bus. Returns the proc
    we launched, or None if an existing authority is reused or none can be started.
    """
    if name_has_owner('org.freedesktop.PolicyKit1'):
        info('Reusing the already-running polkit authority.')
        return None
    polkitd = shutil.which('polkitd')
    for candidate in ('/usr/lib/polkit-1/polkitd', '/usr/libexec/polkit-1/polkitd'):
        if polkitd:
            break
        if os.path.exists(candidate):
            polkitd = candidate
    if not polkitd:
        info('Warning: polkitd not found; the daemon will fail to reach an authority.')
        return None
    info('No polkit authority found; starting {}.'.format(polkitd))
    proc = subprocess.Popen([polkitd, '--no-debug'], stdout=out, stderr=subprocess.STDOUT)
    for _ in range(40):
        if name_has_owner('org.freedesktop.PolicyKit1'):
            break
        time.sleep(0.25)
    return proc


def terminate(proc):
    """Best-effort terminate-then-kill of a child process we started."""
    if proc is None or proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def wait_for_bus_name(timeout=30):
    """Block until packagekitd owns its name on the system bus."""
    try:
        subprocess.run(
            ['gdbus', 'wait', '--system', '--timeout', str(timeout), PK_BUS_NAME], check=True
        )
        return True
    except subprocess.CalledProcessError:
        return False


def main():
    parser = argparse.ArgumentParser(description='Run the PackageKit daemon test.')
    parser.add_argument('--daemon', required=True, help='path to the packagekitd binary')
    parser.add_argument('--test-binary', required=True, help='path to the pk-test-daemon binary')
    args = parser.parse_args()

    reasons = check_prerequisites()
    if reasons:
        print('Skipping daemon test; prerequisites are not met:\n')
        for reason in reasons:
            print('  * ' + reason)
        print('\nSee tests/run-daemon-test.py for details.')
        return EXIT_SKIP

    if os.geteuid() != 0:
        # The default D-Bus policy only lets the PackageKit user (root) own the
        # name, so the test cannot meaningfully run otherwise. Skip rather than
        # fail, so a plain `meson test` stays green for unprivileged developers.
        print(
            'Skipping daemon test: must run as root so packagekitd can own '
            '{!r} on the system bus.'.format(PK_BUS_NAME)
        )
        return EXIT_SKIP

    # The local backend loader (PK_BUILD_LOCAL) resolves the backend module
    # relative to the build tree root, so launch the daemon from there.
    build_root = os.environ.get('MESON_BUILD_ROOT')
    if not build_root:
        build_root = os.path.dirname(os.path.dirname(os.path.abspath(args.daemon)))

    daemon = None
    bus_proc = None
    bus_tmpdir = None
    polkitd_proc = None
    # Keep the daemon output buffered and only print it on failure
    daemon_log = tempfile.TemporaryFile()

    def dump_daemon_log():
        daemon_log.flush()
        daemon_log.seek(0)
        data = daemon_log.read().decode('utf-8', 'replace')
        if data:
            sys.stderr.write('\n--- daemon output ---\n')
            sys.stderr.write(data)
            sys.stderr.write('--- end daemon output ---\n')
            sys.stderr.flush()

    try:
        # Reuse a running system bus and polkit authority if present; otherwise
        # spin up private ones just for this test run.
        bus_proc, bus_tmpdir = start_system_bus_if_needed(daemon_log)
        polkitd_proc = start_polkitd_if_needed(daemon_log)

        info('Launching {} with the dummy backend...'.format(args.daemon))
        daemon = subprocess.Popen(
            [args.daemon, '--verbose', '--disable-timer', '--keep-environment', '--backend=dummy'],
            cwd=build_root,
            stdout=daemon_log,
            stderr=subprocess.STDOUT,
        )

        if not wait_for_bus_name():
            print(
                'ERROR: packagekitd never claimed {!r} on the system ' 'bus.'.format(PK_BUS_NAME),
                file=sys.stderr,
            )
            if daemon.poll() is not None:
                print(
                    'The daemon exited early (code {}); common causes are not '
                    'running as the PackageKit user (root), a missing/'
                    'unreachable polkit authority, or the backend failing to '
                    'load.'.format(daemon.returncode),
                    file=sys.stderr,
                )
            dump_daemon_log()
            return 1

        info('Daemon is up; running {}...'.format(args.test_binary))

        result = subprocess.run([args.test_binary])
        if result.returncode != 0:
            dump_daemon_log()
        return result.returncode
    finally:
        # Tear down in reverse order, and only what we started ourselves
        terminate(daemon)
        terminate(polkitd_proc)
        terminate(bus_proc)
        if bus_tmpdir is not None:
            shutil.rmtree(bus_tmpdir, ignore_errors=True)
        daemon_log.close()


if __name__ == '__main__':
    sys.exit(main())
