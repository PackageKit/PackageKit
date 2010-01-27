#!/usr/bin/env python
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# Copyright (C) 2008
#    Sebastian Heinlein <sebi@glatzor.de>

"""
Allows to start the apt2 backend in a profling mode
"""

__author__ = "Sebastian Heinlein <devel@glatzor.de>"


import hotshot
import sys

from aptDBUSBackend import main

if len(sys.argv) == 2:
    profile = sys.argv[1]
else:
    profile = "profile"

prof = hotshot.Profile(profile)
print prof.runcall(main)
prof.close()
