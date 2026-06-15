#!/usr/bin/env python3
#
# Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

"""Run the PackageKit daemon tests.

The pk-test-e2e binary exercises the client library against a running
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
import pty
import sys
import fcntl
import select
import shutil
import termios
import argparse
import tempfile
import subprocess

from utils import (
    terminate,
    system_bus_reachable,
    start_polkitd_if_needed,
    start_system_bus_if_needed,
)

# meson interprets this exit code as "test skipped"
EXIT_SKIP = 77

PK_BUS_NAME = 'org.freedesktop.PackageKit'
DBUS_CONF = '/usr/share/dbus-1/system.d/org.freedesktop.PackageKit.conf'
POLKIT_POLICY = '/usr/share/polkit-1/actions/org.freedesktop.packagekit.policy'


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


def wait_for_bus_name(timeout=30):
    """Block until packagekitd owns its name on the system bus."""
    try:
        subprocess.run(
            ['gdbus', 'wait', '--system', '--timeout', str(timeout), PK_BUS_NAME], check=True
        )
        return True
    except subprocess.CalledProcessError:
        return False


def _answer_for_prompt(prompt):
    """Pick the answer for an interactive yes/no prompt.

    Some subtests script specific keystrokes ("press N" expects a 'no', the
    rest a default 'yes'); every real confirmation (untrusted software, EULAs,
    "Proceed with changes?", ...) is accepted.
    """
    if 'press N' in prompt:
        return 'N\n'
    if 'press Y' in prompt:
        return 'Y\n'
    if 'press enter' in prompt:
        return '\n'
    return 'y\n'


def run_test_with_auto_answers(test_binary, env):
    """Run the test binary on a pseudo-terminal, answering its prompts.

    pk_console_get_prompt() reads from the controlling terminal (/dev/tty), so the test cannot
    be driven by a plain pipe. We give it a PTY as its controlling terminal, forward everything
    it prints to our stdout (so meson still sees the TAP stream) and write a canned answer whenever a
    "... [Y/n]"/"... [N/y]" prompt appears.

    Returns the test's exit code.
    """
    master, slave = pty.openpty()

    def _make_controlling_tty():
        # Runs in the child after its std fds have been pointed at the slave
        # PTY; claim it as the controlling terminal so /dev/tty resolves here.
        fcntl.ioctl(0, termios.TIOCSCTTY, 0)

    proc = subprocess.Popen(
        [test_binary],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        env=env,
        start_new_session=True,
        preexec_fn=_make_controlling_tty,
    )
    os.close(slave)

    line = ''
    try:
        while True:
            try:
                readable, _, _ = select.select([master], [], [], 1.0)
            except (OSError, select.error):
                break
            if master in readable:
                try:
                    data = os.read(master, 4096)
                except OSError:
                    break  # slave closed: the child has exited
                if not data:
                    break
                text = data.decode('utf-8', 'replace')
                sys.stdout.write(text)
                sys.stdout.flush()

                # Track the current (unterminated) line to spot a prompt.
                line = (line + text).rsplit('\n', 1)[-1]
                stripped = line.rstrip()
                if stripped.endswith('[Y/n]') or stripped.endswith('[N/y]'):
                    os.write(master, _answer_for_prompt(line).encode('utf-8'))
                    line = ''
            elif proc.poll() is not None:
                break
    finally:
        os.close(master)

    return proc.wait()


def main():
    # Line-buffer stdout so our progress messages stay correctly interleaved
    sys.stdout.reconfigure(line_buffering=True)

    parser = argparse.ArgumentParser(description='Run the PackageKit daemon test.')
    parser.add_argument('--daemon', required=True, help='path to the packagekitd binary')
    parser.add_argument('--test', required=True, help='path to the pk-test-e2e binary')
    args = parser.parse_args()

    reasons = check_prerequisites()
    if reasons:
        print('Skipping daemon test; prerequisites are not met:\n')
        for reason in reasons:
            print('  * ' + reason)
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

        print('Launching {} with the dummy backend...'.format(args.daemon))
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

        print('Daemon is up; running {}...'.format(args.test))

        # Force a predictable locale so the (otherwise translated) prompt text stays in English
        test_env = dict(os.environ, LC_ALL='C.UTF-8', LANG='C.UTF-8')

        returncode = run_test_with_auto_answers(args.test, test_env)
        if returncode != 0:
            dump_daemon_log()
        return returncode
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
