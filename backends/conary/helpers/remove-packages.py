#!/usr/bin/python
#
# Copyright (C) 2007 Ken VanDine <ken@vandine.org>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import sys
from conaryBackend import PackageKitConaryBackend

allowDeps = sys.argv[1]
package_ids = sys.argv[2:]
backend = PackageKitConaryBackend(sys.argv[1:])
backend.remove_packages(allowDeps, package_ids)
sys.exit(0)
