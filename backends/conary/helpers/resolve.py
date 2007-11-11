#!/usr/bin/python
#
# Copyright (C) 2007 Elliot Peele <elliot@bentlogic.net>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import sys
from conaryBackend import PackageKitConaryBackend

filter = sys.argv[1]
package = sys.argv[2]
backend = PackageKitConaryBackend(sys.argv[1:])
backend.resolve(filter, package)
sys.exit(0)
