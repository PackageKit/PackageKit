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

# Copyright (C) 2007-2008
#    Tim Lauridsen <timlau@fedoraproject.org>
#    Seth Vidal <skvidal@fedoraproject.org>
#    Luke Macken <lmacken@redhat.com>
#    James Bowes <jbowes@dangerouslyinc.com>
#    Robin Norwood <rnorwood@redhat.com>
#    Richard Hughes <richard@hughsie.com>

# imports

import re

from packagekit.backend import *
from packagekit.progress import *
from packagekit.package import *
import yum
from urlgrabber.progress import BaseMeter,format_time,format_number
from yum.rpmtrans import RPMBaseCallback
from yum.constants import *
from yum.update_md import UpdateMetadata
from yum.callbacks import *
from yum.misc import prco_tuple_to_string,unique
from yum.packages import YumLocalPackage, parsePackages
from yum.packageSack import MetaSack
import rpmUtils
import exceptions
import types
import signal
import time
import os.path

import tarfile
import tempfile
import shutil

from yumDirect import *
from yumFilter import *
from yumComps import *

# Global vars
yumbase = None
progress = PackagekitProgress()  # Progress object to store the progress
pkpackage = PackagekitPackage()

MetaDataMap = {
    'repomd'        : STATUS_DOWNLOAD_REPOSITORY,
    'primary'       : STATUS_DOWNLOAD_PACKAGELIST,
    'filelists'     : STATUS_DOWNLOAD_FILELIST,
    'other'         : STATUS_DOWNLOAD_CHANGELOG,
    'comps'         : STATUS_DOWNLOAD_GROUP,
    'updateinfo'    : STATUS_DOWNLOAD_UPDATEINFO
}

class GPGKeyNotImported(exceptions.Exception):
    pass

def sigquit(signum,frame):
    print >> sys.stderr,"Quit signal sent - exiting immediately"
    if yumbase:
        print >> sys.stderr,"unlocking backend"
        yumbase.closeRpmDB()
        yumbase.doUnlock(YUM_PID_FILE)
    sys.exit(1)

