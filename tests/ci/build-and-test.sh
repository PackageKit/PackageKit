#!/bin/sh
set -e

if [ -d "build" ]; then
  rm build -rf
fi
set -x

meson build \
    -Dlocal_checkout=true \
    -Ddaemon_tests=true \
    $@

# Build & Install
ninja -C build
DESTDIR=/tmp/install_root/ ninja -C build install

# Run tests
mkdir -p /run/dbus/
dbus-daemon --system --print-address
meson test -C build -v --print-errorlogs
