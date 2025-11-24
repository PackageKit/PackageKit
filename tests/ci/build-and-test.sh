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

# Build & Install
ninja -C build
DESTDIR=$DUMMY_DESTDIR ninja -C build install

# Run tests
mkdir -p /run/dbus/
dbus-daemon --system --print-address
meson test -C build \
    -v \
    --print-errorlogs