class PackageKitYumBackend(PackageKitBaseBackend):

    # Packages there require a reboot
    rebootpkgs = ("kernel","kernel-smp","kernel-xen-hypervisor","kernel-PAE",
              "kernel-xen0","kernel-xenU","kernel-xen","kernel-xen-guest",
              "glibc","hal","dbus","xen")

    def handle_repo_error(func):
        def wrapper(*args,**kwargs):
            self = args[0]

            try:
                func(*args,**kwargs)
            except yum.Errors.RepoError,e:
                self._refresh_yum_cache()

                try:
                    func(*args,**kwargs)
                except yum.Errors.RepoError,e:
                    self.error(ERROR_NO_CACHE,str(e))

        return wrapper

    def __init__(self,args,lock=True):
        signal.signal(signal.SIGQUIT,sigquit)
        PackageKitBaseBackend.__init__(self,args)
        self.yumbase = PackageKitYumBase(self)

        self.comps = yumComps(self.yumbase)
        if not self.comps.connect():
            self.refresh_cache()
            if not self.comps.connect():
                self.error(ERROR_GROUP_LIST_INVALID,'comps categories could not be loaded')

        yumbase = self.yumbase
        self._setup_yum()
        if lock:
            self.doLock()

    def details(self,id,license,group,desc,url,bytes):
        '''
        Send 'details' signal
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param license: The license of the package
        @param group: The enumerated group
        @param desc: The multi line package description
        @param url: The upstream project homepage
        @param bytes: The size of the package, in bytes
        convert the description to UTF before sending
        '''
        desc = self._to_unicode(desc)
        PackageKitBaseBackend.details(self,id,license,group,desc,url,bytes)

    def package(self,id,status,summary):
        '''
        send 'package' signal
        @param info: the enumerated INFO_* string
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param summary: The package Summary
        convert the summary to UTF before sending
        '''
        summary = self._to_unicode(summary)
        PackageKitBaseBackend.package(self,id,status,summary)

    def _to_unicode(self,txt,encoding='utf-8'):
        if isinstance(txt,basestring):
            if not isinstance(txt,unicode):
                txt = unicode(txt,encoding,errors='replace')
        return txt

    def doLock(self):
        ''' Lock Yum'''
        retries = 0
        while not self.isLocked():
            try: # Try to lock yum
                self.yumbase.doLock(YUM_PID_FILE)
                PackageKitBaseBackend.doLock(self)
            except:
                time.sleep(2)
                retries += 1
                if retries > 100:
                    self.error(ERROR_CANNOT_GET_LOCK,'Yum is locked by another application')

    def unLock(self):
        ''' Unlock Yum'''
        if self.isLocked():
            PackageKitBaseBackend.unLock(self)
            self.yumbase.closeRpmDB()
            self.yumbase.doUnlock(YUM_PID_FILE)

    def _get_package_ver(self,po):
        ''' return the a ver as epoch:version-release or version-release, if epoch=0'''
        if po.epoch != '0':
            ver = "%s:%s-%s" % (po.epoch,po.version,po.release)
        else:
            ver = "%s-%s" % (po.version,po.release)
        return ver

    def _get_nevra(self,pkg):
        ''' gets the NEVRA for a pkg '''
        return "%s-%s:%s-%s.%s" % (pkg.name,pkg.epoch,pkg.version,pkg.release,pkg.arch);

    @handle_repo_error
    def _do_search(self,searchlist,filters,key):
        '''
        Search for yum packages
        @param searchlist: The yum package fields to search in
        @param filters: package types to search (all,installed,available)
        @param key: key to seach for
        '''
        res = self.yumbase.searchGenerator(searchlist,[key])
        fltlist = filters.split(';')
        pkgfilter = YumFilter(fltlist)
        package_list = [] #we can't do emitting as found if we are post-processing
        installed = []
        available = []

        for (pkg,values) in res:
            if pkg.repo.id == 'installed':
                installed.append(pkg)
            else:
                available.append(pkg)

        pkgfilter.add_installed(installed)
        pkgfilter.add_available(available)

        # we couldn't do this when generating the list
        package_list = pkgfilter.post_process()
        self._show_package_list(package_list)

    def _show_package_list(self,lst):
        for (pkg,status) in lst:
            self._show_package(pkg,status)

    def search_name(self,filters,key):
        '''
        Implement the {backend}-search-name functionality
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True)
        self.percentage(None)

        searchlist = ['name']
        self.status(STATUS_QUERY)
        self.yumbase.doConfigSetup(errorlevel=0,debuglevel=0)# Setup Yum Config
        self._do_search(searchlist,filters,key)

    def search_details(self,filters,key):
        '''
        Implement the {backend}-search-details functionality
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True)
        self.percentage(None)

        searchlist = ['name','summary','description','group']
        self.status(STATUS_QUERY)
        self._do_search(searchlist,filters,key)

    @handle_repo_error
    def search_group(self,filters,group_key):
        '''
        Implement the {backend}-search-group functionality
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True)
        self.yumbase.doConfigSetup(errorlevel=0,debuglevel=0)# Setup Yum Config
        self.yumbase.conf.cache = 1 # Only look in cache.
        self.status(STATUS_QUERY)
        package_list = [] #we can't do emitting as found if we are post-processing
        fltlist = filters.split(';')

        # use direct access for speed
        direct = YumDirectSQL(self.yumbase)
        pkgfilter = YumFilter(fltlist)

        # get the packagelist for this group
        all_packages = self.comps.get_package_list(group_key)

        # get installed packages
        self.percentage(10)
        for package in all_packages:
            pkgs = self.yumbase.rpmdb.searchNevra(name=package)
            pkgfilter.add_installed(pkgs)

        # get available packages
        self.percentage(20)
        if FILTER_INSTALLED not in fltlist:
            # ideally we want to use pkgSack.searchNames, but it's broken with
            # 'too many SQL variables' when you pass it lots of packages
            #pkgs = self.yumbase.pkgSack.searchNames(names=all_packages)
            #pkgfilter.add_available(pkgs)
            for package in all_packages:
                pkgs = direct.resolve(package)
                pkgfilter.add_available(pkgs)

        # we couldn't do this when generating the list
        package_list = pkgfilter.post_process()

        self.percentage(90)
        self._show_package_list(package_list)

        # close all the databases
        direct.close()
        self.percentage(100)

    @handle_repo_error
    def get_packages(self,filters):
        '''
        Search for yum packages
        @param searchlist: The yum package fields to search in
        @param filters: package types to search (all,installed,available)
        @param key: key to seach for
        '''
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.yumbase.doConfigSetup(errorlevel=0,debuglevel=0)# Setup Yum Config
        self.yumbase.conf.cache = 1 # Only look in cache.

        package_list = [] #we can't do emitting as found if we are post-processing
        fltlist = filters.split(';')
        pkgfilter = YumFilter(fltlist)

        # Now show installed packages.
        pkgs = self.yumbase.rpmdb
        pkgfilter.add_installed(pkgs)

        # Now show available packages.
        if FILTER_INSTALLED not in fltlist:
            pkgs = self.yumbase.pkgSack
            pkgfilter.add_available(pkgs)

        # we couldn't do this when generating the list
        package_list = pkgfilter.post_process()
        self._show_package_list(package_list)

    @handle_repo_error
    def search_file(self,filters,key):
        '''
        Implement the {backend}-search-file functionality
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)

        #self.yumbase.conf.cache = 1 # Only look in cache.
        fltlist = filters.split(';')
        pkgfilter = YumFilter(fltlist)

        # Check installed for file
        pkgs = self.yumbase.rpmdb.searchFiles(key)
        pkgfilter.add_installed(pkgs)

        # Check available for file
        if not FILTER_INSTALLED in fltlist:
            # Check available for file
            self.yumbase.repos.populateSack(mdtype='filelists')
            pkgs = self.yumbase.pkgSack.searchFiles(key)
            pkgfilter.add_available(pkgs)

        # we couldn't do this when generating the list
        package_list = pkgfilter.post_process()
        self._show_package_list(package_list)

    @handle_repo_error
    def what_provides(self,filters,provides_type,search):
        '''
        Implement the {backend}-what-provides functionality
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)

        fltlist = filters.split(';')
        pkgfilter = YumFilter(fltlist)

        # Check installed for file
        pkgs = self.yumbase.rpmdb.searchProvides(search)
        pkgfilter.add_installed(pkgs)

        if not FILTER_INSTALLED in fltlist:
            # Check available for file
            pkgs = self.yumbase.pkgSack.searchProvides(search)
            pkgfilter.add_available(pkgs)

        # we couldn't do this when generating the list
        package_list = pkgfilter.post_process()
        self._show_package_list(package_list)

    @handle_repo_error
    def download_packages(self,directory,package_ids):
        '''
        Implement the {backend}-download-packages functionality
        '''
        self._check_init()
        self.allow_cancel(True)
        self.status(STATUS_DOWNLOAD)
        percentage = 0;
        bump = 100 / len(package_ids)

        # download each package
        for package in package_ids:
            self.percentage(percentage)
            pkg,inst = self._findPackage(package)
            n,a,e,v,r = pkg.pkgtup
            packs = self.yumbase.pkgSack.searchNevra(n,e,v,r,a)

            # if we couldn't map package_id -> pkg
            if len(packs) == 0:
                self.message(MESSAGE_WARNING,"Could not find a match for package %s" % package)
                continue

            # should have only one...
            for pkg_download in packs:
                self._show_package(pkg_download,INFO_DOWNLOADING)
                repo = self.yumbase.repos.getRepo(pkg_download.repoid)
                remote = pkg_download.returnSimple('relativepath')
                local = os.path.basename(remote)
                if not os.path.exists(directory):
                    self.error(ERROR_PACKAGE_DOWNLOAD_FAILED,"No destination directory exists")
                local = os.path.join(directory,local)
                if (os.path.exists(local) and os.path.getsize(local) == int(pkg_download.returnSimple('packagesize'))):
                    self.error(ERROR_PACKAGE_DOWNLOAD_FAILED,"Package already exists")
                    continue
                # Disable cache otherwise things won't download
                repo.cache = 0
                pkg_download.localpath = local #Hack:To set the localpath we want
                try:
                    path = repo.getPackage(pkg_download)
                except IOError, e:
                    self.error(ERROR_WRITE_ERROR,"Cannot write to file")
                    continue
            percentage += bump

        # in case we don't sum to 100
        self.percentage(100)

    def _getEVR(self,idver):
        '''
        get the e,v,r from the package id version
        '''
        cpos = idver.find(':')
        if cpos != -1:
            epoch = idver[:cpos]
            idver = idver[cpos+1:]
        else:
            epoch = '0'
        (version,release) = tuple(idver.split('-'))
        return epoch,version,release

    def _findPackage(self,id):
        '''
        find a package based on a package id (name;version;arch;repoid)
        '''
        # is this an real id or just an name
        if len(id.split(';')) > 1:
            # Split up the id
            (n,idver,a,d) = pkpackage.get_package_from_id(id)
            # get e,v,r from package id version
            e,v,r = self._getEVR(idver)
        else:
            n = id
            e = v = r = a = None
        # search the rpmdb for the nevra
        pkgs = self.yumbase.rpmdb.searchNevra(name=n,epoch=e,ver=v,rel=r,arch=a)
        # if the package is found, then return it (do not have to match the repo_id)
        if len(pkgs) != 0:
            return pkgs[0],True
        # search the pkgSack for the nevra
        try:
            pkgs = self.yumbase.pkgSack.searchNevra(name=n,epoch=e,ver=v,rel=r,arch=a)
        except yum.Errors.RepoError,e:
            self.error(ERROR_REPO_NOT_AVAILABLE,str(e))
        # nothing found
        if len(pkgs) == 0:
            return None,False
        # one NEVRA in a single repo
        if len(pkgs) == 1:
            return pkgs[0],False
        # we might have the same NEVRA in multiple repos, match by repo name
        for pkg in pkgs:
            if d == pkg.repoid:
                return pkg,False
        # repo id did not match
        return None,False

    def _get_pkg_requirements(self,pkg,reqlist=[]):
        pkgs = self.yumbase.rpmdb.searchRequires(pkg.name)
        reqlist.extend(pkgs)
        if pkgs:
            for po in pkgs:
                self._get_pkg_requirements(po,reqlist)
        else:
            return reqlist

    def _text_to_boolean(self,text):
        '''
        Parses true and false
        '''
        if text == 'true':
            return True
        if text == 'TRUE':
            return True
        if text == 'yes':
            return True
        if text == 'YES':
            return True
        return False

    def get_requires(self,filters,package_ids,recursive_text):
        '''
        Print a list of requires for a given package
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        percentage = 0;
        bump = 100 / len(package_ids)
        deps_list = []
        resolve_list = []
        recursive = self._text_to_boolean(recursive_text)

        for package in package_ids:
            self.percentage(percentage)
            pkg,inst = self._findPackage(package)
            # FIXME: This is a hack, it simulates a removal of the
            # package and return the transaction
            if inst and pkg:
                resolve_list.append(pkg)
                txmbrs = self.yumbase.remove(po=pkg)
                if txmbrs:
                    rc,msgs =  self.yumbase.buildTransaction()
                    if rc !=2:
                        self.error(ERROR_DEP_RESOLUTION_FAILED,self._format_msgs(msgs))
                    else:
                        for txmbr in self.yumbase.tsInfo:
                            if pkg not in deps_list:
                                deps_list.append(txmbr.po)
            percentage += bump

        # remove any of the original names
        for pkg in resolve_list:
            if pkg in deps_list:
                deps_list.remove(pkg)

        # each unique name, emit
        for pkg in deps_list:
            id = self._pkg_to_id(pkg)
            self.package(id,INFO_INSTALLED,pkg.summary)
        self.percentage(100)

    def _is_inst(self,pkg):
        # search only for requested arch
        return self.yumbase.rpmdb.installed(po=pkg)

    def _is_inst_arch(self,pkg):
        # search for a requested arch first
        ret = self._is_inst(pkg)
        if ret:
            return True;

        # then fallback to i686 if i386
        if pkg.arch == 'i386':
            pkg.arch = 'i686'
            ret = self._is_inst(pkg)
            pkg.arch = 'i386'
        return ret

    def _installable(self,pkg,ematch=False):

        """check if the package is reasonably installable, true/false"""

        exactarchlist = self.yumbase.conf.exactarchlist
        # we look through each returned possibility and rule out the
        # ones that we obviously can't use

        if self._is_inst_arch(pkg):
            return False

        # everything installed that matches the name
        installedByKey = self.yumbase.rpmdb.searchNevra(name=pkg.name,arch=pkg.arch)
        comparable = []
        for instpo in installedByKey:
            if rpmUtils.arch.isMultiLibArch(instpo.arch) == rpmUtils.arch.isMultiLibArch(pkg.arch):
                comparable.append(instpo)
            else:
                continue

        # go through each package
        if len(comparable) > 0:
            for instpo in comparable:
                if pkg.EVR > instpo.EVR: # we're newer - this is an update, pass to them
                    if instpo.name in exactarchlist:
                        if pkg.arch == instpo.arch:
                            return True
                    else:
                        return True

                elif pkg.EVR == instpo.EVR: # same, ignore
                    return False

                elif pkg.EVR < instpo.EVR: # lesser, check if the pkgtup is an exactmatch
                                   # if so then add it to be installed
                                   # if it can be multiply installed
                                   # this is where we could handle setting
                                   # it to be an 'oldpackage' revert.

                    if ematch and self.yumbase.allowedMultipleInstalls(pkg):
                        return True

        else: # we've not got any installed that match n or n+a
            return True

        return False

    def _get_best_pkg_from_list(self,pkglist):
        '''
        Gets best dep package from a list
        '''
        best = None

        # first try and find the highest EVR package that is already installed
        for pkgi in pkglist:
            n,a,e,v,r = pkgi.pkgtup
            pkgs = self.yumbase.rpmdb.searchNevra(name=n,epoch=e,ver=v,arch=a)
            for pkg in pkgs:
                if best:
                    if pkg.EVR > best.EVR:
                        best=pkg
                else:
                    best=pkg

        # then give up and see if there's one available
        if not best:
            for pkg in pkglist:
                if best:
                    if pkg.EVR > best.EVR:
                        best=pkg
                else:
                    best=pkg
        return best

    def _get_best_depends(self,pkgs,recursive):
        ''' Gets the best deps for a package
        @param pkgs: a list of package objects
        @param recursive: if we recurse
        @return: a list for yum package object providing the dependencies
        '''
        deps_list = []

        # get the dep list
        results = self.yumbase.findDeps(pkgs)
        require_list = []
        recursive_list = []

        # get the list of deps for each package
        for pkg in results.keys():
            for req in results[pkg].keys():
                reqlist = results[pkg][req]
                if not reqlist: #  Unsatisfied dependency
                    self.error(ERROR_DEP_RESOLUTION_FAILED,"the (%s) requirement could not be resolved" % prco_tuple_to_string(req),exit=False)
                    break
                require_list.append(reqlist)

        # for each list, find the best backage using a metric
        for reqlist in require_list:
            pkg = self._get_best_pkg_from_list(reqlist)
            if pkg not in pkgs:
                deps_list.append(pkg)
                if recursive and not self._is_inst(pkg):
                    recursive_list.append(pkg)

        # if the package is to be downloaded, also find its deps
        if len(recursive_list) > 0:
            pkgsdeps = self._get_best_depends(recursive_list,True)
            for pkg in pkgsdeps:
                if pkg not in pkgs:
                    deps_list.append(pkg)

        return deps_list

    def get_depends(self,filters,package_ids,recursive_text):
        '''
        Print a list of depends for a given package
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        fltlist = filters.split(';')
        pkgfilter = YumFilter(fltlist)
        recursive = self._text_to_boolean(recursive_text)

        percentage = 0;
        bump = 100 / len(package_ids)
        deps_list = []
        resolve_list = []

        # resolve each package_id to a pkg object
        for package in package_ids:
            self.percentage(percentage)
            name = package.split(';')[0]
            pkg,inst = self._findPackage(package)
            if pkg:
                resolve_list.append(pkg)
            else:
                self.error(ERROR_PACKAGE_NOT_FOUND,'Package %s was not found' % package)
                break
            percentage += bump

        # get the best deps
        deps_list = self._get_best_depends(resolve_list,recursive)

        # make unique list
        deps_list = unique(deps_list)

        # add to correct lists
        for pkg in deps_list:
            if self._is_inst(pkg):
                pkgfilter.add_installed([pkg])
            else:
                pkgfilter.add_available([pkg])

        # we couldn't do this when generating the list
        package_list = pkgfilter.post_process()
        self._show_package_list(package_list)
        self.percentage(100)

    def update_system(self):
        '''
        Implement the {backend}-update-system functionality
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True)
        self.percentage(0)
        self.status(STATUS_RUNNING)

        old_throttle = self.yumbase.conf.throttle
        self.yumbase.conf.throttle = "60%" # Set bandwidth throttle to 60%
                                           # to avoid taking all the system's bandwidth.
        old_skip_broken = self.yumbase.conf.skip_broken
        self.yumbase.conf.skip_broken = 1

        try:
            txmbr = self.yumbase.update() # Add all updates to Transaction
        except yum.Errors.RepoError,e:
            self.error(ERROR_REPO_NOT_AVAILABLE,str(e))
        if txmbr:
            self._runYumTransaction()
        else:
            self.error(ERROR_NO_PACKAGES_TO_UPDATE,"Nothing to do")

        self.yumbase.conf.throttle = old_throttle
        self.yumbase.conf.skip_broken = old_skip_broken

    def refresh_cache(self):
        '''
        Implement the {backend}-refresh_cache functionality
        '''
        self.allow_cancel(True);
        self.percentage(0)
        self.status(STATUS_REFRESH_CACHE)

        pct = 0
        try:
            if len(self.yumbase.repos.listEnabled()) == 0:
                self.percentage(100)
                return

            #work out the slice for each one
            bump = (95/len(self.yumbase.repos.listEnabled()))/2

            for repo in self.yumbase.repos.listEnabled():
                repo.metadata_expire = 0
                self.yumbase.repos.populateSack(which=[repo.id],mdtype='metadata',cacheonly=1)
                pct+=bump
                self.percentage(pct)
                self.yumbase.repos.populateSack(which=[repo.id],mdtype='filelists',cacheonly=1)
                pct+=bump
                self.percentage(pct)

            self.percentage(95)
            # Setup categories/groups
            self.yumbase.doGroupSetup()
            #we might have a rounding error
            self.percentage(100)

        except yum.Errors.RepoError,e:
            self.error(ERROR_REPO_CONFIGURATION_ERROR,str(e))
        except yum.Errors.YumBaseError,e:
            self.error(ERROR_UNKNOWN,str(e))

        # update the comps groups too
        self.comps.refresh()

    @handle_repo_error
    def resolve(self,filters,packages):
        '''
        Implement the {backend}-resolve functionality
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True);
        self.percentage(None)
        self.yumbase.doConfigSetup(errorlevel=0,debuglevel=0)# Setup Yum Config
        self.yumbase.conf.cache = 1 # Only look in cache.
        self.status(STATUS_QUERY)

        fltlist = filters.split(';')
        for package in packages:
            # Get installed packages
            installedByKey = self.yumbase.rpmdb.searchNevra(name=package)
            if FILTER_NOT_INSTALLED not in fltlist:
                for pkg in installedByKey:
                    self._show_package(pkg,INFO_INSTALLED)
            # Get available packages
            if FILTER_INSTALLED not in fltlist:
                for pkg in self.yumbase.pkgSack.returnNewestByNameArch():
                    if pkg.name == package:
                        show = True
                        for instpo in installedByKey:
                            # Check if package have a smaller & equal EVR to a inst pkg
                            if pkg.EVR < instpo.EVR or pkg.EVR == instpo.EVR:
                                show = False
                        if show:
                            self._show_package(pkg,INFO_AVAILABLE)
                            break

    @handle_repo_error
    def install_packages(self,package_ids):
        '''
        Implement the {backend}-install-packages functionality
        This will only work with yum 3.2.4 or higher
        '''
        self._check_init()
        self.allow_cancel(False)
        self.percentage(0)
        self.status(STATUS_RUNNING)
        txmbrs = []
        already_warned = False
        for package in package_ids:
            pkg,inst = self._findPackage(package)
            if pkg and not inst:
                repo = self.yumbase.repos.getRepo(pkg.repoid)
                if not already_warned and not repo.gpgcheck:
                    self.message(MESSAGE_WARNING,"The untrusted package %s will be installed from %s." % (pkg.name, repo))
                    already_warned = True
                txmbr = self.yumbase.install(po=pkg)
                txmbrs.extend(txmbr)
            if inst:
                self.error(ERROR_PACKAGE_ALREADY_INSTALLED,"The package %s is already installed" % pkg.name)
        if txmbrs:
            self._runYumTransaction()
        else:
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED,"The packages failed to be installed")

    def _checkForNewer(self,po):
        pkgs = self.yumbase.pkgSack.returnNewestByName(name=po.name)
        if pkgs:
            newest = pkgs[0]
            if newest.EVR > po.EVR:
                self.message(MESSAGE_WARNING,"A newer version of %s is available online." % po.name)

    def install_files(self,trusted,inst_files):
        '''
        Implement the {backend}-install-files functionality
        Install the package containing the inst_file file
        Needed to be implemented in a sub class
        '''
        for inst_file in inst_files:
            if inst_file.endswith('.src.rpm'):
                self.error(ERROR_CANNOT_INSTALL_SOURCE_PACKAGE,'Backend will not install a src rpm file')
                return

        self._check_init()
        self.allow_cancel(False);
        self.percentage(0)
        self.status(STATUS_RUNNING)

        # process these first
        inst_packs = []

        for inst_file in inst_files:
            if inst_file.endswith('.rpm'):
                continue
            elif inst_file.endswith('.pack'):
                inst_packs.append(inst_file)
                inst_files.remove(inst_file)
            else:
                self.error(ERROR_INVALID_PACKAGE_FILE,'Only rpm files and packs are supported')
                return

        # decompress and add the contents of any .pack files
        for inst_pack in inst_packs:
            pack = tarfile.TarFile(name = inst_pack,mode = "r")
            members = pack.getnames()
            tempdir = tempfile.mkdtemp()
            for mem in members:
                pack.extract(mem,path = tempdir)
            files = os.listdir(tempdir)
            for file in files:
                inst_files.append(os.path.join(tempdir, file))

        # remove files of packages that alrady exist
        for inst_file in inst_files:
            try:
                pkg = YumLocalPackage(ts=self.yumbase.rpmdb.readOnlyTS(), filename=inst_file)
                if self._is_inst(pkg):
                    inst_files.remove(inst_file)
            except yum.Errors.YumBaseError,e:
                self.error(ERROR_INVALID_PACKAGE_FILE,'Package could not be decompressed')
            except:
                self.error(ERROR_UNKNOWN,"Failed to open local file -- please report")

        # If trusted is true, it means that we will only install trusted files
        if trusted == 'yes':
            # disregard the default
            self.yumbase.conf.gpgcheck=1

            # self.yumbase.installLocal fails for unsigned packages when self.yumbase.conf.gpgcheck=1
            # This means we don't run runYumTransaction, and don't get the GPG failure in
            # PackageKitYumBase(_checkSignatures) -- so we check here
            for inst_file in inst_files:
                po = YumLocalPackage(ts=self.yumbase.rpmdb.readOnlyTS(), filename=inst_file)
                try:
                    self.yumbase._checkSignatures([po], None)
                except yum.Errors.YumGPGCheckError,e:
                    self.error(ERROR_MISSING_GPG_SIGNATURE,str(e))
        else:
            self.yumbase.conf.gpgcheck=0

        # common checks copied from yum
        for inst_file in inst_files:
            if not self._check_local_file(inst_file):
                return

        txmbrs = []
        try:
            for inst_file in inst_files:
                txmbr = self.yumbase.installLocal(inst_file)
                if txmbr:
                    txmbrs.extend(txmbr)
                    self._checkForNewer(txmbr[0].po)
                    # Added the package to the transaction set
                else:
                    self.error(ERROR_LOCAL_INSTALL_FAILED,"Can't install %s" % inst_file)
            if len(self.yumbase.tsInfo) == 0:
                self.error(ERROR_LOCAL_INSTALL_FAILED,"Can't install %s" % " or ".join(inst_files))
            self._runYumTransaction()

        except yum.Errors.InstallError,e:
            self.error(ERROR_LOCAL_INSTALL_FAILED,str(e))
        except (yum.Errors.RepoError,yum.Errors.PackageSackError,IOError):
            # We might not be able to connect to the internet to get
            # repository metadata, or the package might not exist.
            # Try again, (temporarily) disabling repos first.
            try:
                for repo in self.yumbase.repos.listEnabled():
                    repo.disable()

                for inst_file in inst_files:
                    txmbr = self.yumbase.installLocal(inst_file)
                    if txmbr:
                        txmbrs.extend(txmbr)
                        if len(self.yumbase.tsInfo) > 0:
                            if not self.yumbase.tsInfo.pkgSack:
                                self.yumbase.tsInfo.pkgSack = MetaSack()
                            self._runYumTransaction()
                    else:
                        self.error(ERROR_LOCAL_INSTALL_FAILED,"Can't install %s" % inst_file)
            except yum.Errors.InstallError,e:
                self.error(ERROR_LOCAL_INSTALL_FAILED,str(e))

    def _check_local_file(self, pkg):
        """
        Duplicates some of the checks that yumbase.installLocal would
        do, so we can get decent error reporting.
        """
        po = None
        try:
            po = YumLocalPackage(ts=self.yumbase.rpmdb.readOnlyTS(), filename=pkg)
        except yum.Errors.MiscError:
            self.error(ERROR_INVALID_PACKAGE_FILE, "%s does not appear to be a valid package." % pkg)
            return False

        if self._is_inst_arch(po):
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED, "The package %s is already installed" % str(po))
            return False

        if len(self.yumbase.conf.exclude) > 0:
           exactmatch, matched, unmatched = \
                   parsePackages([po], self.yumbase.conf.exclude, casematch=1)
           if po in exactmatch + matched:
               self.error(ERROR_PACKAGE_INSTALL_BLOCKED, "Installation of %s is excluded by yum configuration." % pkg)
               return False

        return True

    def update_packages(self,package_ids):
        '''
        Implement the {backend}-install functionality
        This will only work with yum 3.2.4 or higher
        '''
        self._check_init()
        self.allow_cancel(False);
        self.percentage(0)
        self.status(STATUS_RUNNING)
        txmbrs = []
        try:
            for package in package_ids:
                pkg,inst = self._findPackage(package)
                if pkg:
                    txmbr = self.yumbase.update(po=pkg)
                    txmbrs.extend(txmbr)
        except yum.Errors.RepoError,e:
            self.error(ERROR_REPO_NOT_AVAILABLE,str(e))
        if txmbrs:
            self._runYumTransaction()
        else:
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED,"No available updates")

    def _check_for_reboot(self):
        md = self.updateMetadata
        for txmbr in self.yumbase.tsInfo:
            pkg = txmbr.po
            # check if package is in reboot list or flagged with reboot_suggested
            # in the update metadata and is installed/updated etc
            notice = md.get_notice((pkg.name,pkg.version,pkg.release))
            if (pkg.name in self.rebootpkgs \
                or (notice and notice.get_metadata().has_key('reboot_suggested') and notice['reboot_suggested']))\
                and txmbr.ts_state in TS_INSTALL_STATES:
                self.require_restart(RESTART_SYSTEM,"")
                break

    def _truncate(self, text,length,etc='...'):
        if len(text) < length:
            return text
        else:
            return text[:length] + etc

    def _format_msgs(self,msgs):
        if isinstance(msgs,basestring):
             msgs = msgs.split('\n')
        text = ";".join(msgs)
        text = self._truncate(text, 1024);
        text = text.replace("Missing Dependency: ","")
        text = text.replace(" (installed)","")
        return text

    def _runYumTransaction(self,removedeps=None):
        '''
        Run the yum Transaction
        This will only work with yum 3.2.4 or higher
        '''
        try:
            rc,msgs =  self.yumbase.buildTransaction()
        except yum.Errors.RepoError,e:
            self.error(ERROR_REPO_NOT_AVAILABLE,str(e))
        if rc !=2:
            self.error(ERROR_DEP_RESOLUTION_FAILED,self._format_msgs(msgs))
        else:
            self._check_for_reboot()
            if removedeps == False:
                if len(self.yumbase.tsInfo) > 1:
                    retmsg = 'package could not be removed, as other packages depend on it'
                    self.error(ERROR_DEP_RESOLUTION_FAILED,retmsg)
                    return

            try:
                rpmDisplay = PackageKitCallback(self)
                callback = ProcessTransPackageKitCallback(self)
                self.yumbase.processTransaction(callback=callback,
                                      rpmDisplay=rpmDisplay)
            except yum.Errors.YumDownloadError,ye:
                self.error(ERROR_PACKAGE_DOWNLOAD_FAILED,self._format_msgs(ye.value))
            except yum.Errors.YumGPGCheckError,ye:
                self.error(ERROR_BAD_GPG_SIGNATURE,self._format_msgs(ye.value))
            except GPGKeyNotImported,e:
                keyData = self.yumbase.missingGPGKey
                if not keyData:
                    self.error(ERROR_BAD_GPG_SIGNATURE,
                               "GPG key not imported, and no GPG information was found.")
                id = self._pkg_to_id(keyData['po'])
                fingerprint = keyData['fingerprint']()
                hex_fingerprint = "%02x" * len(fingerprint) % tuple(map(ord, fingerprint))
                # Borrowed from http://mail.python.org/pipermail/python-list/2000-September/053490.html

                self.repo_signature_required(id,
                                             keyData['po'].repoid,
                                             keyData['keyurl'].replace("file://",""),
                                             keyData['userid'],
                                             keyData['hexkeyid'],
                                             hex_fingerprint,
                                             time.ctime(keyData['timestamp']),
                                             'gpg')
                self.error(ERROR_GPG_FAILURE,"GPG key %s required" % keyData['hexkeyid'])
            except yum.Errors.YumBaseError,ye:
                message = self._format_msgs(ye.value)
                if message.find ("conflicts with file") != -1:
                    self.error(ERROR_FILE_CONFLICTS,message)
                else:
                    self.error(ERROR_TRANSACTION_ERROR,message)

    def remove_packages(self,allowdep,package_ids):
        '''
        Implement the {backend}-remove functionality
        Needed to be implemented in a sub class
        '''
        self._check_init()
        self.allow_cancel(False);
        self.percentage(0)
        self.status(STATUS_RUNNING)

        txmbrs = []
        for package in package_ids:
            pkg,inst = self._findPackage(package)
            if pkg and inst:
                txmbr = self.yumbase.remove(po=pkg)
                txmbrs.extend(txmbr)
            if not inst:
                self.error(ERROR_PACKAGE_NOT_INSTALLED,"The package %s is not installed" % pkg.name)
        if txmbrs:
            if allowdep != 'yes':
                self._runYumTransaction(removedeps=False)
            else:
                self._runYumTransaction(removedeps=True)
        else:
            self.error(ERROR_PACKAGE_NOT_INSTALLED,"The packages failed to be removed")

    def get_details(self,package_ids):
        '''
        Print a detailed details for a given package
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for package in package_ids:
            pkg,inst = self._findPackage(package)
            if pkg:
                self._show_details_pkg(pkg)
            else:
                self.error(ERROR_PACKAGE_NOT_FOUND,'Package %s was not found' % package)

    def _show_details_pkg(self,pkg):

        pkgver = self._get_package_ver(pkg)
        id = pkpackage.get_package_id(pkg.name,pkgver,pkg.arch,pkg.repo)
        desc = pkg.description
        desc = desc.replace('\n\n',';')
        desc = desc.replace('\n',' ')
        group = self.comps.get_group(pkg.name)
        self.details(id,pkg.license,group,desc,pkg.url,pkg.size)

    def get_files(self,package_ids):
        self._check_init()
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for package in package_ids:
            pkg,inst = self._findPackage(package)
            if pkg:
                files = pkg.returnFileEntries('dir')
                files.extend(pkg.returnFileEntries()) # regular files

                file_list = ";".join(files)

                self.files(package,file_list)
            else:
                self.error(ERROR_PACKAGE_NOT_FOUND,'Package %s was not found' % package)

    def _pkg_to_id(self,pkg):
        pkgver = self._get_package_ver(pkg)
        id = pkpackage.get_package_id(pkg.name,pkgver,pkg.arch,pkg.repo)
        return id

    def _show_package(self,pkg,status):
        '''  Show info about package'''
        id = self._pkg_to_id(pkg)
        self.package(id,status,pkg.summary)

    def _get_status(self,notice):
        ut = notice['type']
        if ut == 'security':
            return INFO_SECURITY
        elif ut == 'bugfix':
            return INFO_BUGFIX
        elif ut == 'enhancement':
            return INFO_ENHANCEMENT
        else:
            return INFO_UNKNOWN

    def get_updates(self,filters):
        '''
        Implement the {backend}-get-updates functionality
        @param filters: package types to show
        '''
        self._check_init()
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        fltlist = filters.split(';')
        package_list = []
        pkgfilter = YumFilter(fltlist)

        try:
            ygl = self.yumbase.doPackageLists(pkgnarrow='updates')
        except yum.Errors.RepoError,e:
            self.error(ERROR_REPO_NOT_AVAILABLE,str(e))
        md = self.updateMetadata
        for pkg in ygl.updates:
            if pkgfilter._do_extra_filtering(pkg):
                # Get info about package in updates info
                notice = md.get_notice((pkg.name,pkg.version,pkg.release))
                if notice:
                    status = self._get_status(notice)
                    pkgfilter.add_custom(pkg,status)
                else:
                    pkgfilter.add_custom(pkg,INFO_NORMAL)

        package_list = pkgfilter.post_process()
        self._show_package_list(package_list)

    def repo_enable(self,repoid,enable):
        '''
        Implement the {backend}-repo-enable functionality
        '''
        self._check_init()
        self.status(STATUS_INFO)
        try:
            repo = self.yumbase.repos.getRepo(repoid)
            if enable == 'false':
                if repo.isEnabled():
                    repo.disablePersistent()
            else:
                if not repo.isEnabled():
                    repo.enablePersistent()

        except yum.Errors.RepoError,e:
            self.error(ERROR_REPO_NOT_FOUND,str(e))

    def _is_development_repo(self,repo):
        if repo.endswith('-debuginfo'):
            return True
        if repo.endswith('-testing'):
            return True
        if repo.endswith('-debug'):
            return True
        if repo.endswith('-development'):
            return True
        if repo.endswith('-source'):
            return True
        return False

    def get_repo_list(self,filters):
        '''
        Implement the {backend}-get-repo-list functionality
        '''
        self._check_init()
        self.status(STATUS_INFO)

        for repo in self.yumbase.repos.repos.values():
            if filters != FILTER_NOT_DEVELOPMENT or not self._is_development_repo(repo.id):
                if repo.isEnabled():
                    self.repo_detail(repo.id,repo.name,'true')
                else:
                    self.repo_detail(repo.id,repo.name,'false')

    def _get_obsoleted(self,name):
        obsoletes = self.yumbase.up.getObsoletesTuples(newest=1)
        for (obsoleting,installed) in obsoletes:
            if obsoleting[0] == name:
                pkg =  self.yumbase.rpmdb.searchPkgTuple(installed)[0]
                return self._pkg_to_id(pkg)
        return ""

    def _get_updated(self,pkg):
        updated = None
        pkgs = self.yumbase.rpmdb.searchNevra(name=pkg.name,arch=pkg.arch)
        if pkgs:
            return self._pkg_to_id(pkgs[0])
        else:
            return ""

    def _get_update_metadata(self):
        if not self._updateMetadata:
            self._updateMetadata = UpdateMetadata()
            for repo in self.yumbase.repos.listEnabled():
                try:
                    self._updateMetadata.add(repo)
                except:
                    pass # No updateinfo.xml.gz in repo
        return self._updateMetadata

    _updateMetadata = None
    updateMetadata = property(fget=_get_update_metadata)

    def _format_str(self,str):
        """
        Convert a multi line string to a list separated by ';'
        """
        if str:
            lines = str.split('\n')
            return ";".join(lines)
        else:
            return ""

    def _format_list(self,lst):
        """
        Convert a multi line string to a list separated by ';'
        """
        if lst:
            return ";".join(lst)
        else:
            return ""

    def _get_update_extras(self,pkg):
        md = self.updateMetadata
        notice = md.get_notice((pkg.name,pkg.version,pkg.release))
        urls = {'bugzilla':[],'cve' : [],'vendor': []}
        if notice:
            # Update Details
            desc = notice['description']
            # Update References (Bugzilla,CVE ...)
            refs = notice['references']
            if refs:
                for ref in refs:
                    typ = ref['type']
                    href = ref['href']
                    title = ref['title'] or ""

                    # Description can sometimes have ';' in them, and we use that as the delimiter
                    title = title.replace(";",",")

                    if href:
                        if typ in ('bugzilla','cve'):
                            urls[typ].append("%s;%s" % (href,title))
                        else:
                            urls['vendor'].append("%s;%s" % (href,title))

            # add link to bohdi if available
            if notice['update_id']:
                href = "https://admin.fedoraproject.org/updates/%s" % notice['update_id']
                title = "%s Update %s" % (notice['release'],notice['update_id'])
                urls['vendor'].append("%s;%s" % (href,title))

            # other interesting data:
            changelog = ''
            state = notice['status'] or ''
            issued = notice['issued'] or ''
            updated = notice['updated'] or ''

            # Reboot flag
            if notice.get_metadata().has_key('reboot_suggested') and notice['reboot_suggested']:
                reboot = 'system'
            else:
                reboot = 'none'
            return self._format_str(desc),urls,reboot,changelog,state,issued,updated
        else:
            return "",urls,"none",'','','',''

    def get_update_detail(self,package_ids):
        '''
        Implement the {backend}-get-update_detail functionality
        '''
        self._check_init()
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        for package in package_ids:
            pkg,inst = self._findPackage(package)
            update = self._get_updated(pkg)
            obsolete = self._get_obsoleted(pkg.name)
            desc,urls,reboot,changelog,state,issued,updated = self._get_update_extras(pkg)
            cve_url = self._format_list(urls['cve'])
            bz_url = self._format_list(urls['bugzilla'])
            vendor_url = self._format_list(urls['vendor'])
            self.update_detail(package,update,obsolete,vendor_url,bz_url,cve_url,reboot,desc,changelog,state,issued,updated)

    def repo_set_data(self,repoid,parameter,value):
        '''
        Implement the {backend}-repo-set-data functionality
        '''
        self._check_init()
        # Get the repo
        repo = self.yumbase.repos.getRepo(repoid)
        if repo:
            repo.cfg.set(repoid,parameter,value)
            try:
                repo.cfg.write(file(repo.repofile,'w'))
            except IOError,e:
                self.error(ERROR_CANNOT_WRITE_REPO_CONFIG,str(e))
        else:
            self.error(ERROR_REPO_NOT_FOUND,'repo %s not found' % repoid)

    def install_signature(self,sigtype,key_id,package):
        self._check_init()
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        pkg,inst = self._findPackage(package)
        if pkg:
            try:
                self.yumbase.getKeyForPackage(pkg,askcb = lambda x,y,z: True)
            except yum.Errors.YumBaseError,e:
                self.error(ERROR_UNKNOWN,str(e))
            except:
                self.error(ERROR_GPG_FAILURE,"Error importing GPG Key for %s" % pkg)

    def _check_init(self,lazy_cache=False):
        '''Just does the caching tweaks'''
        if lazy_cache:
            for repo in self.yumbase.repos.listEnabled():
                repo.metadata_expire = 60 * 60 * 24  # 24 hours
                repo.mdpolicy = "group:all"
        else:
            for repo in self.yumbase.repos.listEnabled():
                repo.metadata_expire = 60 * 60 * 1.5 # 1.5 hours, the default
                repo.mdpolicy = "group:primary"

    def _refresh_yum_cache(self):
        self.status(STATUS_REFRESH_CACHE)
        old_cache_setting = self.yumbase.conf.cache
        self.yumbase.conf.cache = 0
        self.yumbase.repos.setCache(0)

        try:
            self.yumbase.repos.populateSack(mdtype='metadata',cacheonly=1)
            self.yumbase.repos.populateSack(mdtype='filelists',cacheonly=1)
            self.yumbase.repos.populateSack(mdtype='otherdata',cacheonly=1)
        except yum.Errors.RepoError,e:
            self.error(ERROR_REPO_NOT_AVAILABLE,str(e))

        self.yumbase.conf.cache = old_cache_setting
        self.yumbase.repos.setCache(old_cache_setting)

    def _setup_yum(self):
        self.yumbase.doConfigSetup(errorlevel=0,debuglevel=0)     # Setup Yum Config
        self.yumbase.conf.throttle = "90%"                        # Set bandwidth throttle to 40%
        self.dnlCallback = DownloadCallback(self,showNames=True)  # Download callback
        self.yumbase.repos.setProgressBar(self.dnlCallback)       # Setup the download callback class

