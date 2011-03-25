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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# Copyright (C) 2008
#    Richard Hughes <richard@hughsie.com>
#    Andres Vargas <zodman@foresightlinux.org>

# imports
from packagekit.filter import *

import re
from pkConaryLog import log
from conarypk import ConaryPk, get_arch

class ConaryFilter(PackagekitFilter):

    def check_installed(self):
        if  "installed" or "none" in self.fltlist:
            log.debug("check_installed")
            return True
        else:
            return False
    def check_available(self):
        if "~installed" or "none" in self.fltlist:
            log.debug("check_available")
            return True
        else:
            return False

    def _pkg_get_name(self, pkg):
        '''
        Returns the name of the package used for duplicate filtering
        '''
        name,version,flavor = pkg.get("trove")
        return name

    def _pkg_get_unique(self, pkg):
        '''
        Return a unique string for the package
        '''
        name,version,flavor = pkg.get("trove")
        ver = version.trailingRevision()
        fl = get_arch(flavor)
        return "%s-%s.%s" % (name,ver,fl)

    def _pkg_is_installed(self, pkg):
        '''
        Return if the packages are installed
        '''
        unique = self._pkg_get_unique(pkg)
        if unique in self.installed_unique :
            return True
        else:
            return False

