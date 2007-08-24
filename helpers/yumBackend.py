#!/usr/bin/python -tt
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Library General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
# Copyright (C) 2007 Tim Lauridsen <timlau@fedoraproject.org>
# Copyright (C) 2007 Red Hat Inc, Seth Vidal <skvidal@fedoraproject.org>


# imports

import sys

from packagekit import *
import yum

class PackageKitYumBackend(PackageKitBaseBackend):
    
    def __init__(self,args):
        PackageKitBaseBackend.__init__(self,args)
        self.yumbase = yum.YumBase()
        
    def search_name(self,opt,keys):
        '''
        Implement the {backend}-search-nam functionality
        Needed to be implemented in a sub class
        '''
        self.yumbase.conf.cache = 1 # Only look in cache.
        searchlist = ['name']
        res = self.yumbase.searchGenerator(searchlist, [keys])
        
        count = 1
        for (pkg,values) in res:
            print pkg
            if count > 100:
                break
            count+=1 
            installed = '0'
        
            # are we installed?
            if self.yumbase.rpmdb.installed(pkg.name):
                installed = '1'
        
            # do we print to stdout?
            do_print = 0;
            if opt == 'installed' and installed == '1':
                do_print = 1
            elif opt == 'available' and installed == '0':
                do_print = 1
            elif opt == 'all':
                do_print = 1
        
            # print in correct format
            if do_print == 1:
                id = self.get_package_id(pkg.name, pkg.version, pkg.arch, pkg.repo)
                self.package(id,installed, pkg.summary)
            
        
    def get_deps(self,package):
        '''
        Implement the {backend}-get-deps functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def update_system(self):
        '''
        Implement the {backend}-update-system functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")
        
        
    def refresh_cache(self):
        '''
        Implement the {backend}-refresh_cache functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")
        
    def install(self, package):
        '''
        Implement the {backend}-install functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def remove(self, allowdep, package):
        '''
        Implement the {backend}-remove functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_description(self, package):
        '''
        Implement the {backend}-get-description functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")
        
        