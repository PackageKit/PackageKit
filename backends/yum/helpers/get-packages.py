#!/usr/bin/python
#
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
# Copyright (C) 2007 Red Hat Inc, Seth Vidal <skvidal@fedoraproject.org>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import sys

options = sys.argv[1]
if len(sys.argv) > 2:
    show_desc = sys.argv[2]
else:
    show_desc = 'no'
from yumBackend import PackageKitYumBackend

backend = PackageKitYumBackend(sys.argv[1:],lock=False)
backend.get_packages(options,show_desc)
sys.exit(0)
