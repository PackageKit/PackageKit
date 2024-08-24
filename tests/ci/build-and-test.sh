#!/bin/sh
set -e

if [ -d "build" ]; then
  rm build -rf
fi
set -x

meson setup build \
    -Dlocal_checkout=true \
    -Ddaemon_tests=true \
    $@

# Build & Install
ninja -C build
INSTALL_DIR=/tmp/install_root/

if [ -d "$INSTALL_DIR" ]; then
  rm $INSTALL_DIR -rf
fi

DESTDIR=$INSTALL_DIR ninja -C build install

# Run tests
mkdir -p /run/dbus/
dbus-daemon --system --print-address
meson test -C build -v --print-errorlogs