class DownloadCallback(BaseMeter):
    """ Customized version of urlgrabber.progress.BaseMeter class """
    def __init__(self,base,showNames = False):
        BaseMeter.__init__(self)
        self.totSize = ""
        self.base = base
        self.showNames = showNames
        self.oldName = None
        self.lastPct = 0
        self.totalPct = 0
        self.pkgs = None
        self.numPkgs=0
        self.bump = 0.0

    def setPackages(self,pkgs,startPct,numPct):
        self.pkgs = pkgs
        self.numPkgs = float(len(self.pkgs))
        self.bump = numPct/self.numPkgs
        self.totalPct = startPct

    def _getPackage(self,name):
        if self.pkgs:
            for pkg in self.pkgs:
                if isinstance(pkg,YumLocalPackage):
                    rpmfn = pkg.localPkg
                else:
                    rpmfn = os.path.basename(pkg.remote_path) # get the rpm filename of the package
                if rpmfn == name:
                    return pkg
        return None

    def update(self,amount_read,now=None):
        BaseMeter.update(self,amount_read,now)

    def _do_start(self,now=None):
        name = self._getName()
        self.updateProgress(name,0.0,"","")
        if not self.size is None:
            self.totSize = format_number(self.size)

    def _do_update(self,amount_read,now=None):
        fread = format_number(amount_read)
        name = self._getName()
        if self.size is None:
            # Elapsed time
            etime = self.re.elapsed_time()
            fetime = format_time(etime)
            frac = 0.0
            self.updateProgress(name,frac,fread,fetime)
        else:
            # Remaining time
            rtime = self.re.remaining_time()
            frtime = format_time(rtime)
            frac = self.re.fraction_read()
            self.updateProgress(name,frac,fread,frtime)

    def _do_end(self,amount_read,now=None):
        total_time = format_time(self.re.elapsed_time())
        total_size = format_number(amount_read)
        name = self._getName()
        self.updateProgress(name,1.0,total_size,total_time)

    def _getName(self):
        '''
        Get the name of the package being downloaded
        '''
        return self.basename

    def updateProgress(self,name,frac,fread,ftime):
        '''
         Update the progressbar (Overload in child class)
        @param name: filename
        @param frac: Progress fracment (0 -> 1)
        @param fread: formated string containing BytesRead
        @param ftime : formated string containing remaining or elapsed time
        '''
        pct = int(frac*100)
        if name != self.oldName: # If this a new package
            if self.oldName:
                self.base.sub_percentage(100)
            self.oldName = name
            if self.bump > 0.0: # Bump the total download percentage
                self.totalPct += self.bump
                self.lastPct = 0
                self.base.percentage(int(self.totalPct))
            if self.showNames:
                pkg = self._getPackage(name)
                if pkg: # show package to download
                    self.base._show_package(pkg,INFO_DOWNLOADING)
                else:
                    for key in MetaDataMap.keys():
                        if key in name:
                            typ = MetaDataMap[key]
                            self.base.status(typ)
                            break
            self.base.sub_percentage(0)
        else:
            if self.lastPct != pct and pct != 0 and pct != 100:
                self.lastPct = pct
                # bump the sub persentage for this package
                self.base.sub_percentage(pct)

