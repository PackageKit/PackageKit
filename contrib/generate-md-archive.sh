#!/bin/bash
# Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

# run this as non-root

PREFIX=`realpath "./root"`
rm -rf ${PREFIX}
mkdir -p ${PREFIX}/etc/yum.repos.d
cp /etc/yum.repos.d/fedora.repo ${PREFIX}/etc/yum.repos.d/
cp /etc/yum.repos.d/fedora-updates.repo ${PREFIX}/etc/yum.repos.d/
cp /etc/yum.repos.d/fedora-updates-testing.repo ${PREFIX}/etc/yum.repos.d/
[ -x /usr/bin/rpm ] && rpm --root=${PREFIX} --initdb
DESTDIR=${PREFIX} /usr/libexec/packagekit-direct repo-set-data fedora enabled 1
DESTDIR=${PREFIX} /usr/libexec/packagekit-direct repo-set-data updates enabled 1
DESTDIR=${PREFIX} /usr/libexec/packagekit-direct repo-set-data fedora-debuginfo enabled 0
DESTDIR=${PREFIX} /usr/libexec/packagekit-direct repo-set-data updates-debuginfo enabled 0
DESTDIR=${PREFIX} /usr/libexec/packagekit-direct repo-set-data updates-testing-debuginfo enabled 0
DESTDIR=${PREFIX} /usr/libexec/packagekit-direct repo-set-data updates-testing enabled 0
DESTDIR=${PREFIX} /usr/libexec/packagekit-direct refresh
rm -rf ${PREFIX}/etc
rm -rf ${PREFIX}/var/run
rm -rf ${PREFIX}/var/lib
# this is so we can use it again at runtime
mv ${PREFIX}/var/cache ${PREFIX}/var/share
mv ${PREFIX}/var ${PREFIX}/usr
cd ${PREFIX} && tar -cf ../cached-metadata.tar usr
rm -rf ${PREFIX}
echo "now ship cached-metadata.tar in your package!"
