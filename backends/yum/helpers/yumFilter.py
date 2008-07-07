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
from packagekit.backend import *
from packagekit.package import *
from yumDirect import *

import yum
import rpmUtils
import re

GUI_KEYS = re.compile(r'(qt)|(gtk)')

class YumFilter(object):

    def __init__(self,fltlist="none"):
        ''' connect to all enabled repos '''
        self.fltlist = fltlist
        self.package_list = [] #we can't do emitting as found if we are post-processing
        self.installed_nevra = []
        self.pkpackage = PackagekitPackage()

    def add_installed(self,pkgs):
        ''' add a list of packages that are already installed '''
        for pkg in pkgs:
            if self._do_extra_filtering(pkg,self.fltlist):
                self.package_list.append((pkg,INFO_INSTALLED))
            self.installed_nevra.append(self._get_nevra(pkg))

    def add_available(self,pkgs):
        # add a list of packages that are available
        for pkg in pkgs:
            nevra = self._get_nevra(pkg)
            if nevra not in self.installed_nevra:
                if self._do_extra_filtering(pkg,self.fltlist):
                    self.package_list.append((pkg,INFO_AVAILABLE))

    def add_custom(self,pkg,info):
        ''' add a custom packages indervidually '''
        nevra = self._get_nevra(pkg)
        if nevra not in self.installed_nevra:
            if self._do_extra_filtering(pkg,self.fltlist):
                self.package_list.append((pkg,info))

    def post_process(self):
        ''' do filtering we couldn't do when generating the list '''

        if FILTER_BASENAME in self.fltlist:
            self.package_list = self._basename_filter(self.package_list)

        if FILTER_NEWEST in self.fltlist:
            self.package_list = self._do_newest_filtering(self.package_list)

        return self.package_list

    def _get_nevra(self,pkg):
        ''' gets the NEVRA for a pkg '''
        return "%s-%s:%s-%s.%s" % (pkg.name,pkg.epoch,pkg.version,pkg.release,pkg.arch);

    def _is_main_package(self,repo):
        if repo.endswith('-debuginfo'):
            return False
        if repo.endswith('-devel'):
            return False
        if repo.endswith('-libs'):
            return False
        return True

    def _basename_filter(self,package_list):
        '''
        Filter the list so that the number of packages are reduced.
        This is done by only displaying gtk2 rather than gtk2-devel, gtk2-debuginfo, etc.
        This imlementation is done by comparing the SRPM name, and if not falling back
        to the first entry.
        We have to fall back else we don't emit packages where the SRPM does not produce a
        RPM with the same name, for instance, mono produces mono-core, mono-data and mono-winforms.
        @package_list: a (pkg,status) list of packages
        A new list is returned that has been filtered
        '''
        base_list = []
        output_list = []
        base_list_already_got = []

        #find out the srpm name and add to a new array of compound data
        for (pkg,status) in package_list:
            if pkg.sourcerpm:
                base = rpmUtils.miscutils.splitFilename(pkg.sourcerpm)[0]
                base_list.append ((pkg,status,base,pkg.version));
            else:
                base_list.append ((pkg,status,'nosrpm',pkg.version));

        #find all the packages that match thier basename name (done seporately so we get the "best" match)
        for (pkg,status,base,version) in base_list:
            if base == pkg.name and (base,version) not in base_list_already_got:
                output_list.append((pkg,status))
                base_list_already_got.append ((base,version))

        #for all the ones not yet got, can we match against a non devel match?
        for (pkg,status,base,version) in base_list:
            if (base,version) not in base_list_already_got:
                if self._is_main_package(pkg.name):
                    output_list.append((pkg,status))
                    base_list_already_got.append ((base,version))

        #add the remainder of the packages, which should just be the single debuginfo's
        for (pkg,status,base,version) in base_list:
            if (base,version) not in base_list_already_got:
                output_list.append((pkg,status))
                base_list_already_got.append ((base,version))
        return output_list

    def _do_newest_filtering(self,pkglist):
        '''
        Only return the newest package for each name.arch
        '''
        newest = {}
        for pkg,state in pkglist:
            key = (pkg.name, pkg.arch)
            if key in newest and pkg <= newest[key][0]:
                continue
            newest[key] = (pkg,state)
        return newest.values()

    def _do_extra_filtering(self,pkg,filterList):
        ''' do extra filtering (gui,devel etc) '''
        for filter in filterList:
            if filter in (FILTER_INSTALLED,FILTER_NOT_INSTALLED):
                if not self._do_installed_filtering(filter,pkg):
                    return False
            elif filter in (FILTER_GUI,FILTER_NOT_GUI):
                if not self._do_gui_filtering(filter,pkg):
                    return False
            elif filter in (FILTER_DEVELOPMENT,FILTER_NOT_DEVELOPMENT):
                if not self._do_devel_filtering(filter,pkg):
                    return False
            elif filter in (FILTER_FREE,FILTER_NOT_FREE):
                if not self._do_free_filtering(filter,pkg):
                    return False
        return True

    def _do_installed_filtering(self,flt,pkg):
        isInstalled = False
        if flt == FILTER_INSTALLED:
            wantInstalled = True
        else:
            wantInstalled = False
        if isinstance(pkg,PackageObjectSimple):
            isInstalled = False
        else:
            isInstalled = pkg.repo.id == 'installed'
        return isInstalled == wantInstalled

    def _do_gui_filtering(self,flt,pkg):
        isGUI = False
        if flt == FILTER_GUI:
            wantGUI = True
        else:
            wantGUI = False
        isGUI = self._check_for_gui(pkg)
        return isGUI == wantGUI

    def _check_for_gui(self,pkg):
        '''  Check if the GUI_KEYS regex matches any package requirements'''
        for req in pkg.requires:
            reqname = req[0]
            if GUI_KEYS.search(reqname):
                return True
        return False

    def _do_devel_filtering(self,flt,pkg):
        isDevel = False
        if flt == FILTER_DEVELOPMENT:
            wantDevel = True
        else:
            wantDevel = False
        regex =  re.compile(r'(-devel)|(-dgb)|(-static)')
        if regex.search(pkg.name):
            isDevel = True
        return isDevel == wantDevel

    def _do_free_filtering(self,flt,pkg):
        isFree = False
        if flt == FILTER_FREE:
            wantFree = True
        else:
            wantFree = False

        isFree = self.pkpackage.check_license_field(pkg.license)

        return isFree == wantFree

