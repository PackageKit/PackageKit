#!/bin/sh
set -e

if [ -d "build" ]; then
  meson --wipe build $@
else
  meson build $@
fi

# Build, Test & Install
ninja -C build
ninja -C build test
DEST=/tmp/install_root/ ninja -C build install
