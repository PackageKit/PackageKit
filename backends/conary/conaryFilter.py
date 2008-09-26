#!/usr/bin/python
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
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

# Copyright (C) 2008
#    Richard Hughes <richard@hughsie.com>

# imports
from packagekit.filter import *

import re

class ConaryFilter(PackagekitFilter):

    def _pkg_get_unique(self, pkg):
        '''
        Return a unique string for the package
        '''
        return "%s-%s.%s" % (pkg[0], pkg[1], pkg[2])

    def _pkg_is_devel(self, pkg):
        '''
        Return if the package is development.
        '''
        regex = re.compile(r'(:devel)')
        return regex.search(pkg.name)

