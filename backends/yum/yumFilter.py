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

# imports
from packagekit.enums import *
from packagekit.filter import *

import rpmUtils
import re

GUI_KEYS = re.compile(r'(qt)|(gtk)')

class YumFilter(PackagekitFilter):

    def __init__(self, fltlist="none"):
        PackagekitFilter.__init__(self, fltlist)
        basearch = rpmUtils.arch.getBaseArch()
        if basearch == 'i386':
            self.basearch_list = ['i386', 'i486', 'i586', 'i686']
        else:
            self.basearch_list = [basearch]
        self.basearch_list.append('noarch')

    def _is_main_package(self, repo):
        if repo.endswith('-debuginfo'):
            return False
        if repo.endswith('-devel'):
            return False
        if repo.endswith('-libs'):
            return False
        return True

    def _basename_filter(self, package_list):
        '''
        Filter the list so that the number of packages are reduced.
        This is done by only displaying gtk2 rather than gtk2-devel, gtk2-debuginfo, etc.
        This imlementation is done by comparing the SRPM name, and if not falling back
        to the first entry.
        We have to fall back else we don't emit packages where the SRPM does not produce a
        RPM with the same name, for instance, mono produces mono-core, mono-data and mono-winforms.
        @package_list: a (pkg, status) list of packages
        A new list is returned that has been filtered
        '''
        base_list = []
        output_list = []
        base_list_already_got = []

        #find out the srpm name and add to a new array of compound data
        for (pkg, status) in package_list:
            if pkg.sourcerpm:
                base = rpmUtils.miscutils.splitFilename(pkg.sourcerpm)[0]
                base_list.append ((pkg, status, base, pkg.version))
            else:
                base_list.append ((pkg, status, 'nosrpm', pkg.version))

        #find all the packages that match thier basename name (done seporately so we get the "best" match)
        for (pkg, status, base, version) in base_list:
            if base == pkg.name and (base, version) not in base_list_already_got:
                output_list.append((pkg, status))
                base_list_already_got.append ((base, version))

        #for all the ones not yet got, can we match against a non devel match?
        for (pkg, status, base, version) in base_list:
            if (base, version) not in base_list_already_got:
                if self._is_main_package(pkg.name):
                    output_list.append((pkg, status))
                    base_list_already_got.append ((base, version))

        #add the remainder of the packages, which should just be the single debuginfo's
        for (pkg, status, base, version) in base_list:
            if (base, version) not in base_list_already_got:
                output_list.append((pkg, status))
                base_list_already_got.append ((base, version))
        return output_list

    def _do_newest_filtering(self, pkglist):
        '''
        Only return the newest package for each name.arch
        '''
        newest = {}
        for pkg, state in pkglist:
            # only key on name and not arch
            inst = self._pkg_is_installed(pkg)
            key = (pkg.name, pkg.arch, inst)

            # we've already come across this package
            if key in newest:

                # the current package is older version than the one we have stored
                if pkg.verCMP(newest[key][0]) < 0:
                    continue

                # the current package is the same version, but the repository has a lower priority
                if pkg.verCMP(newest[key][0]) == 0 and \
                   pkg.repo >= newest[key][0].repo:
                    continue

                # the current package is newer than what we have stored or the repository has a higher priority, so nuke the old package
                del newest[key]

            newest[key] = (pkg, state)
        return newest.values()

    def post_process(self):
        ''' do filtering we couldn't do when generating the list '''

        if FILTER_BASENAME in self.fltlist:
            self.package_list = self._basename_filter(self.package_list)

        if FILTER_NEWEST in self.fltlist:
            self.package_list = self._do_newest_filtering(self.package_list)

        return self.package_list

    def _pkg_get_unique(self, pkg):
        '''
        Return a unique string for the package
        '''
        return "%s-%s:%s-%s.%s" % (pkg.name, pkg.epoch, pkg.version, pkg.release, pkg.arch)

    def _pkg_get_name(self, pkg):
        '''
        Returns the name of the package used for duplicate filtering
        '''
        return pkg.name

    def _pkg_is_installed(self, pkg):
        '''
        Return if the package is installed.
        '''
        return pkg.repo.id == 'installed'

    def _pkg_is_devel(self, pkg):
        '''
        Return if the package is development.
        '''
        if pkg.name.endswith('-devel'):
            return True
        if pkg.name.endswith('-debuginfo'):
            return True
        if pkg.name.endswith('-static'):
            return True
        if pkg.name.endswith('-libs'):
            return True
        return False

    def _pkg_is_gui(self, pkg):
        '''
        Return if the package is a GUI program.
        '''
        for req in pkg.requires:
            reqname = req[0]
            if GUI_KEYS.search(reqname):
                return True
        return False

    def _pkg_is_arch(self, pkg):
        '''
        Return if the package is native arch.
        '''
        if pkg.arch in self.basearch_list:
            return True
        return False

    def _pkg_is_free(self, pkg):
        '''
        Return if the package is free software.
        '''
        return self.check_license_field(pkg.license)

