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

# Copyright (C) 2007
#    Tim Lauridsen <timlau@fedoraproject.org>
#    Seth Vidal <skvidal@fedoraproject.org>
#    Luke Macken <lmacken@redhat.com>
#    James Bowes <jbowes@dangerouslyinc.com>
#    Robin Norwood <rnorwood@redhat.com>

# imports

import re

from packagekit.backend import *
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
from packagekit.backend import PackagekitProgress

# Global vars
yumbase = None
progress = PackagekitProgress()  # Progress object to store the progress

groupMap = {
'desktops;gnome-desktop'                      : GROUP_DESKTOP_GNOME,
'desktops;window-managers'                    : GROUP_DESKTOP_OTHER,
'desktops;kde-desktop'                        : GROUP_DESKTOP_KDE,
'desktops;xfce-desktop'                       : GROUP_DESKTOP_XFCE,
'apps;authoring-and-publishing'               : GROUP_PUBLISHING,
'apps;office'                                 : GROUP_OFFICE,
'apps;sound-and-video'                        : GROUP_MULTIMEDIA,
'apps;editors'                                : GROUP_OFFICE,
'apps;engineering-and-scientific'             : GROUP_OTHER,
'apps;games'                                  : GROUP_GAMES,
'apps;graphics'                               : GROUP_GRAPHICS,
'apps;text-internet'                          : GROUP_INTERNET,
'apps;graphical-internet'                     : GROUP_INTERNET,
'apps;education'                              : GROUP_EDUCATION,
'development;kde-software-development'        : GROUP_PROGRAMMING,
'development;gnome-software-development'      : GROUP_PROGRAMMING,
'development;development-tools'               : GROUP_PROGRAMMING,
'development;eclipse'                         : GROUP_PROGRAMMING,
'development;development-libs'                : GROUP_PROGRAMMING,
'development;x-software-development'          : GROUP_PROGRAMMING,
'development;web-development'                 : GROUP_PROGRAMMING,
'development;legacy-software-development'     : GROUP_PROGRAMMING,
'development;ruby'                            : GROUP_PROGRAMMING,
'development;java-development'                : GROUP_PROGRAMMING,
'development;xfce-software-development'       : GROUP_PROGRAMMING,
'servers;clustering'                          : GROUP_SERVERS,
'servers;dns-server'                          : GROUP_SERVERS,
'servers;server-cfg'                          : GROUP_SERVERS,
'servers;news-server'                         : GROUP_SERVERS,
'servers;web-server'                          : GROUP_SERVERS,
'servers;smb-server'                          : GROUP_SERVERS,
'servers;sql-server'                          : GROUP_SERVERS,
'servers;ftp-server'                          : GROUP_SERVERS,
'servers;printing'                            : GROUP_SERVERS,
'servers;mysql'                               : GROUP_SERVERS,
'servers;mail-server'                         : GROUP_SERVERS,
'servers;network-server'                      : GROUP_SERVERS,
'servers;legacy-network-server'               : GROUP_SERVERS,
'base-system;java'                            : GROUP_SYSTEM,
'base-system;base-x'                          : GROUP_SYSTEM,
'base-system;system-tools'                    : GROUP_ADMIN_TOOLS,
'base-system;fonts'                           : GROUP_FONTS,
'base-system;hardware-support'                : GROUP_SYSTEM,
'base-system;dial-up'                         : GROUP_SYSTEM,
'base-system;admin-tools'                     : GROUP_ADMIN_TOOLS,
'base-system;legacy-software-support'         : GROUP_LEGACY,
'base-system;base'                            : GROUP_SYSTEM,
'base-system;virtualization'                  : GROUP_VIRTUALIZATION,
'base-system;legacy-fonts'                    : GROUP_FONTS,
'language-support;khmer-support'              : GROUP_LOCALIZATION,
'language-support;persian-support'            : GROUP_LOCALIZATION,
'language-support;georgian-support'           : GROUP_LOCALIZATION,
'language-support;malay-support'              : GROUP_LOCALIZATION,
'language-support;tonga-support'              : GROUP_LOCALIZATION,
'language-support;portuguese-support'         : GROUP_LOCALIZATION,
'language-support;japanese-support'           : GROUP_LOCALIZATION,
'language-support;hungarian-support'          : GROUP_LOCALIZATION,
'language-support;somali-support'             : GROUP_LOCALIZATION,
'language-support;punjabi-support'            : GROUP_LOCALIZATION,
'language-support;bhutanese-support'          : GROUP_LOCALIZATION,
'language-support;british-support'            : GROUP_LOCALIZATION,
'language-support;korean-support'             : GROUP_LOCALIZATION,
'language-support;lao-support'                : GROUP_LOCALIZATION,
'language-support;inuktitut-support'          : GROUP_LOCALIZATION,
'language-support;german-support'             : GROUP_LOCALIZATION,
'language-support;hindi-support'              : GROUP_LOCALIZATION,
'language-support;faeroese-support'           : GROUP_LOCALIZATION,
'language-support;swedish-support'            : GROUP_LOCALIZATION,
'language-support;tsonga-support'             : GROUP_LOCALIZATION,
'language-support;russian-support'            : GROUP_LOCALIZATION,
'language-support;serbian-support'            : GROUP_LOCALIZATION,
'language-support;latvian-support'            : GROUP_LOCALIZATION,
'language-support;samoan-support'             : GROUP_LOCALIZATION,
'language-support;sinhala-support'            : GROUP_LOCALIZATION,
'language-support;catalan-support'            : GROUP_LOCALIZATION,
'language-support;lithuanian-support'         : GROUP_LOCALIZATION,
'language-support;turkish-support'            : GROUP_LOCALIZATION,
'language-support;arabic-support'             : GROUP_LOCALIZATION,
'language-support;vietnamese-support'         : GROUP_LOCALIZATION,
'language-support;mongolian-support'          : GROUP_LOCALIZATION,
'language-support;tswana-support'             : GROUP_LOCALIZATION,
'language-support;irish-support'              : GROUP_LOCALIZATION,
'language-support;italian-support'            : GROUP_LOCALIZATION,
'language-support;slovak-support'             : GROUP_LOCALIZATION,
'language-support;slovenian-support'          : GROUP_LOCALIZATION,
'language-support;belarusian-support'         : GROUP_LOCALIZATION,
'language-support;northern-sotho-support'     : GROUP_LOCALIZATION,
'language-support;kannada-support'            : GROUP_LOCALIZATION,
'language-support;malayalam-support'          : GROUP_LOCALIZATION,
'language-support;swati-support'              : GROUP_LOCALIZATION,
'language-support;breton-support'             : GROUP_LOCALIZATION,
'language-support;romanian-support'           : GROUP_LOCALIZATION,
'language-support;greek-support'              : GROUP_LOCALIZATION,
'language-support;tagalog-support'            : GROUP_LOCALIZATION,
'language-support;zulu-support'               : GROUP_LOCALIZATION,
'language-support;tibetan-support'            : GROUP_LOCALIZATION,
'language-support;danish-support'             : GROUP_LOCALIZATION,
'language-support;afrikaans-support'          : GROUP_LOCALIZATION,
'language-support;southern-sotho-support'     : GROUP_LOCALIZATION,
'language-support;bosnian-support'            : GROUP_LOCALIZATION,
'language-support;brazilian-support'          : GROUP_LOCALIZATION,
'language-support;basque-support'             : GROUP_LOCALIZATION,
'language-support;welsh-support'              : GROUP_LOCALIZATION,
'language-support;thai-support'               : GROUP_LOCALIZATION,
'language-support;telugu-support'             : GROUP_LOCALIZATION,
'language-support;low-saxon-support'          : GROUP_LOCALIZATION,
'language-support;urdu-support'               : GROUP_LOCALIZATION,
'language-support;tamil-support'              : GROUP_LOCALIZATION,
'language-support;indonesian-support'         : GROUP_LOCALIZATION,
'language-support;gujarati-support'           : GROUP_LOCALIZATION,
'language-support;xhosa-support'              : GROUP_LOCALIZATION,
'language-support;chinese-support'            : GROUP_LOCALIZATION,
'language-support;czech-support'              : GROUP_LOCALIZATION,
'language-support;venda-support'              : GROUP_LOCALIZATION,
'language-support;bulgarian-support'          : GROUP_LOCALIZATION,
'language-support;albanian-support'           : GROUP_LOCALIZATION,
'language-support;galician-support'           : GROUP_LOCALIZATION,
'language-support;armenian-support'           : GROUP_LOCALIZATION,
'language-support;dutch-support'              : GROUP_LOCALIZATION,
'language-support;oriya-support'              : GROUP_LOCALIZATION,
'language-support;maori-support'              : GROUP_LOCALIZATION,
'language-support;nepali-support'             : GROUP_LOCALIZATION,
'language-support;icelandic-support'          : GROUP_LOCALIZATION,
'language-support;ukrainian-support'          : GROUP_LOCALIZATION,
'language-support;assamese-support'           : GROUP_LOCALIZATION,
'language-support;bengali-support'            : GROUP_LOCALIZATION,
'language-support;spanish-support'            : GROUP_LOCALIZATION,
'language-support;hebrew-support'             : GROUP_LOCALIZATION,
'language-support;estonian-support'           : GROUP_LOCALIZATION,
'language-support;french-support'             : GROUP_LOCALIZATION,
'language-support;croatian-support'           : GROUP_LOCALIZATION,
'language-support;filipino-support'           : GROUP_LOCALIZATION,
'language-support;finnish-support'            : GROUP_LOCALIZATION,
'language-support;norwegian-support'          : GROUP_LOCALIZATION,
'language-support;southern-ndebele-support'   : GROUP_LOCALIZATION,
'language-support;polish-support'             : GROUP_LOCALIZATION,
'language-support;gaelic-support'             : GROUP_LOCALIZATION,
'language-support;marathi-support'            : GROUP_LOCALIZATION,
'language-support;ethiopic-support'           : GROUP_LOCALIZATION
}

