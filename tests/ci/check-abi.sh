#!/usr/bin/env bash
set -e

if [ -d "build" ]; then
  rm build -rf
fi
set -x

meson setup build \
    -Dlocal_checkout=true \
    $@

DUMMY_DESTDIR=/tmp/install-root/
rm -rf $DUMMY_DESTDIR

# Build & Install
ninja -C build
DESTDIR=$DUMMY_DESTDIR ninja -C build install

pushd original-version

if [ -d "build" ]; then
  rm build -rf
fi

meson setup build \
    -Dlocal_checkout=true \
    $@

DUMMY_ORIG_DESTDIR=/tmp/install-root-orig
rm -rf $DUMMY_ORIG_DESTDIR

# Build & Install
ninja -C build
DESTDIR=$DUMMY_ORIG_DESTDIR ninja -C build install

popd

abidiff \
    --headers-dir1 ${DUMMY_ORIG_DESTDIR}/usr/local/include/PackageKit/packagekit-glib2 \
    --headers-dir2 ${DUMMY_DESTDIR}/usr/local/include/PackageKit/packagekit-glib2 \
    --drop-private-types \
    --fail-no-debug-info \
    --no-added-syms \
    ${DUMMY_ORIG_DESTDIR}/usr/local/lib/x86_64-linux-gnu/libpackagekit-glib2.so \
    ${DUMMY_DESTDIR}/usr/local/lib/x86_64-linux-gnu/libpackagekit-glib2.so
