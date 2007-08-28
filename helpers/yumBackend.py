#!/usr/bin/python -tt
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your filtersion) any later version.
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
# Copyright (C) 2007 Luke Macken <lmacken@redhat.com>


# imports

import sys
import re

from packagekit import *
import yum
from urlgrabber.progress import BaseMeter,format_time,format_number
from yum.rpmtrans import RPMBaseCallback
from yum.constants import *


class PackageKitYumBackend(PackageKitBaseBackend):

    def __init__(self,args):
        PackageKitBaseBackend.__init__(self,args)
        self.yumbase = yum.YumBase()

    def _do_search(self,searchlist,filters,key):
        '''
        Search for yum packages
        @param searchlist: The yum package fields to search in
        @param filters: package types to search (all,installed,available)
        @param key: key to seach for
        '''
        self.yumbase.conf.cache = 1 # Only look in cache.
        res = self.yumbase.searchGenerator(searchlist, [key])
        fltlist = filters.split(';')

        count = 1
        for (pkg,values) in res:
            if count > 100:
                break
            count+=1
            # are we installed?
            if self.yumbase.rpmdb.installed(pkg.name):
                installed = '1'
            else:
                installed = '0'
        
            if self._do_filtering(pkg,fltlist,installed):
                id = self.get_package_id(pkg.name, pkg.version, pkg.arch, pkg.repo)
                self.package(id,installed, pkg.summary)

    def _do_filtering(self,pkg,filterList,installed):
        ''' Filter the package, based on the filter in filterList '''

        # do we print to stdout?
        do_print = False;
        if filterList == ['none']: # 'none' = all packages.
            return True
        elif 'installed' in filterList and installed == '1':
            do_print = True
        elif '~installed' in filterList and installed == '0':
            do_print = True

        if len(filterList) == 1: # Only one filter, return
            return do_print

        if do_print:
            return self._do_extra_filtering(pkg,filterList)
        else:
            return do_print
    
    def _do_extra_filtering(self,pkg,filterList):
        ''' do extra filtering (gui,devel etc) '''
        
        for flt in filterList:
            if flt == 'installed' or flt =='~installed':
                continue
            elif flt == 'gui' or flt =='~gui':
                if not self._do_gui_filtering(flt,pkg):
                    return False
            elif flt =='devel' or flt=='~devel':
                if not self._do_devel_filtering(flt,pkg):
                    return False
        return True
    
    def _do_gui_filtering(self,flt,pkg):
        isGUI = False
        if flt == 'gui':
            wantGUI = True
        else:
            wantGUI = False
        #
        # TODO: Add GUI detection Code here.Set isGUI = True, if it is a GUI app
        #
        isGUI = wantGUI # Fake it for now
        #
        #
        return isGUI == wantGUI

    def _do_devel_filtering(self,flt,pkg):
        isDevel = False
        if flt == 'devel':
            wantDevel = True
        else:
            wantDevel = False
        #
        # TODO: Add Devel detection Code here.Set isDevel = True, if it is a devel app
        #
        regex =  re.compile(r'(-devel)|(-dgb)|(-static)')
        if regex.search(pkg.name):
            isDevel = True
        #
        #
        return isDevel == wantDevel

    def search_name(self,filters,key):
        '''
        Implement the {backend}-search-name functionality
        '''
        searchlist = ['name']
        self._do_search(searchlist, filters, key)

    def search_details(self,filters,key):
        '''
        Implement the {backend}-search-details functionality
        '''
        searchlist = ['name', 'summary', 'description', 'group']
        self._do_search(searchlist, filters, key)

    def search_group(self,filters,key):
        '''
        Implement the {backend}-search-group functionality
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def search_file(self,filters,key):
        '''
        Implement the {backend}-search-file functionality
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_deps(self,package):
        '''
        Print a list of dependencies for a given package
        '''
        res = self.yumbase.searchGenerator(['name'], [package])

        for (pkg, name) in res:
            if name[0] == package:
                deps = self.yumbase.findDeps([pkg]).values()[0]
                for deplist in deps.values():
                    for dep in deplist:
                        if not results.has_key(dep.name):
                            results[dep.name] = dep
                break

        for pkg in results.values():
            id = self.get_package_id(pkg.name, pkg.version, pkg.arch, pkg.repo)
            self.package(id, 1, pkg.summary)

    def update_system(self):
        '''
        Implement the {backend}-update-system functionality
        Needed to be implemented in a sub class
        '''
        self.yumbase.doConfigSetup()                         # Setup Yum Config
        callback = DownloadCallback(self,showNames=True)     # Download callback
        self.yumbase.repos.setProgressBar( callback )        # Setup the download callback class
        self.percentage(0)
        txmbr = self.yumbase.update() # Add all updates to Transaction
        if txmbr:
            self._runYumTransaction()
        else:
            self.error(ERROR_INTERNAL_ERROR,"Nothing to do")
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
        This will only work with yum 3.2.4 or higher
        '''
        self.yumbase.doConfigSetup()                         # Setup Yum Config
        callback = DownloadCallback(self,showNames=True)     # Download callback
        self.yumbase.repos.setProgressBar( callback )        # Setup the download callback class
        self.percentage(0)
        try:
            txmbr = self.yumbase.install(name=package)
            self._runYumTransaction()
        except yum.Errors.InstallError,e:
            msgs = '\n'.join(str(e))
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED,msgs)        

    def update(self, package):
        '''
        Implement the {backend}-install functionality
        This will only work with yum 3.2.4 or higher
        '''
        self.yumbase.doConfigSetup()                         # Setup Yum Config
        callback = DownloadCallback(self,showNames=True)     # Download callback
        self.yumbase.repos.setProgressBar( callback )        # Setup the download callback class
        self.percentage(0)
        txmbr = self.yumbase.update(name=package)
        if txmbr:
            self._runYumTransaction()
        else:
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED,"No available updates")

    
    def _runYumTransaction(self):
        '''
        Run the yum Transaction
        This will only work with yum 3.2.4 or higher
        '''
        rc,msgs =  self.yumbase.buildTransaction()
        if rc !=2:
            retmsg = "Error in Dependency Resolution\n" +"\n".join(msgs)
            self.error(ERROR_DEP_RESOLUTION_FAILED,retmsg)
        else:
            try:
                rpmDisplay = PackageKitCallback(self)
                callback = ProcessTransPackageKitCallback(self)
                self.yumbase.processTransaction(callback=callback,
                                      rpmDisplay=rpmDisplay)
            except yum.Errors.YumDownloadError, msgs:
                retmsg = "Error in Download\n" +"\n".join(msgs)
                self.error(ERROR_PACKAGE_DOWNLOAD_FAILED,retmsg)
            except yum.Errors.YumGPGCheckError, msgs:
                retmsg = "Error in Package Signatures\n" +"\n".join(msgs)
                self.error(ERROR_INTERNAL_ERROR,retmsg)
            except yum.Errors.YumBaseError, msgs:
                retmsg = "Error in Transaction Processing\n" +"\n".join(msgs)
                self.error(ERROR_INTERNAL_ERROR,retmsg)

    def remove(self, allowdep, package):
        '''
        Implement the {backend}-remove functionality
        Needed to be implemented in a sub class
        '''
        self.yumbase.doConfigSetup()                         # Setup Yum Config
        callback = DownloadCallback(self,showNames=True)     # Download callback
        self.yumbase.repos.setProgressBar( callback )        # Setup the download callback class
        self.percentage(0)
        txmbr = self.yumbase.remove(name=package)
        if txmbr:
            self._runYumTransaction()
        else:
            self.error(ERROR_INTERNAL_ERROR,"Nothing to do")


    def get_description(self, package):
        '''
        Print a detailed description for a given package
        '''
        res = self.yumbase.searchGenerator(['name'], [package])
        for (pkg, name) in res:
            if name[0] == package:
                id = self.get_package_id(pkg.name, pkg.version,
                                         pkg.arch, pkg.repo)
                self.description(id, "%s-%s" % (pkg.version, pkg.release),
                                 repr(pkg.description), pkg.url)
                break
            
    def get_updates(self):
        '''
        Implement the {backend}-get-updates functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")
            