MetaDataMap = {
    'repomd'        : STATUS_DOWNLOAD_REPOSITORY,
    'primary'       : STATUS_DOWNLOAD_PACKAGELIST,
    'filelists'     : STATUS_DOWNLOAD_FILELIST,
    'other'         : STATUS_DOWNLOAD_CHANGELOG,
    'comps'         : STATUS_DOWNLOAD_GROUP,
    'updateinfo'    : STATUS_DOWNLOAD_UPDATEINFO
}

GUI_KEYS = re.compile(r'(qt)|(gtk)')

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
        seen_nevra = [] # yum returns packages as available even when installed
        pkg_list = [] # only do the second iteration on not installed pkgs
        package_list = [] #we can't do emitting as found if we are post-processing

        for (pkg,values) in res:
            if pkg.repo.id == 'installed':
                if self._do_extra_filtering(pkg,fltlist):
                    package_list.append((pkg,INFO_INSTALLED))
                    seen_nevra.append(self._get_nevra(pkg))
            else:
                pkg_list.append(pkg)
        for pkg in pkg_list:
            nevra = self._get_nevra(pkg)
            if nevra not in seen_nevra:
                if self._do_extra_filtering(pkg,fltlist):
                    package_list.append((pkg,INFO_AVAILABLE))
                    seen_nevra.append(self._get_nevra(pkg))

        # basename filter if specified
        if FILTER_BASENAME in fltlist:
            package_list = self._basename_filter(package_list)

        # newest filter
        if FILTER_NEWEST in fltlist:
            package_list = self._do_newest_filtering(package_list)

        self._show_package_list(package_list)

    def _show_package_list(self,lst):
        for (pkg,status) in lst:
            self._show_package(pkg,status)

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

        isFree = self.check_license_field(pkg.license)

        return isFree == wantFree

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

    def _buildGroupDict(self):
        pkgGroups= {}
        cats = self.yumbase.comps.categories
        if len(cats) == 0:
            self.error(ERROR_GROUP_LIST_INVALID,'comps categories could not be loaded')
        for cat in cats:
            grps = map(lambda x: self.yumbase.comps.return_group(x),
               filter(lambda x: self.yumbase.comps.has_group(x),cat.groups))
            grplist = []
            for group in grps:
                for pkg in group.mandatory_packages.keys():
                    pkgGroups[pkg] = "%s;%s" % (cat.categoryid,group.groupid)
                for pkg in group.default_packages.keys():
                    pkgGroups[pkg] = "%s;%s" % (cat.categoryid,group.groupid)
                for pkg in group.optional_packages.keys():
                    pkgGroups[pkg] = "%s;%s" % (cat.categoryid,group.groupid)
                for pkg in group.conditional_packages.keys():
                    pkgGroups[pkg] = "%s;%s" % (cat.categoryid,group.groupid)
        return pkgGroups

    @handle_repo_error
    def search_group(self,filters,key):
        '''
        Implement the {backend}-search-group functionality
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True)
        self.percentage(None)
        self.yumbase.doConfigSetup(errorlevel=0,debuglevel=0)# Setup Yum Config
        self.yumbase.conf.cache = 1 # Only look in cache.
        self.status(STATUS_QUERY)
        package_list = [] #we can't do emitting as found if we are post-processing

        try:
            pkgGroupDict = self._buildGroupDict()
            self.yumbase.conf.cache = 1 # Only look in cache.
            fltlist = filters.split(';')
            installed_nevra = [] # yum returns packages as available even when installed

            if not FILTER_NOT_INSTALLED in fltlist:
                # Check installed for group
                for pkg in self.yumbase.rpmdb:
                    group = GROUP_OTHER                    # Default Group
                    if pkgGroupDict.has_key(pkg.name):     # check if pkg name exist in package / group dictinary
                        cg = pkgGroupDict[pkg.name]
                        if groupMap.has_key(cg):
                            group = groupMap[cg]           # use the pk group name, instead of yum 'category/group'
                    if group == key:
                        if self._do_extra_filtering(pkg,fltlist):
                            package_list.append((pkg,INFO_INSTALLED))
                    installed_nevra.append(self._get_nevra(pkg))

            if not FILTER_INSTALLED in fltlist:
                # Check available for group
                for pkg in self.yumbase.pkgSack:
                    nevra = self._get_nevra(pkg)
                    if nevra not in installed_nevra:
                        group = GROUP_OTHER
                        if pkgGroupDict.has_key(pkg.name):
                            cg = pkgGroupDict[pkg.name]
                            if groupMap.has_key(cg):
                                group = groupMap[cg]
                        if group == key:
                            if self._do_extra_filtering(pkg,fltlist):
                                package_list.append((pkg,INFO_AVAILABLE))

        except yum.Errors.GroupsError,e:
            self.error(ERROR_GROUP_NOT_FOUND,str(e))

        # basename filter if specified
        if FILTER_BASENAME in fltlist:
            package_list = self._basename_filter(package_list)

        # newest filter
        if FILTER_NEWEST in fltlist:
            package_list = self._do_newest_filtering(package_list)

        self._show_package_list(package_list)

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

        # Now show installed packages.
        if FILTER_NOT_INSTALLED not in fltlist:
            for pkg in self.yumbase.rpmdb:
                if self._do_extra_filtering(pkg,fltlist):
                    package_list.append((pkg,INFO_INSTALLED))

        # Now show available packages.
        if FILTER_INSTALLED not in fltlist:
            for pkg in self.yumbase.pkgSack.returnNewestByNameArch():
                if self._do_extra_filtering(pkg,fltlist):
                    package_list.append((pkg,INFO_AVAILABLE))

        # basename filter if specified
        if FILTER_BASENAME in fltlist:
            package_list = self._basename_filter(package_list)

        # newest filter
        if FILTER_NEWEST in fltlist:
            package_list = self._do_newest_filtering(package_list)

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
        found = {}
        if not FILTER_NOT_INSTALLED in fltlist:
            # Check installed for file
            matches = self.yumbase.rpmdb.searchFiles(key)
            for pkg in matches:
                if not found.has_key(str(pkg)):
                    if self._do_extra_filtering(pkg,fltlist):
                        self._show_package(pkg,INFO_INSTALLED)
                        found[str(pkg)] = 1
        if not FILTER_INSTALLED in fltlist:
            # Check available for file
            self.yumbase.repos.populateSack(mdtype='filelists')
            matches = self.yumbase.pkgSack.searchFiles(key)
            for pkg in matches:
                if not found.has_key(str(pkg)):
                    if self._do_extra_filtering(pkg,fltlist):
                        self._show_package(pkg,INFO_AVAILABLE)
                        found[str(pkg)] = 1

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
        found = {}
        if not FILTER_NOT_INSTALLED in fltlist:
            # Check installed for file
            matches = self.yumbase.rpmdb.searchProvides(search)
            for pkg in matches:
                if not found.has_key(str(pkg)):
                    if self._do_extra_filtering(pkg,fltlist):
                        self._show_package(pkg,INFO_INSTALLED)
                        found[str(pkg)] = 1
        if not FILTER_INSTALLED in fltlist:
            # Check available for file
            matches = self.yumbase.pkgSack.searchProvides(search)
            for pkg in matches:
                if found.has_key(str(pkg)):
                    if self._do_extra_filtering(pkg,fltlist):
                        self._show_package(pkg,INFO_AVAILABLE)
                        found[str(pkg)] = 1

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
            (n,idver,a,d) = self.get_package_from_id(id)
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

    def get_requires(self,filters,package,recursive):
        '''
        Print a list of requires for a given package
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        pkg,inst = self._findPackage(package)
        # FIXME: This is a hack, it simulates a removal of the
        # package and return the transaction
        if inst and pkg:
            txmbrs = self.yumbase.remove(po=pkg)
            if txmbrs:
                rc,msgs =  self.yumbase.buildTransaction()
                if rc !=2:
                    self.error(ERROR_DEP_RESOLUTION_FAILED,self._format_msgs(msgs))
                else:
                    for txmbr in self.yumbase.tsInfo:
                        if txmbr.po.name != pkg.name:
                            self._show_package(txmbr.po,INFO_INSTALLED)

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

    def _get_best_dependencies(self,po):
        ''' find the most recent packages that provides the dependencies for a package
        @param po: yum package object to find deps for
        @return: a list for yum package object providing the dependencies
        '''
        results = self.yumbase.findDeps([po])
        pkg = results.keys()[0]
        bestdeps=[]
        if len(results[pkg].keys()) == 0: # No dependencies for this package ?
            return bestdeps
        for req in results[pkg].keys():
            reqlist = results[pkg][req]
            if not reqlist: #  Unsatisfied dependency
                self.error(ERROR_DEP_RESOLUTION_FAILED,"the (%s) requirement could not be resolved" % prco_tuple_to_string(req),exit=False)
                continue
            best = None
            for po in reqlist:
                if best:
                    if po.EVR > best.EVR:
                        best=po
                else:
                    best= po
            bestdeps.append(best)
        return unique(bestdeps)

    def get_depends(self,filters,package,recursive):
        '''
        Print a list of depends for a given package
        '''
        self._check_init(lazy_cache=True)
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        fltlist = filters.split(';')
        name = package.split(';')[0]

        pkg,inst = self._findPackage(package)
        results = {}
        if pkg:
            deps = self._get_best_dependencies(pkg)
        else:
            self.error(ERROR_PACKAGE_NOT_FOUND,'Package %s was not found' % package)
        for pkg in deps:
            if pkg.name != name:
                pkgver = self._get_package_ver(pkg)
                id = self.get_package_id(pkg.name,pkgver,pkg.arch,pkg.repoid)

                if self._is_inst_arch(pkg) and FILTER_NOT_INSTALLED not in fltlist:
                    self.package(id,INFO_INSTALLED,pkg.summary)
                else:
                    if self._installable(pkg) and FILTER_INSTALLED not in fltlist:
                        self.package(id,INFO_AVAILABLE,pkg.summary)

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

    @handle_repo_error
    def resolve(self,filters,name):
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
        # Get installed packages
        installedByKey = self.yumbase.rpmdb.searchNevra(name=name)
        if FILTER_NOT_INSTALLED not in fltlist:
            for pkg in installedByKey:
                self._show_package(pkg,INFO_INSTALLED)
        # Get available packages
        if FILTER_INSTALLED not in fltlist:
            for pkg in self.yumbase.pkgSack.returnNewestByNameArch():
                if pkg.name == name:
                    show = True
                    for instpo in installedByKey:
                        # Check if package have a smaller & equal EVR to a inst pkg
                        if pkg.EVR < instpo.EVR or pkg.EVR == instpo.EVR:
                            show = False
                    if show:
                        self._show_package(pkg,INFO_AVAILABLE)

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

    def install_files (self,trusted,inst_files):
        '''
        Implement the {backend}-install-files functionality
        Install the package containing the inst_file file
        Needed to be implemented in a sub class
        '''
        for inst_file in inst_files:
            if inst_file.endswith('.src.rpm'):
                self.error(ERROR_CANNOT_INSTALL_SOURCE_PACKAGE,'Backend will not install a src rpm file')
                return
        for inst_file in inst_files:
            if not inst_file.endswith('.rpm'):
                self.error(ERROR_INVALID_PACKAGE_FILE,'Only rpm packages are supported')
                return
        self._check_init()
        self.allow_cancel(False);
        self.percentage(0)
        self.status(STATUS_RUNNING)

        pkgs_to_inst = []

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

    def update_packages(self,packages):
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
            for package in packages:
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

    def get_details(self,package):
        '''
        Print a detailed details for a given package
        '''
        self._check_init()
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        pkg,inst = self._findPackage(package)
        if pkg:
            self._show_details(pkg)
        else:
            self.error(ERROR_PACKAGE_NOT_FOUND,'Package %s was not found' % package)

    def _show_details(self,pkg):
        pkgver = self._get_package_ver(pkg)
        id = self.get_package_id(pkg.name,pkgver,pkg.arch,pkg.repo)
        desc = pkg.description
        desc = desc.replace('\n\n',';')
        desc = desc.replace('\n',' ')

        # this takes oodles of time
        pkgGroupDict = self._buildGroupDict()
        group = GROUP_OTHER
        if pkgGroupDict.has_key(pkg.name):
            cg = pkgGroupDict[pkg.name]
            if groupMap.has_key(cg):
                # use PK group name
                group = groupMap[cg]

        self.details(id,pkg.license,group,desc,pkg.url,pkg.size)

    def get_files(self,package):
        self._check_init()
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

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
        id = self.get_package_id(pkg.name,pkgver,pkg.arch,pkg.repo)
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

        try:
            ygl = self.yumbase.doPackageLists(pkgnarrow='updates')
        except yum.Errors.RepoError,e:
            self.error(ERROR_REPO_NOT_AVAILABLE,str(e))
        md = self.updateMetadata
        for pkg in ygl.updates:
            if self._do_extra_filtering(pkg,fltlist):
                # Get info about package in updates info
                notice = md.get_notice((pkg.name,pkg.version,pkg.release))
                if notice:
                    status = self._get_status(notice)
                    package_list.append((pkg,status))
                else:
                    package_list.append((pkg,INFO_NORMAL))

        # basename filter if specified
        if FILTER_BASENAME in fltlist:
            for (pkg,status) in self._basename_filter(package_list):
                self._show_package(pkg,status)
        else:
            for (pkg,status) in package_list:
                self._show_package(pkg,status)

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

            # Reboot flag
            if notice.get_metadata().has_key('reboot_suggested') and notice['reboot_suggested']:
                reboot = 'system'
            else:
                reboot = 'none'
            return self._format_str(desc),urls,reboot
        else:
            return "",urls,"none"

    def get_update_detail(self,package):
        '''
        Implement the {backend}-get-update_detail functionality
        '''
        self._check_init()
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        pkg,inst = self._findPackage(package)
        update = self._get_updated(pkg)
        obsolete = self._get_obsoleted(pkg.name)
        desc,urls,reboot = self._get_update_extras(pkg)
        cve_url = self._format_list(urls['cve'])
        bz_url = self._format_list(urls['bugzilla'])
        vendor_url = self._format_list(urls['vendor'])
        self.update_detail(package,update,obsolete,vendor_url,bz_url,cve_url,reboot,desc)

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
            id = self.base.get_package_id(self.curpkg,'','','')
        else:
            pkgver = self.base._get_package_ver(self.curpkg)
            id = self.base.get_package_id(self.curpkg.name,pkgver,self.curpkg.arch,self.curpkg.repo)
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

    # XXX takes a PackageKitBackend so we can call StatusChanged on it.
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