class PackageKitCallback(RPMBaseCallback):
    def __init__(self,base):
        RPMBaseCallback.__init__(self)
        self.base = base
        self.pct = 0
        self.curpkg = None
        self.startPct = 50
        self.numPct = 50

        # this isn't defined in yum as it's only used in the rollback plugin
        TS_REPACKAGING = 'repackaging'

        # Map yum transactions with pk info enums
        self.info_actions = { TS_UPDATE : INFO_UPDATING,
                        TS_ERASE: INFO_REMOVING,
                        TS_INSTALL: INFO_INSTALLING,
                        TS_TRUEINSTALL : INFO_INSTALLING,
                        TS_OBSOLETED: INFO_OBSOLETING,
                        TS_OBSOLETING: INFO_INSTALLING,
                        TS_UPDATED: INFO_CLEANUP}

        # Map yum transactions with pk state enums
        self.state_actions = { TS_UPDATE : STATUS_UPDATE,
                        TS_ERASE: STATUS_REMOVE,
                        TS_INSTALL: STATUS_INSTALL,
                        TS_TRUEINSTALL : STATUS_INSTALL,
                        TS_OBSOLETED: STATUS_OBSOLETE,
                        TS_OBSOLETING: STATUS_INSTALL,
                        TS_UPDATED: STATUS_CLEANUP,
                        TS_REPACKAGING: STATUS_REPACKAGING}

    def _calcTotalPct(self,ts_current,ts_total):
        bump = float(self.numPct)/ts_total
        pct = int(self.startPct + (ts_current * bump))
        return pct

    def _showName(self,status):
        if type(self.curpkg) in types.StringTypes:
            id = pkpackage.get_package_id(self.curpkg,'','','')
        else:
            pkgver = self.base._get_package_ver(self.curpkg)
            id = pkpackage.get_package_id(self.curpkg.name,pkgver,self.curpkg.arch,self.curpkg.repo)
        self.base.package(id,status,"")

    def event(self,package,action,te_current,te_total,ts_current,ts_total):
        if str(package) != str(self.curpkg):
            self.curpkg = package
            try:
                self.base.status(self.state_actions[action])
                self._showName(self.info_actions[action])
            except exceptions.KeyError,e:
                self.base.message(MESSAGE_WARNING,"The constant '%s' was unknown, please report" % action)
            pct = self._calcTotalPct(ts_current,ts_total)
            self.base.percentage(pct)
        val = (ts_current*100L)/ts_total
        if val != self.pct:
            self.pct = val
            self.base.sub_percentage(val)

    def errorlog(self,msg):
        # grrrrrrrr
        pass

