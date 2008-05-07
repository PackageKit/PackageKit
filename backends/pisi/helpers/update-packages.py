#!/usr/bin/python
# -*- coding: utf-8 -*-
#
# Copyright (C) 2007 S.Çağlar Onur <caglar@pardus.org.tr>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import sys
import pisiBackend

backend = pisiBackend.PackageKitPisiBackend(sys.argv[1:])
backend.update_packages(sys.argv[1])

sys.exit()
