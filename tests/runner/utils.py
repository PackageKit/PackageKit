#
# Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

import os
import time
import shutil
import tempfile
import subprocess


def system_bus_reachable():
    """Whether a system D-Bus daemon looks reachable."""
    if os.environ.get('DBUS_SYSTEM_BUS_ADDRESS'):
        return True
    return os.path.exists('/run/dbus/system_bus_socket') or os.path.exists(
        '/var/run/dbus/system_bus_socket'
    )


def _name_has_owner(name):
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
        print('Reusing the already-running system bus.', flush=True)
        return None, None
    tmpdir = tempfile.mkdtemp(prefix='pk-test-bus-')
    # polkitd drops privileges to the unprivileged 'polkitd' system user, so the
    # directory holding the bus socket must be traversable by it to connect (and
    # claim org.freedesktop.PolicyKit1); mkdtemp() creates it as 0700.
    os.chmod(tmpdir, 0o755)
    socket_path = os.path.join(tmpdir, 'system_bus_socket')
    address = 'unix:path=' + socket_path
    print('No system bus found; starting a private one at {}.'.format(address), flush=True)
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
    if _name_has_owner('org.freedesktop.PolicyKit1'):
        print('Reusing the already-running polkit authority.', flush=True)
        return None
    polkitd = shutil.which('polkitd')
    for candidate in ('/usr/lib/polkit-1/polkitd', '/usr/libexec/polkit-1/polkitd'):
        if polkitd:
            break
        if os.path.exists(candidate):
            polkitd = candidate
    if not polkitd:
        print('Warning: polkitd not found; the daemon will fail to reach an authority.', flush=True)
        return None
    print('No polkit authority found; starting {}.'.format(polkitd), flush=True)
    proc = subprocess.Popen([polkitd, '--no-debug'], stdout=out, stderr=subprocess.STDOUT)
    for _ in range(40):
        if _name_has_owner('org.freedesktop.PolicyKit1'):
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