class ProcessTransPackageKitCallback:
    def __init__(self,base):
        self.base = base

    def event(self,state,data=None):
        if state == PT_DOWNLOAD:        # Start Downloading
            self.base.allow_cancel(True)
            self.base.percentage(10)
            self.base.status(STATUS_DOWNLOAD)
        if state == PT_DOWNLOAD_PKGS:   # Packages to download
            self.base.dnlCallback.setPackages(data,10,30)
        elif state == PT_GPGCHECK:
            self.base.percentage(40)
            self.base.status(STATUS_SIG_CHECK)
            pass
        elif state == PT_TEST_TRANS:
            self.base.allow_cancel(False)
            self.base.percentage(45)
            self.base.status(STATUS_TEST_COMMIT)
            pass
        elif state == PT_TRANSACTION:
            self.base.allow_cancel(False)
            self.base.percentage(50)
            pass

class DepSolveCallback(object):

    # takes a PackageKitBackend so we can call StatusChanged on it.
    # That's kind of hurky.
    def __init__(self,backend):
        self.started = False
        self.backend = backend

    def start(self):
       if not self.started:
           self.backend.status(STATUS_DEP_RESOLVE)
           self.backend.percentage(None)

    # Be lazy and not define the others explicitly
    def _do_nothing(self,*args,**kwargs):
        pass

    def __getattr__(self,x):
        return self._do_nothing

class PackageKitYumBase(yum.YumBase):
    """
    Subclass of YumBase.  Needed so we can overload _checkSignatures
    and nab the gpg sig data
    """

    def __init__(self,backend):
        yum.YumBase.__init__(self)
        self.missingGPGKey = None
        self.dsCallback = DepSolveCallback(backend)

    def _checkSignatures(self,pkgs,callback):
        ''' The the signatures of the downloaded packages '''
        # This can be overloaded by a subclass.

        for po in pkgs:
            result,errmsg = self.sigCheckPkg(po)
            if result == 0:
                # verified ok, or verify not required
                continue
            elif result == 1:
                # verify failed but installation of the correct GPG key might help
                self.getKeyForPackage(po,fullaskcb=self._fullAskForGPGKeyImport)
            else:
                # fatal GPG verification error
                raise yum.Errors.YumGPGCheckError,errmsg
        return 0

    def _fullAskForGPGKeyImport(self,data):
        self.missingGPGKey = data

        raise GPGKeyNotImported()

    def _askForGPGKeyImport(self,po,userid,hexkeyid):
        '''
        Ask for GPGKeyImport
        '''
        # TODO: Add code here to send the RepoSignatureRequired signal
        return False
