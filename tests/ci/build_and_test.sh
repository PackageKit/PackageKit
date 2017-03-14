#!/bin/sh
set -e

./autogen.sh $@

# Build, Test & Install
make
make install DEST=/tmp/install_root/
