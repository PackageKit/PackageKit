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
from urlgrabber.progress import BaseMeter,format_time,format_number


class PackageKitYumBackend(PackageKitBaseBackend):
    
    def __init__(self,args):
        PackageKitBaseBackend.__init__(self,args)
        self.yumbase = yum.YumBase()
        
    def _do_search(self,searchlist,opt,key):       
        '''
        Search for yum packages
        @param searchlist: The yum package fields to search in
        @param opt: package types to search (all,installed,available)
        @param key: key to seach for
        '''
        self.yumbase.conf.cache = 1 # Only look in cache.
        res = self.yumbase.searchGenerator(searchlist, [key])
        
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
        
    def search_name(self,opt,key):
        '''
        Implement the {backend}-search-nam functionality
        Needed to be implemented in a sub class
        '''
        searchlist = ['name']
        self._do_search(searchlist, opt, key)
            
    def search_details(self,opt,key):
        '''
        Implement the {backend}-search-details functionality
        Needed to be implemented in a sub class
        '''
        searchlist = ['name', 'summary', 'description', 'group']
        self._do_search(searchlist, opt, key)

       
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
        self.yumbase.doConfigSetup()          # Setup Yum Config
        callback = DownloadCallback(self)     # Download callback
        self.yumbase.repos.setProgressBar( callback ) # Setup the download callback class
        pct = 0
        self.percentage(pct)
        try:
            if len(self.yumbase.repos.listEnabled()) == 0:
                self.percentage(100)
                return
        
            #work out the slice for each one
            bump = (100/len(self.yumbase.repos.listEnabled()))/2
        
            for repo in self.yumbase.repos.listEnabled():
                repo.metadata_expire = 0
                self.yumbase.repos.populateSack(which=[repo.id], mdtype='metadata', cacheonly=1)
                pct+=bump
                self.percentage(pct)
                self.yumbase.repos.populateSack(which=[repo.id], mdtype='filelists', cacheonly=1)
                pct+=bump
                self.percentage(pct)
        
            #we might have a rounding error
            self.percentage(100)
        
        except yum.Errors.YumBaseError, e:
            self.error(ERROR_INTERNAL_ERROR,str(e))
        
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
        
class DownloadCallback( BaseMeter ):
    """ Customized version of urlgrabber.progress.BaseMeter class """
    def __init__( self,base):
        BaseMeter.__init__( self )
        self.totSize = ""
        self.base = base
               
    def update( self, amount_read, now=None ):
        BaseMeter.update( self, amount_read, now )           

    def _do_start( self, now=None ):
        name = self._getName()        
        self.updateProgress(name,0.0,"","")        
        if not self.size is None:
            self.totSize = format_number( self.size )

    def _do_update( self, amount_read, now=None ):
        fread = format_number( amount_read )
        name = self._getName()        
        if self.size is None:
            # Elabsed time
            etime = self.re.elapsed_time()
            fetime = format_time( etime )
            frac = 0.0
            self.updateProgress(name,frac,fread,fetime)      
        else:
            # Remaining time
            rtime = self.re.remaining_time()
            frtime = format_time( rtime )
            frac = self.re.fraction_read()
            self.updateProgress(name,frac,fread,frtime)      
              

    def _do_end( self, amount_read, now=None ):
        total_time = format_time( self.re.elapsed_time() )
        total_size = format_number( amount_read )
        name = self._getName()        
        self.updateProgress(name,1.0,total_size,total_time)      

    def _getName(self):
        '''
        Get the name of the package being downloaded
        '''
        if self.text and type( self.text ) == type( "" ):
            name = self.text
        else:
            name = self.basename
        return name
        
    def updateProgress(self,name,frac,fread,ftime):
        '''
         Update the progressbar (Overload in child class)
        @param name: filename
        @param frac: Progress fracment (0 -> 1)
        @param fread: formated string containing BytesRead
        @param ftime : formated string containing remaining or elapsed time
        '''
        self.base.sub_percentage(int( frac*100 ))
    
    
        