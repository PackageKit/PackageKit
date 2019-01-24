#!/bin/sh
set -e

if [ -d "build" ]; then
  rm build -rf
fi
meson build -Dlocal_checkout=true -Ddaemon_tests=false $@

# Build, Test & Install
ninja -C build
ninja -C build test
DEST=/tmp/install_root/ ninja -C build install
