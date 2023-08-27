#!/bin/sh
set -e

if [ -d "build" ]; then
  rm build -rf
fi
meson build \
    -Dlocal_checkout=true \
    -Ddaemon_tests=true \
    $@

# Build, Test & Install
ninja -C build
meson test -C build --print-errorlogs
DESTDIR=/tmp/install_root/ ninja -C build install
