#!/usr/bin/python
#
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import sys

from yumBackend import PackageKitYumBackend
filters = sys.argv[1]
packages = sys.argv[2:]
backend = PackageKitYumBackend(sys.argv[2:])
backend.resolve(filters,packages)
backend.unLock()
sys.exit(0)