class DownloadCallback( BaseMeter ):
    """ Customized version of urlgrabber.progress.BaseMeter class """
    def __init__(self,base,showNames = False):
        BaseMeter.__init__( self )
        self.totSize = ""
        self.base = base
        self.showNames = showNames
        self.oldName = None
        self.lastPct = 0

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
            # Elapsed time
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
        pct = int( frac*100 )
        if self.lastPct != pct:
            self.lastPct = pct
            self.base.sub_percentage(int( frac*100 ))
        if name != self.oldName:
            self.oldName = name
            if self.showNames:
                self.base.data(name)

class PackageKitCallback(RPMBaseCallback):
    def __init__(self,base):
        RPMBaseCallback.__init__(self)
        self.base = base
        self.pct = 0
        self.curpkg = None
        self.actions = { 'Updating' : STATE_UPDATE,
                         'Erasing' : STATE_REMOVE,
                         'Installing' : STATE_INSTALL}

    def event(self, package, action, te_current, te_total, ts_current, ts_total):
        if str(package) != self.curpkg:
            self.curpkg = str(package)
            self.base.data(package)
            print action
            if action in TS_INSTALL_STATES:
                self.base.status(STATE_INSTALL)
            elif action in TS_REMOVE_STATES:
                self.base.status(STATE_REMOVE)
        val = (ts_current*100L)/ts_total
        if val != self.pct:
            self.pct = val
            self.base.sub_percentage(val)

    def errorlog(self, msg):
        # grrrrrrrr
        pass

PT_DOWNLOAD        = 0
PT_GPGCHECK        = 1
PT_TEST_TRANS      = 2
PT_TRANSACTION     = 3

class ProcessTransPackageKitCallback:
    def __init__(self,base):
        self.base = base

    def event(self,state):
        if state == PT_DOWNLOAD:
            self.base.percentage(10)
            self.base.status(STATE_DOWNLOAD)
        elif state == PT_GPGCHECK:
            self.base.percentage(40)
            pass
        elif state == PT_TEST_TRANS:
            self.base.percentage(45)
            pass
        elif state == PT_TRANSACTION:
            self.base.percentage(50)
            pass
