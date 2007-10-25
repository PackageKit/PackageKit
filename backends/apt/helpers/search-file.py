#!/usr/bin/python
#
# Copyright (C) 2007 Ali Sabil <ali.sabil@gmail.com>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import sys

options = sys.argv[1]
searchlist = sys.argv[2]

from aptBackend import PackageKitAptBackend

backend = PackageKitAptBackend(sys.argv[1:])
backend.search_file(options,searchlist)
sys.exit(0)
