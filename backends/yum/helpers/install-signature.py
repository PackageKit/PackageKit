#!/usr/bin/python
#
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import sys

sigtype = sys.argv[1]
key_id = sys.argv[2]
package_id = sys.argv[3]

from yumBackend import PackageKitYumBackend

backend = PackageKitYumBackend(sys.argv[1:],lock=False)
backend.install_signature(sigtype,key_id,package_id)
sys.exit(0)

