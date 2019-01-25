#!/bin/sh
set -e

meson build $@

# Build, Test & Install
ninja -C build
ninja -C build test
DEST=/tmp/install_root/ ninja -C build install
