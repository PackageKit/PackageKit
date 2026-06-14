#!/bin/sh
set -e

if [ -d "build" ]; then
  rm build -rf
fi
set -x

meson setup build \
    -Dmaintainer=true \
    -Dlocal_checkout=true \
    -Dlegacy_tools=true \
    -Ddaemon_tests=true \
    $@

DUMMY_DESTDIR=/tmp/install-root/
rm -rf $DUMMY_DESTDIR

#
# Build & Install
#
ninja -C build
DESTDIR=$DUMMY_DESTDIR ninja -C build install

#
# Run tests
#

# Ensure we have a D-Bus daemon running
mkdir -p /run/dbus/
dbus-daemon --system --print-address

# Run the regular test suite (the PK daemon tests are run separately
# below, as they need extra setup and must run as root)
meson test -C build \
    --no-suite daemon \
    -v \
    --print-errorlogs

# Run the daemon integration suite. It needs the D-Bus and polkit policy
# installed to their system paths so the bus and polkitd honour them; the test
# wrapper starts a private system bus and polkitd on demand (and tears them
# down) if none are running. It runs as root so packagekitd can own its name.
install -Dm644 build/data/org.freedesktop.PackageKit.conf \
    /usr/share/dbus-1/system.d/org.freedesktop.PackageKit.conf
install -Dm644 build/policy/org.freedesktop.packagekit.policy \
    /usr/share/polkit-1/actions/org.freedesktop.packagekit.policy
install -Dm644 policy/org.freedesktop.packagekit.rules \
    /usr/share/polkit-1/rules.d/org.freedesktop.packagekit.rules

meson test -C build \
    --suite daemon \
    -v \
    --print-errorlogs
