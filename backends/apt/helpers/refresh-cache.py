#!/usr/bin/python
#
# Apt refresh-cache handler. Modified from the yum handler
#
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
# Copyright (C) 2007 Red Hat Inc, Seth Vidal <skvidal@fedoraproject.org>
# Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import sys

from aptBackend import PackageKitAptBackend

backend = PackageKitAptBackend(sys.argv[1:])
backend.refresh_cache()
sys.exit(0)
