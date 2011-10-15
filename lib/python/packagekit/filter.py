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
from .enums import *
from .package import PackagekitPackage

class PackagekitFilter(object, PackagekitPackage):

    def __init__(self, fltlist="none"):
        ''' save state '''
        self.fltlist = fltlist
        self.package_list = [] #we can't do emitting as found if we are post-processing
        self.installed_unique = {}

    def add_installed(self, pkgs):
        ''' add a list of packages that are already installed '''
        for pkg in pkgs:
            self.package_list.append((pkg, INFO_INSTALLED))

    def add_available(self, pkgs):
        ''' add a list of packages that are available '''
        for pkg in pkgs:
            self.package_list.append((pkg, INFO_AVAILABLE))

    def add_custom(self, pkg, info):
        ''' add a custom packages indervidually '''
        self.package_list.append((pkg, info))

    def _filter_base(self, pkg):
        ''' do extra filtering (gui, devel etc) '''
        for flt in self.fltlist:
            if flt in (FILTER_GUI, FILTER_NOT_GUI):
                if not self._do_gui_filtering(flt, pkg):
                    return False
            elif flt in (FILTER_DEVELOPMENT, FILTER_NOT_DEVELOPMENT):
                if not self._do_devel_filtering(flt, pkg):
                    return False
            elif flt in (FILTER_FREE, FILTER_NOT_FREE):
                if not self._do_free_filtering(flt, pkg):
                    return False
            elif flt in (FILTER_ARCH, FILTER_NOT_ARCH):
                if not self._do_arch_filtering(flt, pkg):
                    return False
        return True

    def _filter_installed(self, pkg):
        ''' do extra filtering (gui, devel etc) '''
        for flt in self.fltlist:
            if flt in (FILTER_INSTALLED, FILTER_NOT_INSTALLED):
                if not self._do_installed_filtering(flt, pkg):
                    return False
        return True

    def get_package_list(self):
        '''
        do filtering we couldn't do when generating the list
        '''

        # filter common things here like architecture
        # NOTE: we can't do installed and ~installed here as we need
        # this data for the newest and downgrade checks below
        package_list = self.package_list
        self.package_list = []
        for pkg, state in package_list:
            if self._filter_base(pkg):
                self.package_list.append((pkg, state))

        # check there are not available versions in the package list
        # that are older than the installed version
        package_list = self.package_list
        self.package_list = []
        for pkg, state in package_list:

            add = True;
            if state is INFO_AVAILABLE:
                for pkg_tmp, state_tmp in self.package_list:
                    if state_tmp is not INFO_INSTALLED:
                        continue
                    rc = self._pkg_compare(pkg, pkg_tmp)

                    # don't add if the same as the installed package
                    # or a downgrade to the existing installed package
                    if rc == 0 or rc == -1:
                        add = False
                        break

            if add:
                self.package_list.append((pkg, state))

        # filter installed state last
        package_list = self.package_list
        self.package_list = []
        for pkg, state in package_list:
            if self._filter_installed(pkg):
                self.package_list.append((pkg, state))

        # do the backend specific filtering
        return self.post_process()

    def post_process(self):
        '''
        do filtering we couldn't do when generating the list
        Needed to be implemented in a sub class
        '''
        return self.package_list

    def _pkg_compare(self, pkg1, pkg2):
        '''
        Returns a version comparison of the packages, where:
        -2 : pkg1 not comparable with pkg2
        -1 : pkg2 is newer than pkg1
         0 : pkg1 == pkg2
         1 : pkg1 is newer than pkg2
         2 : not implemented
        Needed to be implemented in a sub class
        '''
        return 2

    def _pkg_get_name(self, pkg):
        '''
        Returns the name of the package used for duplicate filtering
        Needed to be implemented in a sub class
        '''
        return None

    def _pkg_is_installed(self, pkg):
        '''
        Return if the package is installed.
        Needed to be implemented in a sub class
        '''
        return True

    def _pkg_is_devel(self, pkg):
        '''
        Return if the package is development.
        Needed to be implemented in a sub class
        '''
        return True

    def _pkg_is_gui(self, pkg):
        '''
        Return if the package is a GUI program.
        Needed to be implemented in a sub class
        '''
        return True

    def _pkg_is_free(self, pkg):
        '''
        Return if the package is free software.
        Needed to be implemented in a sub class
        '''
        return True

    def _pkg_is_arch(self, pkg):
        '''
        Return if the package is the same architecture as the machine.
        Needed to be implemented in a sub class
        '''
        return True

    def _do_installed_filtering(self, flt, pkg):
        is_installed = self._pkg_is_installed(pkg)
        if flt == FILTER_INSTALLED:
            want_installed = True
        else:
            want_installed = False
        return is_installed == want_installed

    def _do_devel_filtering(self, flt, pkg):
        is_devel = self._pkg_is_devel(pkg)
        if flt == FILTER_DEVELOPMENT:
            want_devel = True
        else:
            want_devel = False
        return is_devel == want_devel

    def _do_gui_filtering(self, flt, pkg):
        is_gui = self._pkg_is_gui(pkg)
        if flt == FILTER_GUI:
            want_gui = True
        else:
            want_gui = False
        return is_gui == want_gui

    def _do_free_filtering(self, flt, pkg):
        is_free = self._pkg_is_free(pkg)
        if flt == FILTER_FREE:
            want_free = True
        else:
            want_free = False
        return is_free == want_free

    def _do_arch_filtering(self, flt, pkg):
        is_arch = self._pkg_is_arch(pkg)
        if flt == FILTER_ARCH:
            want_arch = True
        else:
            want_arch = False
        return is_arch == want_arch

