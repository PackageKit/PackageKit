#!/bin/sh
#
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

echo -n "Distro version:       "
cat /etc/*-release | uniq

echo -n "PackageKit version:   "
pkcon --version

echo "PackageKit Process Information:"
ps aux --forest | grep packagekitd | grep -v grep

