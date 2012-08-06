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

# Copyright (C) 2007-2009
#    Tim Lauridsen <timlau@fedoraproject.org>
#    Seth Vidal <skvidal@fedoraproject.org>
#    Luke Macken <lmacken@redhat.com>
#    James Bowes <jbowes@dangerouslyinc.com>
#    Robin Norwood <rnorwood@redhat.com>
#
# Copyright (C) 2007-2010
#    Richard Hughes <richard@hughsie.com>
#
# Copyright (C) 2009
#    MediaGrabber, based on the logic of pirut by Jeremy Katz <katzj@redhat.com>
#    Muayyad Alsadi <alsadi@ojuba.org>

# imports
from packagekit.backend import *
from packagekit.progress import *
from packagekit.enums import *
from packagekit.package import PackagekitPackage
import yum
from urlgrabber.progress import BaseMeter, format_number
from urlgrabber.grabber import URLGrabber, URLGrabError
from yum.rpmtrans import RPMBaseCallback
from yum.constants import *
from yum.update_md import UpdateMetadata
from yum.callbacks import *
from yum.misc import prco_tuple_to_string, unique
from yum.packages import YumLocalPackage, parsePackages
from yum.packageSack import MetaSack
import rpmUtils
import exceptions
import types
import signal
import time
import os.path
import logging
import socket
import gio

import tarfile
import tempfile
import shutil
import ConfigParser

from yumFilter import *
from yumComps import *

# Global vars
yumbase = None
progress = PackagekitProgress()  # Progress object to store the progress

MetaDataMap = {
    'repomd'        : STATUS_DOWNLOAD_REPOSITORY,
    'primary'       : STATUS_DOWNLOAD_PACKAGELIST,
    'filelists'     : STATUS_DOWNLOAD_FILELIST,
    'other'         : STATUS_DOWNLOAD_CHANGELOG,
    'comps'         : STATUS_DOWNLOAD_GROUP,
    'updateinfo'    : STATUS_DOWNLOAD_UPDATEINFO
}

StatusPercentageMap = {
    STATUS_DEP_RESOLVE : 5,
    STATUS_DOWNLOAD    : 10,
    STATUS_SIG_CHECK   : 40,
    STATUS_TEST_COMMIT : 45,
    STATUS_INSTALL     : 55,
    STATUS_CLEANUP     : 95
}

# this isn't defined in yum as it's only used in the rollback plugin
TS_REPACKAGING = 'repackaging'

# Map yum transactions with pk info enums
TransactionsInfoMap = {
    TS_UPDATE       : INFO_UPDATING,
    TS_ERASE        : INFO_REMOVING,
    TS_INSTALL      : INFO_INSTALLING,
    TS_TRUEINSTALL  : INFO_INSTALLING,
    TS_OBSOLETED    : INFO_OBSOLETING,
    TS_OBSOLETING   : INFO_INSTALLING,
    TS_UPDATED      : INFO_CLEANUP
}

# Map yum transactions with pk state enums
TransactionsStateMap = {
    TS_UPDATE       : STATUS_UPDATE,
    TS_ERASE        : STATUS_REMOVE,
    TS_INSTALL      : STATUS_INSTALL,
    TS_TRUEINSTALL  : STATUS_INSTALL,
    TS_OBSOLETED    : STATUS_OBSOLETE,
    TS_OBSOLETING   : STATUS_INSTALL,
    TS_UPDATED      : STATUS_CLEANUP,
    TS_REPACKAGING  : STATUS_REPACKAGING
}

class GPGKeyNotImported(exceptions.Exception):
    pass

def sigquit(signum, frame):
    if yumbase:
        yumbase.closeRpmDB()
        yumbase.doUnlock(YUM_PID_FILE)
    sys.exit(1)

def _to_unicode(txt, encoding='utf-8'):
    if isinstance(txt, basestring):
        if not isinstance(txt, unicode):
            txt = unicode(txt, encoding, errors='replace')
    return txt

def _get_package_ver(po):
    ''' return the a ver as epoch:version-release or version-release, if epoch=0'''
    ver = ''
    if po.epoch != '0':
        ver = "%s:%s-%s" % (po.epoch, po.version, po.release)
    elif po.release:
        ver = "%s-%s" % (po.version, po.release)
    elif po.version:
        ver = po.version
    return ver

def _format_package_id(package_id):
    """
    Convert 'hal;0.5.8;i386;fedora' to 'hal-0.5.8-fedora(i386)'
    """
    parts = package_id.split(';')
    if len(parts) != 4:
        return "incorrect package_id: %s" % package_id
    return "%s-%s(%s)%s" % (parts[0], parts[1], parts[2], parts[3])

def _format_str(text):
    """
    Convert a multi line string to a list separated by ';'
    """
    if text:
        lines = text.split('\n')
        return ";".join(lines)
    else:
        return ""

def _format_list(lst):
    """
    Convert a multi line string to a list separated by ';'
    """
    if lst:
        return ";".join(lst)
    else:
        return ""

def _getEVR(idver):
    '''
    get the e, v, r from the package id version
    '''
    cpos = idver.find(':')
    if cpos != -1:
        epoch = idver[:cpos]
        idver = idver[cpos+1:]
    else:
        epoch = '0'
    try:
        (version, release) = tuple(idver.split('-'))
    except ValueError, e:
        version = '0'
        release = '0'
    return epoch, version, release

def _truncate(text, length, etc='...'):
    if len(text) < length:
        return text
    else:
        return text[:length] + etc

def _is_development_repo(repo):
    if repo.endswith('-debuginfo'):
        return True
    if repo.endswith('-debug'):
        return True
    if repo.endswith('-development'):
        return True
    if repo.endswith('-source'):
        return True
    return False

def _format_msgs(msgs):
    if isinstance(msgs, basestring):
        msgs = msgs.split('\n')

    # yum can pass us structures (!) in the message field
    try:
        text = ";".join(msgs)
    except exceptions.TypeError, e:
        text = str(msgs)
    except Exception, e:
        text = _format_str(traceback.format_exc())

    text = _truncate(text, 1024)
    text = text.replace(";Please report this error in bugzilla", "")
    text = text.replace("Missing Dependency: ", "")
    text = text.replace(" (installed)", "")
    return text

def _get_cmdline_for_pid(pid):
    if not pid:
        return "invalid"
    cmdlines = open("/proc/%d/cmdline" % pid).read().split('\0')
    cmdline = " ".join(cmdlines).strip(' ')
    return cmdline

class PackageKitYumBackend(PackageKitBaseBackend, PackagekitPackage):

    # Packages there require a reboot
    rebootpkgs = ("kernel", "kernel-smp", "kernel-xen-hypervisor", "kernel-PAE",
              "kernel-xen0", "kernel-xenU", "kernel-xen", "kernel-xen-guest",
              "glibc", "hal", "dbus", "xen")

    def __init__(self, args, lock=True):
        signal.signal(signal.SIGQUIT, sigquit)
        PackageKitBaseBackend.__init__(self, args)
        try:
            self.yumbase = PackageKitYumBase(self)
        except PkError, e:
            self.error(e.code, e.details)

        # load the config file
        config = ConfigParser.ConfigParser()
        try:
            config.read('/etc/PackageKit/Yum.conf')
        except Exception, e:
            raise PkError(ERROR_REPO_CONFIGURATION_ERROR, "Failed to load Yum.conf: %s" % _to_unicode(e))

        # if this key does not exist, it's not fatal
        try:
            self.system_packages = config.get('Backend', 'SystemPackages').split(';')
        except ConfigParser.NoOptionError, e:
            self.system_packages = []
        except Exception, e:
            raise PkError(ERROR_REPO_CONFIGURATION_ERROR, "Failed to load Yum.conf: %s" % _to_unicode(e))
        try:
            self.infra_packages = config.get('Backend', 'InfrastructurePackages').split(';')
        except ConfigParser.NoOptionError, e:
            self.infra_packages = []
        except Exception, e:
            raise PkError(ERROR_REPO_CONFIGURATION_ERROR, "Failed to load Yum.conf: %s" % _to_unicode(e))

        # get the lock early
        if lock:
            self.doLock()

        self.package_summary_cache = {}
        self.comps = yumComps(self.yumbase)
        if not self.comps.connect():
            self.refresh_cache(True)
            if not self.comps.connect():
                self.error(ERROR_GROUP_LIST_INVALID, 'comps categories could not be loaded')

        # timeout a socket after this much time
        timeout = 15.0
        socket.setdefaulttimeout(timeout)

        # use idle bandwidth by setting congestion control algorithm to TCP Low Priority
        if self.background:
            socket.TCP_CONGESTION = 13
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_CONGESTION, "lp")
            except socket.error, e:
                pass

        # we only check these types
        self.transaction_sig_check_map = [TS_UPDATE, TS_INSTALL, TS_TRUEINSTALL, TS_OBSOLETING]

        # this is global so we can catch sigquit and closedown
        yumbase = self.yumbase
        try:
            self._setup_yum()
        except PkError, e:
            self.error(e.code, e.details)

    def details(self, package_id, package_license, group, desc, url, bytes):
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
        desc = _to_unicode(desc)
        PackageKitBaseBackend.details(self, package_id, package_license, group, desc, url, bytes)

    def package(self, package_id, status, summary):
        '''
        send 'package' signal
        @param info: the enumerated INFO_* string
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param summary: The package Summary
        convert the summary to UTF before sending
        '''
        summary = _to_unicode(summary)

        # maintain a dictionary of the summary text so we can use it when rpm
        # is giving up package names without summaries
        (name, idver, a, repo) = self.get_package_from_id(package_id)
        if len(summary) > 0:
            self.package_summary_cache[name] = summary
        else:
            if self.package_summary_cache.has_key(name):
                summary = self.package_summary_cache[name]

        PackageKitBaseBackend.package(self, package_id, status, summary)

    def category(self, parent_id, cat_id, name, summary, icon):
        '''
        Send 'category' signal
        parent_id : A parent id, e.g. "admin" or "" if there is no parent
        cat_id    : a unique category id, e.g. "admin;network"
        name      : a verbose category name in current locale.
        summery   : a summary of the category in current locale.
        icon      : an icon name to represent the category
        '''
        name = _to_unicode(name)
        summary = _to_unicode(summary)
        PackageKitBaseBackend.category(self, parent_id, cat_id, name, summary, icon)

    def doLock(self):
        ''' Lock Yum'''
        retries = 0
        cmdline = None
        while not self.isLocked():
            try: # Try to lock yum
                self.yumbase.doLock(YUM_PID_FILE)
                PackageKitBaseBackend.doLock(self)
                self.allow_cancel(False)
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except yum.Errors.LockError, e:
                self.allow_cancel(True)
                self.status(STATUS_WAITING_FOR_LOCK)

                # get the command line of the other thing
                if not cmdline:
                    cmdline = _get_cmdline_for_pid(e.pid)

                # if it's us, kill it as it's from another instance where the daemon crashed
                if cmdline.find("yumBackend.py") != -1:
                    self.message(MESSAGE_BACKEND_ERROR, "killing pid %i, as old instance" % e.pid)
                    os.kill(e.pid, signal.SIGQUIT)

                # wait a little time, and try again
                time.sleep(2)
                retries += 1

                # give up, and print process information
                if retries > 100:
                    msg = "The other process has the command line '%s' (PID %i)" % (cmdline, e.pid)
                    self.error(ERROR_CANNOT_GET_LOCK, "Yum is locked by another application. %s" % _to_unicode(msg))

    def unLock(self):
        ''' Unlock Yum'''
        if self.isLocked():
            PackageKitBaseBackend.unLock(self)
            try:
                self.yumbase.closeRpmDB()
                self.yumbase.doUnlock(YUM_PID_FILE)
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

    def _do_meta_package_search(self, filters, values):
        grps = self.comps.get_meta_packages()
        for grpid in grps:
            for value in values:
                if value in grpid:
                    self._show_meta_package(grpid, filters)

    def set_locale(self, code):
        '''
        Implement the set-locale functionality
        Needed to be implemented in a sub class
        '''
        self.lang = code

    def _do_search(self, searchlist, filters, values):
        '''
        Search for yum packages
        @param searchlist: The yum package fields to search in
        @param filters: package types to search (all, installed, available)
        @param values: key to seach for
        '''
        pkgfilter = YumFilter(filters)
        package_list = []

        # get collection objects
        if FILTER_NOT_COLLECTIONS not in filters:
            self._do_meta_package_search(filters, values)

        # return, as we only want collection objects
        if FILTER_COLLECTIONS not in filters:
            installed = []
            available = []
            try:
                res = self.yumbase.searchGenerator(searchlist, values)
                for (pkg, inst) in res:
                    if pkg.repo.id.startswith('installed'):
                        installed.append(pkg)
                    else:
                        available.append(pkg)
            except yum.Errors.RepoError, e:
                raise PkError(ERROR_NO_CACHE, "failed to use search generator: %s" %_to_unicode(e))
            except exceptions.IOError, e:
                raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            else:
                pkgfilter.add_installed(installed)
                pkgfilter.add_available(available)

                # we couldn't do this when generating the list
                package_list = pkgfilter.get_package_list()
                self._show_package_list(package_list)

    def _show_package_list(self, lst):
        for (pkg, status) in lst:
            self._show_package(pkg, status)

    def search_name(self, filters, values):
        '''
        Implement the search-name functionality
        '''
        try:
            self._check_init(lazy_cache=True)
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(None)

        searchlist = ['name']
        self.status(STATUS_QUERY)
        try:
            self.yumbase.doConfigSetup(errorlevel=0, debuglevel=0)# Setup Yum Config
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        try:
            self._do_search(searchlist, filters, values)
        except PkError, e:
            self.error(e.code, e.details, exit=False)

    def search_details(self, filters, values):
        '''
        Implement the search-details functionality
        '''
        try:
            self._check_init(lazy_cache=True)
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        try:
            self.yumbase.doConfigSetup(errorlevel=0, debuglevel=0)# Setup Yum Config
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(None)

        searchlist = ['name', 'summary', 'description', 'group']
        self.status(STATUS_QUERY)
        try:
            self._do_search(searchlist, filters, values)
        except PkError, e:
            self.error(e.code, e.details, exit=False)

    def _get_installed_from_names(self, name_list):
        found = []
        for package in name_list:
            try:
                pkgs = self.yumbase.rpmdb.searchNevra(name=package)
            except exceptions.IOError, e:
                raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            else:
                found.extend(pkgs)
        return found

    def _get_available_from_names(self, name_list):
        pkgs = None
        try:
            pkgs = self.yumbase.pkgSack.searchNames(names=name_list)
        except yum.Errors.RepoError, e:
            raise PkError(ERROR_NO_CACHE, "failed to search names: %s" %_to_unicode(e))
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        return pkgs

    def _handle_repo_group_search_using_yumdb(self, repo_id, filters):
        """
        Handle the special repo groups
        """
        self.percentage(None)
        pkgfilter = YumFilter(filters)
        available = []
        installed = []

        pkgs = []
        # get installed packages that came from this repo
        try:
            pkgs = self.yumbase.rpmdb
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        for pkg in pkgs:
            if pkg.yumdb_info.get('from_repo') == repo_id:
                pkgfilter.add_installed([pkg])

        # find the correct repo
        try:
            repos = self.yumbase.repos.findRepos(repo_id)
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        if len(repos) == 0:
            raise PkError(ERROR_REPO_NOT_FOUND, "cannot find repo %s" % repo_id)

        # the repo might have been disabled if it is no longer contactable
        if not repos[0].isEnabled():
            raise PkError(ERROR_PACKAGE_NOT_FOUND, '%s cannot be found as %s is disabled' % (repo_id, repos[0].id))

        # populate the sack with data
        try:
            self.yumbase.repos.populateSack(repo_id)
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        for pkg in repos[0].sack:
            available.append(pkg)

        # add list to filter
        pkgfilter.add_installed(installed)
        pkgfilter.add_available(available)
        package_list = pkgfilter.get_package_list()
        self._show_package_list(package_list)
        self.percentage(100)

    def _handle_repo_group_search(self, repo_id, filters):
        """
        Handle the special repo groups
        This is much slower than using _handle_repo_group_search_using_yumdb()
        as we have to resolve each package in the repo to see if it's
        installed.
        Of course, on RHEL5, there is no yumdb.
        """
        self.percentage(None)
        pkgfilter = YumFilter(filters)
        available = []
        installed = []

        # find the correct repo
        try:
            repos = self.yumbase.repos.findRepos(repo_id)
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        if len(repos) == 0:
            raise PkError(ERROR_REPO_NOT_FOUND, "cannot find repo %s" % repo_id)
        print "found repos"

        # the repo might have been disabled if it is no longer contactable
        if not repos[0].isEnabled():
            raise PkError(ERROR_PACKAGE_NOT_FOUND, '%s cannot be found as %s is disabled' % (repo_id, repos[0].id))

        # populate the sack with data
        try:
            self.yumbase.repos.populateSack(repo_id)
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

        for pkg in repos[0].sack:
            try:
                instpo = self.yumbase.rpmdb.searchNevra(name=pkg.name, epoch=pkg.epoch, ver=pkg.ver, rel=pkg.rel, arch=pkg.arch)
            except exceptions.IOError, e:
                raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            if self._is_inst(pkg):
                installed.append(instpo[0])
            else:
                available.append(pkg)

        # add list to filter
        pkgfilter.add_installed(installed)
        pkgfilter.add_available(available)
        package_list = pkgfilter.get_package_list()
        self._show_package_list(package_list)
        self.percentage(100)

    def _handle_newest(self, filters):
        """
        Handle the special newest group
        """
        self.percentage(None)
        pkgfilter = YumFilter(filters)
        pkgs = []
        try:
            ygl = self.yumbase.doPackageLists(pkgnarrow='recent')
            pkgs.extend(ygl.recent)
        except yum.Errors.RepoError, e:
            raise PkError(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e))
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        else:
            installed = []
            available = []
            for pkg in pkgs:
                try:
                    instpo = self.yumbase.rpmdb.searchNevra(name=pkg.name, epoch=pkg.epoch, ver=pkg.ver, rel=pkg.rel, arch=pkg.arch)
                except exceptions.IOError, e:
                    raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                if len(instpo) > 0:
                    installed.append(instpo[0])
                else:
                    available.append(pkg)

            # add list to filter
            pkgfilter.add_installed(installed)
            pkgfilter.add_available(available)
            package_list = pkgfilter.get_package_list()
            self._show_package_list(package_list)
            self.percentage(100)

    def _handle_collections(self, filters):
        """
        Handle the special collection group
        """
        # Fixme: Add some real code.
        self.percentage(None)
        collections = self.comps.get_meta_packages()
        if len(collections) == 0:
            raise PkError(ERROR_GROUP_LIST_INVALID, 'No groups could be found. A cache refresh should fix this.')

        pct = 20
        old_pct = -1
        step = (100.0 - pct) / len(collections)
        for col in collections:
            self._show_meta_package(col, filters)
            pct += step
            if int(pct) != int(old_pct):
                self.percentage(pct)
                old_pct = pct
        self.percentage(100)

    def _show_meta_package(self, grpid, filters):
        show_avail = FILTER_INSTALLED not in filters
        show_inst = FILTER_NOT_INSTALLED not in filters
        package_id = "%s;;;meta" % grpid
        try:
            grp = self.yumbase.comps.return_group(grpid)
        except yum.Errors.RepoError, e:
            raise PkError(ERROR_NO_CACHE, "failed to get groups from comps: %s" % _to_unicode(e))
        except yum.Errors.GroupsError, e:
            raise PkError(ERROR_GROUP_NOT_FOUND, "failed to find group '%s': %s" % (package_id, _to_unicode(e)))
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        else:
            if grp:
                name = grp.nameByLang(self.lang)
                if grp.installed:
                    if show_inst:
                        self.package(package_id, INFO_COLLECTION_INSTALLED, name)
                else:
                    if show_avail:
                        self.package(package_id, INFO_COLLECTION_AVAILABLE, name)

    def search_group(self, filters, values):
        '''
        Implement the search-group functionality
        '''
        try:
            self._check_init(lazy_cache=True)
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.allow_cancel(True)
        try:
            self.yumbase.doConfigSetup(errorlevel=0, debuglevel=0)# Setup Yum Config
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        self.yumbase.conf.cache = 0 # TODO: can we just look in the cache?
        self.status(STATUS_QUERY)
        package_list = [] #we can't do emitting as found if we are post-processing
        pkgfilter = YumFilter(filters)

        # handle collections
        if GROUP_COLLECTIONS in values:
            try:
                self._handle_collections(filters)
            except PkError, e:
                self.error(e.code, e.details, exit=False)
            return

        # handle newest packages
        if GROUP_NEWEST in values:
            try:
                self._handle_newest(filters)
            except PkError, e:
                self.error(e.code, e.details, exit=False)
            return

        # handle repo groups
        if 'repo:' in values[0]:
            try:
                self._handle_repo_group_search_using_yumdb(values[0].replace('repo:',''), filters)
            except PkError, e:
                self.error(e.code, e.details, exit=False)
            return

        # for each search term
        for value in values:
            # handle dynamic groups (yum comps group)
            if value[0] == '@':
                cat_id = value[1:]
                 # get the packagelist for this group
                all_packages = self.comps.get_meta_package_list(cat_id)
            else: # this is an group_enum
                # get the packagelist for this group enum
                all_packages = self.comps.get_package_list(value)

            # group don't exits, just bail out
            if not all_packages:
                continue

            if FILTER_NOT_INSTALLED not in filters:
                try:
                    pkgfilter.add_installed(self._get_installed_from_names(all_packages))
                except PkError, e:
                    self.error(e.code, e.details, exit=False)
                    return

            if FILTER_INSTALLED not in filters:
                try:
                    pkgfilter.add_available(self._get_available_from_names(all_packages))
                except PkError, e:
                    self.error(e.code, e.details, exit=False)
                    return

        # we couldn't do this when generating the list
        package_list = pkgfilter.get_package_list()

        self.percentage(90)
        self._show_package_list(package_list)

        self.percentage(100)

    def get_packages(self, filters):
        '''
        Search for yum packages
        @param searchlist: The yum package fields to search in
        @param filters: package types to search (all, installed, available)
        '''
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        try:
            self.yumbase.doConfigSetup(errorlevel=0, debuglevel=0)# Setup Yum Config
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        self.yumbase.conf.cache = 0 # TODO: can we just look in the cache?

        package_list = [] #we can't do emitting as found if we are post-processing
        pkgfilter = YumFilter(filters)

        # Now show installed packages.
        try:
            pkgs = self.yumbase.rpmdb
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        pkgfilter.add_installed(pkgs)

        # Now show available packages.
        if FILTER_INSTALLED not in filters:
            try:
                pkgs = self.yumbase.pkgSack
            except yum.Errors.RepoError, e:
                self.error(ERROR_NO_CACHE, "failed to get package sack: %s" %_to_unicode(e), exit=False)
                return
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            else:
                pkgfilter.add_available(pkgs)

        # we couldn't do this when generating the list
        package_list = pkgfilter.get_package_list()
        self._show_package_list(package_list)

    def search_file(self, filters, values):
        '''
        Implement the search-file functionality
        '''
        try:
            self._check_init(lazy_cache=True)
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)

        #self.yumbase.conf.cache = 0 # TODO: can we just look in the cache?
        pkgfilter = YumFilter(filters)

        # Check installed for file
        if not FILTER_NOT_INSTALLED in filters:
            for value in values:
                try:
                    pkgs = self.yumbase.rpmdb.searchFiles(value)
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                pkgfilter.add_installed(pkgs)

        # Check available for file
        if not FILTER_INSTALLED in filters:
            for value in values:
                try:
                    # we don't need the filelists as we're not globbing
                    pkgs = self.yumbase.pkgSack.searchFiles(value)
                except yum.Errors.RepoError, e:
                    self.error(ERROR_NO_CACHE, "failed to search sack: %s" %_to_unicode(e), exit=False)
                    return
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                else:
                    pkgfilter.add_available(pkgs)

        # we couldn't do this when generating the list
        package_list = pkgfilter.get_package_list()
        self._show_package_list(package_list)

    def _get_provides_query(self, provides_type, value):
        # gets a list of provides

        # old standard
        if value.startswith("gstreamer0.10("):
            return [ value ]

        # new standard
        if provides_type == PROVIDES_CODEC:
            return [ "gstreamer0.10(%s)" % value ]
        if provides_type == PROVIDES_FONT:
            return [ "font(%s)" % value ]
        if provides_type == PROVIDES_MIMETYPE:
            return [ "mimehandler(%s)" % value ]
        if provides_type == PROVIDES_POSTSCRIPT_DRIVER:
            return [ "postscriptdriver(%s)" % value ]
        if provides_type == PROVIDES_PLASMA_SERVICE:
            # We need to allow the Plasma version to be specified.
            if value.startswith("plasma"):
                return [ value ]
            # For compatibility, we default to plasma4.
            return [ "plasma4(%s)" % value ]
        if provides_type == PROVIDES_ANY:
            provides = []
            provides.append(self._get_provides_query(PROVIDES_CODEC, value)[0])
            provides.append(self._get_provides_query(PROVIDES_FONT, value)[0])
            provides.append(self._get_provides_query(PROVIDES_MIMETYPE, value)[0])
            provides.append(self._get_provides_query(PROVIDES_POSTSCRIPT_DRIVER, value)[0])
            provides.append(self._get_provides_query(PROVIDES_PLASMA_SERVICE, value)[0])
            provides.append(value)
            return provides

        # not supported
        raise PkError(ERROR_NOT_SUPPORTED, "this backend does not support '%s' provides" % provides_type)

    def what_provides(self, filters, provides_type, values):
        '''
        Implement the what-provides functionality
        '''
        try:
            self._check_init(lazy_cache=True)
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)
        values_provides = []

        pkgfilter = YumFilter(filters)

        try:
            for value in values:
                provides = self._get_provides_query(provides_type, value)
                for provide in provides:
                    values_provides.append(provide)
        except PkError, e:
            self.error(e.code, e.details, exit=False)
        else:
            # there may be multiple provide strings
            for provide in values_provides:
                # Check installed packages for provide
                try:
                    pkgs = self.yumbase.rpmdb.searchProvides(provide)
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                else:
                    pkgfilter.add_installed(pkgs)

                    if not FILTER_INSTALLED in filters:
                        # Check available packages for provide
                        try:
                            pkgs = self.yumbase.pkgSack.searchProvides(provide)
                        except yum.Errors.RepoError, e:
                            self.error(ERROR_NO_CACHE, "failed to get provides for sack: %s" %_to_unicode(e), exit=False)
                            return
                        except exceptions.IOError, e:
                            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                        except Exception, e:
                            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                        else:
                            pkgfilter.add_available(pkgs)

                    # we couldn't do this when generating the list
                    package_list = pkgfilter.get_package_list()
                    self._show_package_list(package_list)

    def get_categories(self):
        '''
        Implement the get-categories functionality
        '''
        try:
            self._check_init(lazy_cache=True)
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.percentage(None)
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        cats = []
        try:
            cats = self.yumbase.comps.categories
        except yum.Errors.RepoError, e:
            self.error(ERROR_NO_CACHE, "failed to get comps list: %s" %_to_unicode(e), exit=False)
        except yum.Errors.GroupsError, e:
            self.error(ERROR_GROUP_LIST_INVALID, "Failed to get groups list: %s" %_to_unicode(e), exit=False)
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        else:
            if len(cats) == 0:
                self.error(ERROR_GROUP_LIST_INVALID, "no comps categories", exit=False)
                return
            for cat in cats:
                cat_id = cat.categoryid
                # yum >= 3.2.10
                # name = cat.nameByLang(self.lang)
                # summary = cat.descriptionByLang(self.lang)
                name = _to_unicode(cat.name)
                summary = _to_unicode(cat.description)
                fn = "/usr/share/pixmaps/comps/%s.png" % cat_id
                if os.access(fn, os.R_OK):
                    icon = cat_id
                else:
                    icon = "image-missing"
                self.category("", cat_id, name, summary, icon)
                self._get_groups(cat_id)

        # also add the repo category objects
        self.category("", 'repo:', 'Software Sources', 'Packages from specific software sources', 'base-system')
        try:
            repos = self.yumbase.repos.repos.values()
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            return
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            return
        for repo in repos:
            if repo.isEnabled():
                self.category("repo:", "repo:" + repo.id, repo.name, 'Packages from ' + repo.name, 'base-system')

    def _get_groups(self, cat_id):
        '''
        Implement the get-collections functionality
        '''
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        if cat_id:
            cats = [cat_id]
        else:
            cats =  [cat.categoryid for cat in self.yumbase.comps.categories]
        for cat in cats:
            grps = []
            for grp_id in self.comps.get_groups(cat):
                try:
                    grp = self.yumbase.comps.return_group(grp_id)
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                if grp:
                    grps.append(grp)
            for grp in sorted(grps):
                grp_id = grp.groupid
                cat_id_name = "@%s" % (grp_id)
                name = grp.nameByLang(self.lang)
                summary = grp.descriptionByLang(self.lang)
                icon = "image-missing"
                fn = "/usr/share/pixmaps/comps/%s.png" % grp_id
                if os.access(fn, os.R_OK):
                    icon = grp_id
                else:
                    fn = "/usr/share/pixmaps/comps/%s.png" % cat_id
                    if os.access(fn, os.R_OK):
                        icon = cat_id
                self.category(cat, cat_id_name, name, summary, icon)

    def download_packages(self, directory, package_ids):
        '''
        Implement the download-packages functionality
        '''
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.status(STATUS_DOWNLOAD)
        percentage = 0
        bump = 100 / len(package_ids)

        # download each package
        for package_id in package_ids:
            self.percentage(percentage)
            try:
                pkg, inst = self._findPackage(package_id)
            except PkError, e:
                self.error(e.code, e.details, exit=True)
                return
            # if we couldn't map package_id -> pkg
            if not pkg:
                self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, "Could not find the package %s" % package_id)
                continue

            n, a, e, v, r = pkg.pkgtup
            try:
                packs = self.yumbase.pkgSack.searchNevra(n, e, v, r, a)
            except yum.Errors.RepoError, e:
                self.error(ERROR_NO_CACHE, "failed to search package sack: %s" %_to_unicode(e), exit=False)
                return
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

            # if we couldn't map package_id -> pkg
            if len(packs) == 0:
                self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, "Could not find a match for package %s" % package_id)
                continue

            # choose the first entry, as the same NEVRA package in multiple repos is fine
            pkg_download = packs[0]
            self._show_package(pkg_download, INFO_DOWNLOADING)
            try:
                repo = self.yumbase.repos.getRepo(pkg_download.repoid)
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            remote = pkg_download.returnSimple('relativepath')
            if directory:
                local = os.path.basename(remote)
                if not os.path.exists(directory):
                    self.error(ERROR_PACKAGE_DOWNLOAD_FAILED, "No destination directory exists", exit=False)
                    return
                local = os.path.join(directory, local)
                if (os.path.exists(local) and os.path.getsize(local) == int(pkg_download.returnSimple('packagesize'))):
                    self.error(ERROR_PACKAGE_DOWNLOAD_FAILED, "Package already exists as %s" % local, exit=False)
                    return
            # Disable cache otherwise things won't download
            repo.cache = 0

            #  set the localpath we want
            if directory:
                pkg_download.localpath = local
            try:
                path = repo.getPackage(pkg_download)

                # emit the file we downloaded
                package_id_tmp = self._pkg_to_id(pkg_download)
                self.files(package_id_tmp, path)

            except IOError, e:
                self.error(ERROR_PACKAGE_DOWNLOAD_FAILED, "Cannot write to file", exit=False)
                return
            percentage += bump

        # in case we don't sum to 100
        self.percentage(100)

    def _is_meta_package(self, package_id):
        grp = None
        if len(package_id.split(';')) > 1:
            # Split up the id
            (name, idver, a, repo) = self.get_package_from_id(package_id)
            isGroup = False
            if repo == 'meta':
                if name[0] == '@':
                    name = name[1:]
                try:
                    grp = self.yumbase.comps.return_group(name)
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                isGroup = True
            elif name[0] == '@':
                try:
                    grp = self.yumbase.comps.return_group(name[1:])
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                isGroup = True
            if isGroup and not grp:
                self.error(ERROR_GROUP_NOT_FOUND, "group %s does not exist (searched for %s)" % (name, _format_package_id (package_id)))
                return
        return grp

    def _findPackage(self, package_id):
        '''
        find a package based on a package id (name;version;arch;repoid)
        '''
        # Bailout if meta packages, just to be sure
        if self._is_meta_package(package_id):
            return None, False

        # is this an real id?
        if len(package_id.split(';')) <= 1:
            raise PkError(ERROR_PACKAGE_ID_INVALID, "package_id '%s' cannot be parsed" % _format_package_id(package_id))

        # Split up the id
        (n, idver, a, repo) = self.get_package_from_id(package_id)
        # get e, v, r from package id version
        e, v, r = _getEVR(idver)

        if repo.startswith('installed'):
            # search the rpmdb for the nevra
            try:
                pkgs = self.yumbase.rpmdb.searchNevra(name=n, epoch=e, ver=v, rel=r, arch=a)
            except exceptions.IOError, e:
                raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            # if the package is found, then return it (do not have to match the repo_id)
            if len(pkgs) != 0:
                return pkgs[0], True
            return None, False

        # find the correct repo, and don't use yb.pkgSack.searchNevra as it
        # searches all repos and takes 66ms
        try:
            repos = self.yumbase.repos.findRepos(repo)
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        if len(repos) == 0:
            raise PkError(ERROR_REPO_NOT_FOUND, "cannot find repo %s" % repo)

        # the repo might have been disabled if it is no longer contactable
        if not repos[0].isEnabled():
            raise PkError(ERROR_PACKAGE_NOT_FOUND, '%s cannot be found as %s is disabled' % (_format_package_id(package_id), repos[0].id))

        # populate the sack with data
        try:
            self.yumbase.repos.populateSack(repo)
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

        # search the pkgSack for the nevra
        try:
            pkgs = repos[0].sack.searchNevra(name=n, epoch=e, ver=v, rel=r, arch=a)
        except yum.Errors.RepoError, e:
            raise PkError(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e))
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

        # nothing found
        if len(pkgs) == 0:
            return None, False

        # multiple entries
        if len(pkgs) > 1:
            self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, "more than one package match for %s" % _format_package_id(package_id))

        # return first entry
        return pkgs[0], False

    def get_requires(self, filters, package_ids, recursive):
        '''
        Print a list of requires for a given package
        '''
        try:
            self._check_init(lazy_cache=True)
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        percentage = 0
        bump = 100 / len(package_ids)
        deps_list = []
        resolve_list = []

        for package_id in package_ids:
            self.percentage(percentage)
            grp = self._is_meta_package(package_id)
            if grp:
                if not grp.installed:
                    self.error(ERROR_PACKAGE_NOT_INSTALLED, "The Group %s is not installed" % grp.groupid)
                else:
                    try:
                        txmbrs = self.yumbase.groupRemove(grp.groupid)
                    except exceptions.IOError, e:
                        self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                    except Exception, e:
                        self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                    for txmbr in self.yumbase.tsInfo:
                        deps_list.append(txmbr.po)
            else:
                try:
                    pkg, inst = self._findPackage(package_id)
                except PkError, e:
                    if e.code == ERROR_PACKAGE_NOT_FOUND:
                        self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, e.details)
                        continue
                    self.error(e.code, e.details, exit=True)
                    return
                # This simulates the removal of the package
                if inst and pkg:
                    resolve_list.append(pkg)
                    try:
                        txmbrs = self.yumbase.remove(po=pkg)
                    except exceptions.IOError, e:
                        self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                    except Exception, e:
                        self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            percentage += bump

        # do the depsolve to pull in deps
        if len(self.yumbase.tsInfo) > 0  and recursive:
            try:
                rc, msgs =  self.yumbase.buildTransaction()
            except yum.Errors.RepoError, e:
                self.error(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e))
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            if rc != 2:
                self.error(ERROR_DEP_RESOLUTION_FAILED, _format_msgs(msgs))
            else:
                for txmbr in self.yumbase.tsInfo:
                    if txmbr.po not in deps_list:
                        deps_list.append(txmbr.po)

        # remove any of the original names
        for pkg in resolve_list:
            if pkg in deps_list:
                deps_list.remove(pkg)

        # each unique name, emit
        for pkg in deps_list:
            package_id = self._pkg_to_id(pkg)
            self.package(package_id, INFO_INSTALLED, pkg.summary)
        self.percentage(100)

    def _is_inst(self, pkg):
        # search only for requested arch
        try:
            ret = self.yumbase.rpmdb.installed(po=pkg)
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        return ret

    def _is_inst_arch(self, pkg):
        # search for a requested arch first
        ret = self._is_inst(pkg)
        if ret:
            return True

        # then fallback to i686 if i386
        if pkg.arch == 'i386':
            pkg.arch = 'i686'
            ret = self._is_inst(pkg)
            pkg.arch = 'i386'
        return ret

    def _installable(self, pkg, ematch=False):

        """check if the package is reasonably installable, true/false"""

        try:
            exactarchlist = self.yumbase.conf.exactarchlist
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        # we look through each returned possibility and rule out the
        # ones that we obviously can't use

        if self._is_inst_arch(pkg):
            return False

        # everything installed that matches the name
        try:
            installedByKey = self.yumbase.rpmdb.searchNevra(name=pkg.name, arch=pkg.arch)
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
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

                    if ematch:
                        try:
                            ret = self.yumbase.allowedMultipleInstalls(pkg)
                        except exceptions.IOError, e:
                            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                        except Exception, e:
                            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                        if ret:
                            return True

        else: # we've not got any installed that match n or n+a
            return True

        return False

    def _get_best_pkg_from_list(self, pkglist):
        '''
        Gets best dep package from a list
        '''
        best = None

        # first try and find the highest EVR package that is already installed
        for pkgi in pkglist:
            n, a, e, v, r = pkgi.pkgtup
            try:
                pkgs = self.yumbase.rpmdb.searchNevra(name=n, epoch=e, ver=v, arch=a)
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            for pkg in pkgs:
                if best:
                    if pkg.EVR > best.EVR:
                        best = pkg
                else:
                    best = pkg

        # then give up and see if there's one available
        if not best:
            for pkg in pkglist:
                if best:
                    if pkg.EVR > best.EVR:
                        best = pkg
                else:
                    best = pkg
        return best

    def _get_best_depends(self, pkgs, recursive):
        ''' Gets the best deps for a package
        @param pkgs: a list of package objects
        @param recursive: if we recurse
        @return: a list for yum package object providing the dependencies
        '''
        deps_list = []

        # get the dep list
        try:
            results = self.yumbase.findDeps(pkgs)
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        require_list = []
        recursive_list = []

        # get the list of deps for each package
        for pkg in results.keys():
            for req in results[pkg].keys():
                reqlist = results[pkg][req]
                if not reqlist: #  Unsatisfied dependency
                    self.error(ERROR_DEP_RESOLUTION_FAILED, "the (%s) requirement could not be resolved" % prco_tuple_to_string(req), exit=False)
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
            pkgsdeps = self._get_best_depends(recursive_list, True)
            for pkg in pkgsdeps:
                if pkg not in pkgs:
                    deps_list.append(pkg)

        return deps_list

    def _get_group_packages(self, grp):
        '''
        Get the packages there will be installed when a comps group
        is installed
        '''
        if not grp.installed:
            try:
                txmbrs = self.yumbase.selectGroup(grp.groupid)
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        else:
            try:
                txmbrs = self.yumbase.groupRemove(grp.groupid)
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        pkgs = []
        for t in txmbrs:
            pkgs.append(t.po)
        if not grp.installed:
            try:
                self.yumbase.deselectGroup(grp.groupid)
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        else:
            try:
                self.yumbase.groupUnremove(grp.groupid)
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        return pkgs

    def _get_depends_not_installed(self, filters, package_ids, recursive):
        '''
        Gets the deps that are not installed, optimisation of get_depends
        using a yum transaction
        Returns a list of pkgs.
        '''
        percentage = 0
        bump = 100 / len(package_ids)
        deps_list = []
        resolve_list = []

        for package_id in package_ids:
            self.percentage(percentage)
            grp = self._is_meta_package(package_id)
            if grp:
                if grp.installed:
                    self.error(ERROR_PACKAGE_ALREADY_INSTALLED, "The Group %s is already installed" % grp.groupid)
                else:
                    try:
                        txmbrs = self.yumbase.selectGroup(grp.groupid)
                    except exceptions.IOError, e:
                        self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                    except Exception, e:
                        self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                    for txmbr in self.yumbase.tsInfo:
                        deps_list.append(txmbr.po)
                    # unselect what we previously selected
                    try:
                        self.yumbase.deselectGroup(grp.groupid)
                    except exceptions.IOError, e:
                        self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                    except Exception, e:
                        self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            else:
                try:
                    pkg, inst = self._findPackage(package_id)
                except PkError, e:
                    self.error(e.code, e.details, exit=True)
                    return
                # This simulates the addition of the package
                if not inst and pkg:
                    resolve_list.append(pkg)
                    try:
                        txmbrs = self.yumbase.install(po=pkg)
                    except yum.Errors.RepoError, e:
                        self.error(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e))
                    except exceptions.IOError, e:
                        self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                    except Exception, e:
                        self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            percentage += bump

        if len(self.yumbase.tsInfo) > 0 and recursive:
            try:
                rc, msgs =  self.yumbase.buildTransaction()
            except yum.Errors.RepoError, e:
                self.error(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e))
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            if rc != 2:
                self.error(ERROR_DEP_RESOLUTION_FAILED, _format_msgs(msgs))
            else:
                for txmbr in self.yumbase.tsInfo:
                    if txmbr.po not in deps_list:
                        deps_list.append(txmbr.po)

        # make unique list
        deps_list = unique(deps_list)

        # remove any of the packages we passed in
        for package_id in package_ids:
            try:
                pkg, inst = self._findPackage(package_id)
            except PkError, e:
                self.error(e.code, e.details, exit=True)
                return
            if pkg in deps_list:
                deps_list.remove(pkg)

        # remove any that are already installed
        for pkg in deps_list:
            if self._is_inst(pkg):
                deps_list.remove(pkg)

        return deps_list

    def get_depends(self, filters, package_ids, recursive):
        '''
        Print a list of depends for a given package
        '''
        try:
            self._check_init(lazy_cache=True)
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        pkgfilter = YumFilter(filters)

        # before we do an install we do ~installed + recursive true,
        # which we can emulate quicker by doing a transaction, but not
        # executing it
        if FILTER_NOT_INSTALLED in filters and recursive:
            pkgs = self._get_depends_not_installed (filters, package_ids, recursive)
            pkgfilter.add_available(pkgs)
            package_list = pkgfilter.get_package_list()
            self._show_package_list(package_list)
            self.percentage(100)
            return

        percentage = 0
        bump = 100 / len(package_ids)
        deps_list = []
        resolve_list = []
        grp_pkgs = []

        # resolve each package_id to a pkg object
        for package_id in package_ids:
            self.percentage(percentage)
            grp = self._is_meta_package(package_id)
            if grp:
                pkgs = self._get_group_packages(grp)
                grp_pkgs.extend(pkgs)
            else:
                try:
                    pkg, inst = self._findPackage(package_id)
                except PkError, e:
                    if e.code == ERROR_PACKAGE_NOT_FOUND:
                        self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, e.details)
                        continue
                    self.error(e.code, e.details, exit=True)
                    return
                if pkg:
                    resolve_list.append(pkg)
                else:
                    self.error(ERROR_PACKAGE_NOT_FOUND, 'Package %s was not found' % package_id)
                    break
            percentage += bump

        if grp_pkgs:
            resolve_list.extend(grp_pkgs)
        # get the best deps -- doing recursive is VERY slow
        deps_list = self._get_best_depends(resolve_list, recursive)

        # make unique list
        deps_list = unique(deps_list)

        # If packages comes from a group, then we show them along with deps.
        if grp_pkgs:
            deps_list.extend(grp_pkgs)

        # add to correct lists
        for pkg in deps_list:
            if self._is_inst(pkg):
                pkgfilter.add_installed([pkg])
            else:
                pkgfilter.add_available([pkg])

        # we couldn't do this when generating the list
        package_list = pkgfilter.get_package_list()
        self._show_package_list(package_list)
        self.percentage(100)

    def _is_package_repo_signed(self, pkg):
        '''
        Finds out if the repo that contains the package is signed
        '''
        signed = False
        try:
            repo = self.yumbase.repos.getRepo(pkg.repoid)
            signed = repo.gpgcheck
        except yum.Errors.RepoError, e:
            raise PkError(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e))
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        return signed

    def _set_only_trusted(self, only_trusted):
        # if only_trusted is true, it means that we will only install/update
        # signed files and fail on unsigned ones

        if hasattr(self.yumbase, "_override_sigchecks"):
            # _override_sigchecks logic is reversed
            override_sigchecks = not only_trusted

            self.yumbase._override_sigchecks = override_sigchecks

            for repo in self.yumbase.repos.listEnabled():
                repo._override_sigchecks = override_sigchecks

        for attrname in ("gpgcheck", "repo_gpgcheck", "localpkg_gpgcheck"):
            if hasattr(self.yumbase.conf, attrname):
                setattr(self.yumbase.conf, attrname, only_trusted)

            for repo in self.yumbase.repos.listEnabled():
                if hasattr(repo, attrname):
                    setattr(repo, attrname, only_trusted)


    def update_system(self, only_trusted):
        '''
        Implement the update-system functionality
        '''
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(0)
        self.status(STATUS_RUNNING)

        self._set_only_trusted(only_trusted)

        self.yumbase.conf.throttle = "60%" # Set bandwidth throttle to 60%
                                           # to avoid taking all the system's bandwidth.
        try:
            txmbr = self.yumbase.update() # Add all updates to Transaction
        except yum.Errors.RepoError, e:
            self.error(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e), exit=False)
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        else:
            if txmbr:
                # check all the packages in the transaction if only-trusted
                for t in txmbr:
                    # ignore transactions that do not have to be checked, e.g. obsoleted
                    if t.output_state not in self.transaction_sig_check_map:
                        continue
                    pkg = t.po
                    try:
                        signed = self._is_package_repo_signed(pkg)
                    except PkError, e:
                        self.error(e.code, e.details, exit=False)
                        return
                    if signed:
                        continue
                    if only_trusted:
                        self.error(ERROR_CANNOT_UPDATE_REPO_UNSIGNED, "The package %s will not be updated from unsigned repo %s" % (pkg.name, pkg.repoid), exit=False)
                        return
                    self._show_package(pkg, INFO_UNTRUSTED)
                try:
                    self._runYumTransaction(allow_skip_broken=True, only_simulate=False)
                except PkError, e:
                    self.error(e.code, e.details, exit=False)
            else:
                self.error(ERROR_NO_PACKAGES_TO_UPDATE, "Nothing to do", exit=False)
                return

    def refresh_cache(self, force):
        '''
        Implement the refresh_cache functionality
        '''
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.allow_cancel(True)
        self.percentage(0)
        self.status(STATUS_REFRESH_CACHE)

        # we are working offline
        if not self.has_network:
            self.error(ERROR_NO_NETWORK, "cannot refresh cache when offline", exit=False)
            return

        pct = 0
        try:
            if len(self.yumbase.repos.listEnabled()) == 0:
                self.percentage(100)
                return

            #work out the slice for each one
            bump = (95/len(self.yumbase.repos.listEnabled()))/2

            for repo in self.yumbase.repos.listEnabled():

                # emit details for UI
                self.repo_detail(repo.id, repo.name, True)

                # is physical media
                if repo.mediaid:
                    continue
                repo.metadata_expire = 0
                self.yumbase.repos.populateSack(which=[repo.id], mdtype='metadata', cacheonly=1)
                pct += bump
                self.percentage(pct)
                self.yumbase.repos.populateSack(which=[repo.id], mdtype='filelists', cacheonly=1)
                pct += bump
                self.percentage(pct)

            self.percentage(95)
            # Setup categories/groups
            try:
                self.yumbase.doGroupSetup()
            except yum.Errors.GroupsError, e:
                pass
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            #we might have a rounding error
            self.percentage(100)

        except yum.Errors.RepoError, e:
            message = _format_msgs(e.value)
            if message.find ("No more mirrors to try") != -1:
                self.error(ERROR_NO_MORE_MIRRORS_TO_TRY, message, exit=False)
            else:
                self.error(ERROR_REPO_CONFIGURATION_ERROR, message, exit=False)
        except yum.Errors.YumBaseError, e:
            self.error(ERROR_UNKNOWN, "cannot refresh cache: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        else:
            # update the comps groups too
            self.comps.refresh()

    def resolve(self, filters, packages):
        '''
        Implement the resolve functionality
        '''
        try:
            self._check_init(lazy_cache=True)
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.allow_cancel(True)
        self.percentage(None)
        try:
            self.yumbase.doConfigSetup(errorlevel=0, debuglevel=0)# Setup Yum Config
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        self.yumbase.conf.cache = 0 # TODO: can we just look in the cache?
        self.status(STATUS_QUERY)

        pkgfilter = YumFilter(filters)
        package_list = []

        # OR search
        for package in packages:
            # Get installed packages
            if FILTER_NOT_INSTALLED not in filters:
                try:
                    pkgs = self.yumbase.rpmdb.searchNevra(name=package)
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                else:
                    pkgfilter.add_installed(pkgs)

            # Get available packages
            if FILTER_INSTALLED not in filters:
                try:
                    pkgs = self.yumbase.pkgSack.returnNewestByName(name=package)
                except yum.Errors.PackageSackError, e:
                    # no package of this name found, which is okay
                    pass
                except yum.Errors.RepoError, e:
                    self.error(ERROR_NO_CACHE, "failed to return newest by package sack: %s" %_to_unicode(e), exit=False)
                    return
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                else:
                    pkgfilter.add_available(pkgs)

        # is this a metapackage (a group)
        for package_id in packages:
            if package_id[0] == '@':
                grps = self.comps.get_meta_packages()
                for grpid in grps:
                    if grpid == package_id[1:]:

                        # create virtual package
                        pkg = yum.packages.PackageObject()
                        pkg.name = package_id[1:]
                        pkg.version = ''
                        pkg.release = ''
                        pkg.arch = ''
                        pkg.epoch = '0'
                        pkg.repo = 'meta'

                        # get the name and the installed status
                        try:
                            grp = self.yumbase.comps.return_group(grpid)
                        except yum.Errors.RepoError, e:
                            raise PkError(ERROR_NO_CACHE, "failed to get groups from comps: %s" % _to_unicode(e))
                        except yum.Errors.GroupsError, e:
                            raise PkError(ERROR_GROUP_NOT_FOUND, "failed to find group '%s': %s" % (package_id, _to_unicode(e)))
                        except exceptions.IOError, e:
                            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                        except Exception, e:
                            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                        pkg.summary = grp.name
                        if grp.installed:
                            pkgfilter.add_installed([pkg])
                        else:
                            pkgfilter.add_available([pkg])

        # we couldn't do this when generating the list
        package_list = pkgfilter.get_package_list()
        self._show_package_list(package_list)

    def install_packages(self, only_trusted, inst_files):
        self._install_packages(only_trusted, inst_files)

    def simulate_install_packages(self, inst_files):
        self._install_packages(False, inst_files, True)

    def _install_packages(self, only_trusted, package_ids, simulate=False):
        '''
        Implement the install-packages functionality
        '''
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(False)
        self.percentage(0)
        self.status(STATUS_RUNNING)
        txmbrs = []

        self._set_only_trusted(only_trusted or simulate)

        for package_id in package_ids:
            grp = self._is_meta_package(package_id)
            if grp:
                if grp.installed:
                    self.error(ERROR_PACKAGE_ALREADY_INSTALLED, "This Group %s is already installed" % grp.groupid, exit=False)
                    return
                try:
                    # I'm not sure why we have to deselectGroup() before we selectGroup(), but if we don't
                    # then selectGroup returns no packages. I've already made sure that any selectGroup
                    # invokations do deselectGroup, so I'm not sure what's going on...
                    self.yumbase.deselectGroup(grp.groupid)
                    txmbr = self.yumbase.selectGroup(grp.groupid)
                    if not txmbr:
                        self.error(ERROR_GROUP_NOT_FOUND, "No packages were found in the %s group for %s." % (grp.groupid, _format_package_id(package_id)))
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                txmbrs.extend(txmbr)
            else:
                try:
                    pkg, inst = self._findPackage(package_id)
                except PkError, e:
                    self.error(e.code, e.details, exit=True)
                    return
                if pkg and not inst:
                    txmbr = self.yumbase.install(po=pkg)
                    txmbrs.extend(txmbr)
                if inst:
                    self.error(ERROR_PACKAGE_ALREADY_INSTALLED, "The package %s is already installed" % pkg.name, exit=False)
                    return
        if txmbrs:
            for t in txmbrs:
                pkg = t.po
                # ignore transactions that do not have to be checked, e.g. obsoleted
                if t.output_state not in self.transaction_sig_check_map:
                    continue
                try:
                    signed = self._is_package_repo_signed(pkg)
                except PkError, e:
                    self.error(e.code, e.details, exit=False)
                    return
                if signed:
                    continue
                if only_trusted:
                    self.error(ERROR_CANNOT_INSTALL_REPO_UNSIGNED, "The package %s will not be installed from unsigned repo %s" % (pkg.name, pkg.repoid), exit=False)
                    return
                self._show_package(pkg, INFO_UNTRUSTED)
            try:
                self._runYumTransaction(only_simulate=simulate)
            except PkError, e:
                self.error(e.code, e.details, exit=False)
        else:
            self.error(ERROR_ALL_PACKAGES_ALREADY_INSTALLED, "The packages are already all installed", exit=False)

    def _checkForNewer(self, po):
        pkgs = None
        try:
            pkgs = self.yumbase.pkgSack.returnNewestByName(name=po.name)
        except yum.Errors.PackageSackError:
            pass
        except yum.Errors.RepoError, e:
            pass
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        if pkgs:
            newest = pkgs[0]
            if newest.EVR > po.EVR:
                self.message(MESSAGE_NEWER_PACKAGE_EXISTS, "A newer version of %s is available online." % po.name)

    def install_files(self, only_trusted, inst_files):
        self._install_files(only_trusted, inst_files)

    def simulate_install_files(self, inst_files):
        self._install_files(False, inst_files, True)

    def _install_files(self, only_trusted, inst_files, simulate=False):
        '''
        Implement the install-files functionality
        Install the package containing the inst_file file
        Needed to be implemented in a sub class
        '''
        for inst_file in inst_files:
            if inst_file.endswith('.src.rpm'):
                self.error(ERROR_CANNOT_INSTALL_SOURCE_PACKAGE, 'Backend will not install a src rpm file', exit=False)
                return

        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(False)
        self.percentage(0)
        self.status(STATUS_RUNNING)

        # check we have at least one file
        if len(inst_files) == 0:
            self.error(ERROR_FILE_NOT_FOUND, 'no files specified to install', exit=False)
            return

        # check that the files still exist
        for inst_file in inst_files:
            if not os.path.exists(inst_file):
                self.error(ERROR_FILE_NOT_FOUND, '%s could not be found' % inst_file, exit=False)
                return

        # process these first
        tempdir = tempfile.mkdtemp()
        inst_packs = []

        for inst_file in inst_files:
            if inst_file.endswith('.rpm'):
                continue
            elif inst_file.endswith('.servicepack'):
                inst_packs.append(inst_file)
            else:
                self.error(ERROR_INVALID_PACKAGE_FILE, 'Only rpm files and packs are supported', exit=False)
                return

        # decompress and add the contents of any .servicepack files
        for inst_pack in inst_packs:
            inst_files.remove(inst_pack)
            pack = tarfile.TarFile(name = inst_pack, mode = "r")
            members = pack.getnames()
            for mem in members:
                pack.extract(mem, path = tempdir)
            files = os.listdir(tempdir)

            # find the metadata file
            packtype = 'unknown'
            for fn in files:
                if fn == "metadata.conf":
                    config = ConfigParser.ConfigParser()
                    config.read(os.path.join(tempdir, fn))
                    if config.has_option('PackageKit Service Pack', 'type'):
                        packtype = config.get('PackageKit Service Pack', 'type')
                    break

            # we only support update and install
            if packtype != 'install' and packtype != 'update':
                self.error(ERROR_INVALID_PACKAGE_FILE, 'no support for type %s' % packtype, exit=False)
                return

            # add the file if it's an install, or update if installed
            for fn in files:
                if fn.endswith('.rpm'):
                    inst_file = os.path.join(tempdir, fn)
                    try:
                        # read the file
                        pkg = YumLocalPackage(ts=self.yumbase.rpmdb.readOnlyTS(), filename=inst_file)
                        pkgs_local = self.yumbase.rpmdb.searchNevra(name=pkg.name)
                    except yum.Errors.MiscError:
                        self.error(ERROR_INVALID_PACKAGE_FILE, "%s does not appear to be a valid package." % inst_file)
                    except yum.Errors.YumBaseError, e:
                        self.error(ERROR_INVALID_PACKAGE_FILE, 'Package could not be decompressed')
                    except exceptions.IOError, e:
                        self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                    except:
                        self.error(ERROR_UNKNOWN, "Failed to open local file -- please report")
                    else:
                        # trying to install package that already exists
                        if len(pkgs_local) == 1 and pkgs_local[0].EVR == pkg.EVR:
                            self.message(MESSAGE_PACKAGE_ALREADY_INSTALLED, '%s is already installed and the latest version' % pkg.name)

                        # trying to install package older than already exists
                        elif len(pkgs_local) == 1 and pkgs_local[0].EVR > pkg.EVR:
                            self.message(MESSAGE_PACKAGE_ALREADY_INSTALLED, 'a newer version of %s is already installed' % pkg.name)

                        # only update if installed
                        elif packtype == 'update':
                            if len(pkgs_local) > 0:
                                inst_files.append(inst_file)

                        # only install if we passed the checks above
                        elif packtype == 'install':
                            inst_files.append(inst_file)

        if len(inst_files) == 0:
            # More than one pkg to be installed, all of them already installed
            self.error(ERROR_ALL_PACKAGES_ALREADY_INSTALLED,
                       'All of the specified packages have already been installed')

        self._set_only_trusted(only_trusted or simulate)

        # self.yumbase.installLocal fails for unsigned packages when self.yumbase.conf.gpgcheck = 1
        # This means we don't run runYumTransaction, and don't get the GPG failure in
        # PackageKitYumBase(_checkSignatures) -- so we check here
        for inst_file in inst_files:
            try:
                po = YumLocalPackage(ts=self.yumbase.rpmdb.readOnlyTS(), filename=inst_file)
            except yum.Errors.MiscError:
                self.error(ERROR_INVALID_PACKAGE_FILE, "%s does not appear to be a valid package." % inst_file, exit=False)
                return
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            try:
                self.yumbase._checkSignatures([po], None)
            except yum.Errors.YumGPGCheckError, e:
                if only_trusted:
                    self.error(ERROR_MISSING_GPG_SIGNATURE, _to_unicode(e), exit=False)
                    return
                self._show_package(po, INFO_UNTRUSTED)
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

        # common checks copied from yum
        for inst_file in inst_files:
            if not self._check_local_file(inst_file):
                return

        txmbrs = []
        try:
            for inst_file in inst_files:
                try:
                    txmbr = self.yumbase.installLocal(inst_file)
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                if txmbr:
                    txmbrs.extend(txmbr)
                    self._checkForNewer(txmbr[0].po)
                    # Added the package to the transaction set
                else:
                    self.error(ERROR_LOCAL_INSTALL_FAILED, "Can't install %s as no transaction" % _to_unicode(inst_file))
            if len(self.yumbase.tsInfo) == 0:
                self.error(ERROR_LOCAL_INSTALL_FAILED, "Can't install %s" % " or ".join(inst_files), exit=False)
                return

            try:
                self._runYumTransaction(only_simulate=simulate)
            except PkError, e:
                self.error(e.code, e.details, exit=False)
                return

        except yum.Errors.InstallError, e:
            self.error(ERROR_LOCAL_INSTALL_FAILED, _to_unicode(e))
        except (yum.Errors.RepoError, yum.Errors.PackageSackError, IOError):
            # We might not be able to connect to the internet to get
            # repository metadata, or the package might not exist.
            # Try again, (temporarily) disabling repos first.
            try:
                for repo in self.yumbase.repos.listEnabled():
                    repo.disable()

                for inst_file in inst_files:
                    try:
                        txmbr = self.yumbase.installLocal(inst_file)
                    except exceptions.IOError, e:
                        self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                    except Exception, e:
                        self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                    if txmbr:
                        txmbrs.extend(txmbr)
                        if len(self.yumbase.tsInfo) > 0:
                            if not self.yumbase.tsInfo.pkgSack:
                                self.yumbase.tsInfo.pkgSack = MetaSack()
                            try:
                                self._runYumTransaction(only_simulate=simulate)
                            except PkError, e:
                                self.error(e.code, e.details, exit=False)
                                return
                    else:
                        self.error(ERROR_LOCAL_INSTALL_FAILED, "Can't install %s" % inst_file)
            except yum.Errors.InstallError, e:
                self.error(ERROR_LOCAL_INSTALL_FAILED, _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        shutil.rmtree(tempdir)

    def _check_local_file(self, pkg):
        """
        Duplicates some of the checks that yumbase.installLocal would
        do, so we can get decent error reporting.
        """
        po = None
        try:
            po = YumLocalPackage(ts=self.yumbase.rpmdb.readOnlyTS(), filename=pkg)
        except yum.Errors.MiscError:
            self.error(ERROR_INVALID_PACKAGE_FILE, "%s does not appear to be a valid package." % pkg, exit=False)
            return False
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            return False
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            return False

        # check if wrong arch
        suitable_archs = rpmUtils.arch.getArchList()
        if po.arch not in suitable_archs:
            self.error(ERROR_INCOMPATIBLE_ARCHITECTURE, "Package %s has incompatible architecture %s. Valid architectures are %s" % (pkg, po.arch, suitable_archs), exit=False)
            return False

        # check already installed
        if self._is_inst_arch(po):
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED, "The package %s is already installed" % str(po), exit=False)
            return False

        # check if excluded
        if len(self.yumbase.conf.exclude) > 0:
            exactmatch, matched, unmatched = parsePackages([po], self.yumbase.conf.exclude, casematch=1)
            if po in exactmatch + matched:
                self.error(ERROR_PACKAGE_INSTALL_BLOCKED, "Installation of %s is excluded by yum configuration." % pkg, exit=False)
                return False

        return True

    def update_packages(self, only_trusted, package_ids):
        self._update_packages(only_trusted, package_ids)

    def simulate_update_packages(self, package_ids):
        self._update_packages(False, package_ids, True)

    def _update_packages(self, only_trusted, package_ids, simulate=False):
        '''
        Implement the install functionality
        This will only work with yum 3.2.4 or higher
        '''
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(False)
        self.percentage(0)
        self.status(STATUS_RUNNING)

        self._set_only_trusted(only_trusted or simulate)

        txmbrs = []
        try:
            for package_id in package_ids:
                try:
                    pkg, inst = self._findPackage(package_id)
                except PkError, e:
                    if e.code == ERROR_PACKAGE_NOT_FOUND:
                        self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, e.details)
                        package_ids.remove(package_id)
                        continue
                    self.error(e.code, e.details, exit=True)
                    return
                if pkg:
                    try:
                        txmbr = self.yumbase.update(po=pkg)
                    except exceptions.IOError, e:
                        self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                    except Exception, e:
                        self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                    if not txmbr:
                        self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, "could not add package update for %s: %s" % (_format_package_id(package_id), pkg))
                    else:
                        txmbrs.extend(txmbr)
        except yum.Errors.RepoError, e:
            self.error(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e), exit=False)
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        else:
            if txmbrs:
                for t in txmbrs:
                    # ignore transactions that do not have to be checked, e.g. obsoleted
                    if t.output_state not in self.transaction_sig_check_map:
                        continue
                    pkg = t.po
                    try:
                        signed = self._is_package_repo_signed(pkg)
                    except PkError, e:
                        self.error(e.code, e.details, exit=False)
                        return
                    if signed:
                        continue
                    if only_trusted:
                        self.error(ERROR_CANNOT_UPDATE_REPO_UNSIGNED, "The package %s will not be updated from unsigned repo %s" % (pkg.name, pkg.repoid), exit=False)
                        return
                    self._show_package(pkg, INFO_UNTRUSTED)
                try:
                    self._runYumTransaction(allow_skip_broken=True, only_simulate=simulate)
                except PkError, e:
                    self.error(e.code, e.details, exit=False)
            else:
                self.error(ERROR_TRANSACTION_ERROR, "No transaction to process", exit=False)

    def _check_for_reboot(self):
        md = self.updateMetadata
        for txmbr in self.yumbase.tsInfo:
            pkg = txmbr.po
            # check if package is in reboot list or flagged with reboot_suggested
            # in the update metadata and is installed/updated etc
            notice = md.get_notice((pkg.name, pkg.version, pkg.release))
            if (pkg.name in self.rebootpkgs \
                or (notice and notice.get_metadata().has_key('reboot_suggested') and notice['reboot_suggested'])):
                self.require_restart(RESTART_SYSTEM, self._pkg_to_id(pkg))

    def _runYumTransaction(self, allow_remove_deps=None, allow_skip_broken=False, only_simulate=False):
        '''
        Run the yum Transaction
        This will only work with yum 3.2.4 or higher
        '''
        message = ''
        try:
            self.yumbase.conf.skip_broken = 0
            rc, msgs = self.yumbase.buildTransaction()
            message = _format_msgs(msgs)
        except yum.Errors.RepoError, e:
            raise PkError(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e))
        except yum.Errors.PackageSackError, e:
            raise PkError(ERROR_PACKAGE_DATABASE_CHANGED, _to_unicode(e))
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

        # if return value is 1 (error), try again with skip-broken if allowed
        if allow_skip_broken and rc == 1:
            try:
                self.yumbase.conf.skip_broken = 1
                rc, msgs = self.yumbase.buildTransaction()
                message += " : %s" % _format_msgs(msgs)
            except yum.Errors.RepoError, e:
                raise PkError(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e))
            except exceptions.IOError, e:
                raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

        # test did succeed
        self._check_for_reboot()
        if allow_remove_deps == False:
            if len(self.yumbase.tsInfo) > 1:
                retmsg = 'package could not be removed, as other packages depend on it'
                raise PkError(ERROR_DEP_RESOLUTION_FAILED, retmsg)

        # we did not succeed
        if rc != 2:
            if message.find ("is needed by") != -1:
                raise PkError(ERROR_DEP_RESOLUTION_FAILED, message)
            if message.find ("empty transaction") != -1:
                raise PkError(ERROR_NO_PACKAGES_TO_UPDATE, message)
            raise PkError(ERROR_TRANSACTION_ERROR, message)

        # abort now we have the package list
        if only_simulate:
            package_list = []
            for txmbr in self.yumbase.tsInfo:
                if txmbr.output_state in TransactionsInfoMap.keys():
                    info = TransactionsInfoMap[txmbr.output_state]
                    package_list.append((txmbr.po, info))

            self.percentage(90)
            self._show_package_list(package_list)
            self.percentage(100)
            return

        try:
            rpmDisplay = PackageKitCallback(self)
            callback = ProcessTransPackageKitCallback(self)
            self.yumbase.processTransaction(callback=callback,
                                  rpmDisplay=rpmDisplay)
        except yum.Errors.YumDownloadError, ye:
            raise PkError(ERROR_PACKAGE_DOWNLOAD_FAILED, _format_msgs(ye.value))
        except yum.Errors.YumGPGCheckError, ye:
            raise PkError(ERROR_BAD_GPG_SIGNATURE, _format_msgs(ye.value))
        except GPGKeyNotImported, e:
            keyData = self.yumbase.missingGPGKey
            if not keyData:
                raise PkError(ERROR_BAD_GPG_SIGNATURE, "GPG key not imported, and no GPG information was found.")
            package_id = self._pkg_to_id(keyData['po'])
            fingerprint = keyData['fingerprint']()
            hex_fingerprint = "%02x" * len(fingerprint) % tuple(map(ord, fingerprint))
            # Borrowed from http://mail.python.org/pipermail/python-list/2000-September/053490.html

            self.repo_signature_required(package_id,
                                         keyData['po'].repoid,
                                         keyData['keyurl'].replace("file://", ""),
                                         keyData['userid'],
                                         keyData['hexkeyid'],
                                         hex_fingerprint,
                                         time.ctime(keyData['timestamp']),
                                         'gpg')
            raise PkError(ERROR_GPG_FAILURE, "GPG key %s required" % keyData['hexkeyid'])
        except yum.Errors.YumBaseError, ye:
            message = _format_msgs(ye.value)
            if message.find ("conflicts with file") != -1:
                raise PkError(ERROR_FILE_CONFLICTS, message)
            if message.find ("rpm_check_debug vs depsolve") != -1:
                raise PkError(ERROR_PACKAGE_CONFLICTS, message)
            else:
                raise PkError(ERROR_TRANSACTION_ERROR, message)
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

    def remove_packages(self, allowdep, autoremove, package_ids):
        self._remove_packages(allowdep, autoremove, package_ids)

    def simulate_remove_packages(self, package_ids):
        self._remove_packages(True, False, package_ids, True)

    def _remove_packages(self, allowdep, autoremove, package_ids, simulate=False):
        '''
        Implement the remove functionality
        Needed to be implemented in a sub class
        '''
        # TODO: use autoremove
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(False)
        self.percentage(0)
        self.status(STATUS_RUNNING)

        if autoremove:
            self.message(MESSAGE_PARAMETER_INVALID, "the yum backend does not support autoremove")

        txmbrs = []
        for package_id in package_ids:
            grp = self._is_meta_package(package_id)
            if grp:
                if not grp.installed:
                    self.error(ERROR_PACKAGE_NOT_INSTALLED, "This Group %s is not installed" % grp.groupid)
                try:
                    txmbr = self.yumbase.groupRemove(grp.groupid)
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                txmbrs.extend(txmbr)
            else:
                try:
                    pkg, inst = self._findPackage(package_id)
                except PkError, e:
                    self.error(e.code, e.details, exit=True)
                    return
                if pkg and inst:
                    try:
                        txmbr = self.yumbase.remove(po=pkg)
                    except exceptions.IOError, e:
                        self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                    except Exception, e:
                        self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                    txmbrs.extend(txmbr)
                if pkg and not inst:
                    self.error(ERROR_PACKAGE_NOT_INSTALLED, "The package %s is not installed" % pkg.name)
        if txmbrs:
            # check to find any system packages
            try:
                rc, msgs =  self.yumbase.buildTransaction()
            except yum.Errors.RepoError, e:
                self.error(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e))
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            if rc != 2:
                self.error(ERROR_DEP_RESOLUTION_FAILED, _format_msgs(msgs))
            else:
                for txmbr in self.yumbase.tsInfo:
                    pkg = txmbr.po
                    if pkg.name in self.system_packages:
                        self.error(ERROR_CANNOT_REMOVE_SYSTEM_PACKAGE, "The package %s is essential to correct operation and cannot be removed using this tool." % pkg.name, exit=False)
                        return
            try:
                if not allowdep:
                    self._runYumTransaction(allow_remove_deps=False, only_simulate=simulate)
                else:
                    self._runYumTransaction(allow_remove_deps=True, only_simulate=simulate)
            except PkError, e:
                self.error(e.code, e.details, exit=False)
        else:
            msg = "The following packages failed to be removed: %s" % str(package_ids)
            self.error(ERROR_PACKAGE_NOT_INSTALLED, msg, exit=False)

    def _get_category(self, groupid):
        cat_id = self.comps.get_category(groupid)
        if self.yumbase.comps._categories.has_key(cat_id):
            return self.yumbase.comps._categories[cat_id]
        else:
            return None

    def get_details(self, package_ids):
        '''
        Print a detailed details for a given package
        '''
        try:
            self._check_init(lazy_cache=True)
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for package_id in package_ids:
            grp = self._is_meta_package(package_id)
            if grp:
                package_id = "%s;;;meta" % grp.groupid
                desc = grp.descriptionByLang(self.lang)
                desc = desc.replace('\n\n', ';')
                desc = desc.replace('\n', ' ')
                group = GROUP_COLLECTIONS
                pkgs = self._get_group_packages(grp)
                size = 0
                for pkg in pkgs:
                    size = size + pkg.size
                self.details(package_id, "", group, desc, "", size)

            else:
                try:
                    pkg, inst = self._findPackage(package_id)
                except PkError, e:
                    if e.code == ERROR_PACKAGE_NOT_FOUND:
                        self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, e.details)
                        continue
                    self.error(e.code, e.details, exit=True)
                    return
                if pkg:
                    self._show_details_pkg(pkg)
                else:
                    self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, 'Package %s was not found' % _format_package_id(package_id))
                    continue

    def _show_details_pkg(self, pkg):

        pkgver = _get_package_ver(pkg)
        package_id = self.get_package_id(pkg.name, pkgver, pkg.arch, pkg.repo)
        desc = _to_unicode(pkg.description)
        url = _to_unicode(pkg.url)
        license = _to_unicode(pkg.license)

        # some RPM's (especially from google) have no description
        if desc:
            desc = desc.replace('\n', ';')
            desc = desc.replace('\t', ' ')
        else:
            desc = ''

        # if we are remote and in the cache, our size is zero
        size = pkg.size
        if not pkg.repo.id.startswith('installed') and pkg.verifyLocalPkg():
            size = 0

        group = self.comps.get_group(pkg.name)
        self.details(package_id, license, group, desc, url, size)

    def get_files(self, package_ids):
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for package_id in package_ids:
            try:
                pkg, inst = self._findPackage(package_id)
            except PkError, e:
                self.error(e.code, e.details, exit=True)
                return
            if pkg:
                files = pkg.returnFileEntries('dir')
                files.extend(pkg.returnFileEntries()) # regular files
                file_list = ";".join(files)
                self.files(package_id, file_list)
            else:
                self.error(ERROR_PACKAGE_NOT_FOUND, 'Package %s was not found' % package_id)

    def _pkg_to_id(self, pkg):
        pkgver = _get_package_ver(pkg)
        repo = str(pkg.repo)
        if repo.startswith('/'):
            repo = "local"
        # can we add data from the yumdb
        if repo == 'installed':
            repo_tmp = pkg.yumdb_info.get('from_repo')
            if repo_tmp:
                repo = 'installed:' + repo_tmp
        package_id = self.get_package_id(pkg.name, pkgver, pkg.arch, repo)
        return package_id

    def _show_package(self, pkg, status):
        '''  Show info about package'''
        package_id = self._pkg_to_id(pkg)
        self.package(package_id, status, pkg.summary)

    def get_distro_upgrades(self):
        '''
        Implement the get-distro-upgrades functionality
        '''
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)

        # if we're RHEL, then we don't have preupgrade
        if not os.path.exists('/usr/share/preupgrade/releases.list'):
            return

        # parse the releases file
        config = ConfigParser.ConfigParser()
        config.read('/usr/share/preupgrade/releases.list')

        # find the newest release
        newest = None
        last_version = 0
        for section in config.sections():
            # we only care about stable versions
            if config.has_option(section, 'stable') and config.getboolean(section, 'stable'):
                version = config.getfloat(section, 'version')
                if (version > last_version):
                    newest = section
                    last_version = version

        # got no valid data
        if not newest:
            self.error(ERROR_FAILED_CONFIG_PARSING, "could not get latest distro data")

        # are we already on the latest version
        try:
            present_version = float(self.yumbase.conf.yumvar['releasever'])
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        if (present_version >= last_version):
            return

        # if we have an upgrade candidate then pass back data to daemon
        tok = newest.split(" ")
        name = "%s-%s" % (tok[0].lower(), tok[1])
        self.distro_upgrade(DISTRO_UPGRADE_STABLE, name, newest)

    def _get_status(self, notice):
        ut = notice['type']
        if ut == 'security':
            return INFO_SECURITY
        elif ut == 'bugfix':
            return INFO_BUGFIX
        elif ut == 'enhancement':
            return INFO_ENHANCEMENT
        elif ut == 'newpackage':
            return INFO_ENHANCEMENT
        else:
            self.message(MESSAGE_BACKEND_ERROR, "status unrecognised, please report in bugzilla: %s" % ut)
            return INFO_NORMAL

    def get_updates(self, filters):
        '''
        Implement the get-updates functionality
        @param filters: package types to show
        '''
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        # yum 'helpfully' keeps an array of updates available
        self.yumbase.up = None

        # clear the package sack so we can get new updates
        self.yumbase.pkgSack = None

        package_list = []
        pkgfilter = YumFilter(filters)
        pkgs = []
        try:
            ygl = self.yumbase.doPackageLists(pkgnarrow='updates')
            pkgs.extend(ygl.updates)
            ygl = self.yumbase.doPackageLists(pkgnarrow='obsoletes')
            pkgs.extend(ygl.obsoletes)
        except yum.Errors.RepoError, e:
            self.error(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e))
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

        # some packages should be updated before the others
        infra_pkgs = []
        for pkg in pkgs:
            if pkg.name in self.infra_packages or pkg.name.partition('-')[0] in self.infra_packages:
                infra_pkgs.append(pkg)
        if len(infra_pkgs) > 0:
            if len(infra_pkgs) < len(pkgs):
                msg = []
                for pkg in infra_pkgs:
                    msg.append(pkg.name)
                self.message(MESSAGE_OTHER_UPDATES_HELD_BACK, "Infrastructure packages take priority. " \
                             "The packages '%s' will be updated before other packages" % msg)
            pkgs = infra_pkgs

        # get the list of installed updates as this is needed for get_applicable_notices()
        installed_dict = {}
        for pkgtup_updated, pkgtup_installed in self.yumbase.up.getUpdatesTuples():
            installed_dict[pkgtup_installed[0]] = pkgtup_installed

        md = self.updateMetadata
        for pkg in unique(pkgs):
            if pkgfilter._filter_base(pkg):
                # we pre-get the ChangeLog data so that the changes file is
                # downloaded at GetUpdates time, not when we open the GUI
                # get each element of the ChangeLog
                try:
                    changelog = pkg.returnChangelog()
                except yum.Errors.RepoError, e:
                    self.error(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e))
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

                # fall back to this if there is no installed update or there is no metadata
                status = INFO_NORMAL

                # Get info about package in updates info (using the installed update of this name)
                if installed_dict.has_key(pkg.name):
                    pkgtup = installed_dict[pkg.name]
                    notices = md.get_applicable_notices(pkgtup)
                    if notices:
                        for (pkgtup, notice) in notices:
                            status = self._get_status(notice)
                            if status == INFO_SECURITY:
                                break
                pkgfilter.add_custom(pkg, status)

        package_list = pkgfilter.get_package_list()
        self._show_package_list(package_list)

    def repo_enable(self, repoid, enable):
        '''
        Implement the repo-enable functionality
        '''
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.status(STATUS_INFO)
        try:
            repo = self.yumbase.repos.getRepo(repoid)
            if not enable:
                if repo.isEnabled():
                    repo.disablePersistent()
            else:
                if not repo.isEnabled():
                    repo.enablePersistent()
                    if repoid.find ("rawhide") != -1:
                        warning = "These packages are untested and still under development." \
                                  "This repository is used for development of new releases.\n\n" \
                                  "This repository can see significant daily turnover and major " \
                                  "functionality changes which cause unexpected problems with " \
                                  "other development packages.\n" \
                                  "Please use these packages if you want to work with the " \
                                  "Fedora developers by testing these new development packages.\n\n" \
                                  "If this is not correct, please disable the %s software source." % repoid
                        self.message(MESSAGE_REPO_FOR_DEVELOPERS_ONLY, warning.replace("\n", ";"))
        except yum.Errors.RepoError, e:
            self.error(ERROR_REPO_NOT_FOUND, _to_unicode(e))
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

        # update the comps groups too
        self.comps.refresh()

    def get_repo_list(self, filters):
        '''
        Implement the get-repo-list functionality
        '''
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.status(STATUS_INFO)

        try:
            repos = self.yumbase.repos.repos.values()
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            return
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
            return
        for repo in repos:
            if not FILTER_NOT_DEVELOPMENT in filters or not _is_development_repo(repo.id):
                enabled = repo.isEnabled()
                self.repo_detail(repo.id, repo.name, enabled)

    def _get_obsoleted(self, name):
        try:
            # make sure yum doesn't explode in some internal fit of rage
            self.yumbase.up.doObsoletes()
            obsoletes = self.yumbase.up.getObsoletesTuples(newest=1)
            for (obsoleting, installed) in obsoletes:
                if obsoleting[0] == name:
                    pkg =  self.yumbase.rpmdb.searchPkgTuple(installed)[0]
                    return self._pkg_to_id(pkg)
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            pass # no obsolete data - fd#17528
        return ""

    def _get_updated(self, pkg):
        try:
            pkgs = self.yumbase.rpmdb.searchNevra(name=pkg.name, arch=pkg.arch)
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
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
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    pass # No updateinfo.xml.gz in repo
        return self._updateMetadata

    _updateMetadata = None
    updateMetadata = property(fget=_get_update_metadata)

    def _get_update_extras(self, pkg):
        md = self.updateMetadata
        notice = md.get_notice((pkg.name, pkg.version, pkg.release))
        urls = {'bugzilla':[], 'cve' : [], 'vendor': []}
        if notice:
            # Update Details
            desc = notice['description']
            if desc:
                desc = desc.replace("\t", " ")

            # add link to bohdi if available
            if notice['from'].find('updates@fedoraproject.org') != -1:
                if notice['update_id']:
                    releasever = self.yumbase.conf.yumvar['releasever']
                    href = "https://admin.fedoraproject.org/updates/F%s/%s" % (releasever, notice['update_id'])
                    title = "%s Update %s" % (notice['release'], notice['update_id'])
                    urls['vendor'].append("%s;%s" % (href, title))

            # Update References (Bugzilla, CVE ...)
            refs = notice['references']
            if refs:
                for ref in refs:
                    typ = ref['type']
                    href = ref['href']
                    title = ref['title'] or ""

                    # Description can sometimes have ';' in them, and we use that as the delimiter
                    title = title.replace(";", ", ")

                    if href:
                        if typ in ('bugzilla', 'cve'):
                            urls[typ].append("%s;%s" % (href, title))
                        else:
                            urls['vendor'].append("%s;%s" % (href, title))

            # other interesting data:
            changelog = ''
            state = notice['status'] or ''
            issued = notice['issued'] or ''
            updated = notice['updated'] or ''
            if updated == issued:
                updated = ''

            # Reboot flag
            if notice.get_metadata().has_key('reboot_suggested') and notice['reboot_suggested']:
                reboot = 'system'
            else:
                reboot = 'none'
            return _format_str(desc), urls, reboot, changelog, state, issued, updated
        else:
            return "", urls, "none", '', '', '', ''

    def get_update_detail(self, package_ids):
        '''
        Implement the get-update_detail functionality
        '''
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        for package_id in package_ids:
            try:
                pkg, inst = self._findPackage(package_id)
            except PkError, e:
                if e.code == ERROR_PACKAGE_NOT_FOUND:
                    self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, e.details)
                    continue
                self.error(e.code, e.details, exit=True)
                return
            if pkg == None:
                self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, "could not find %s" % _format_package_id(package_id))
                continue
            update = self._get_updated(pkg)
            obsolete = self._get_obsoleted(pkg.name)
            desc, urls, reboot, changelog, state, issued, updated = self._get_update_extras(pkg)

            # the metadata stores broken ISO8601 formatted values
            issued = issued.replace(" ", "T")
            updated = updated.replace(" ", "T")

            # extract the changelog for the local package
            if len(changelog) == 0:

                # get the current installed version of the package
                instpkg = None
                try:
                    instpkgs = self.yumbase.rpmdb.searchNevra(name=pkg.name)
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                if len(instpkgs) == 1:
                    instpkg = instpkgs[0]

                # get each element of the ChangeLog
                try:
                    changes = pkg.returnChangelog()
                except yum.Errors.RepoError, e:
                    self.error(ERROR_REPO_NOT_AVAILABLE, _to_unicode(e))
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                for change in changes:

                    # ensure change has require number of fields
                    if len(change) != 3:
                        changelog += ";*Could not parse change element:* '%s';" % str(change)
                        continue

                    # get version number from "Seth Vidal <skvidal at fedoraproject.org> - 3:3.2.20-1"
                    header = _to_unicode(change[1])
                    version = header.rsplit(' ', 1)

                    # is older than what we have already?
                    if instpkg:
                        evr = ('0', '0', '0')
                        try:
                            evr = _getEVR(version[1])
                        except Exception, e:
                            pass
                        if evr == ('0', '0', '0'):
                            changelog += ";*Could not parse header:* '%s', *expected*: 'Firstname Lastname <email@account.com> - version-release';" % header
                        rc = rpmUtils.miscutils.compareEVR((instpkg.epoch, instpkg.version, instpkg.release.split('.')[0]), evr)
                        if rc >= 0:
                            break

                    gmtime = time.gmtime(change[0])
                    time_str = "%i-%02i-%02i" % (gmtime[0], gmtime[1], gmtime[2])
                    body = _to_unicode(change[2].replace("\t", " "))
                    changelog += _format_str('**' + time_str + '** ' + header + '\n' + body + '\n\n')

            cve_url = _format_list(urls['cve'])
            bz_url = _format_list(urls['bugzilla'])
            vendor_url = _format_list(urls['vendor'])
            self.update_detail(package_id, update, obsolete, vendor_url, bz_url, cve_url, reboot, desc, changelog, state, issued, updated)

    def repo_set_data(self, repoid, parameter, value):
        '''
        Implement the repo-set-data functionality
        '''
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        # Get the repo
        try:
            repo = self.yumbase.repos.getRepo(repoid)
        except yum.Errors.RepoError, e:
            self.error(ERROR_REPO_NOT_FOUND, "repo '%s' cannot be found in list" % repoid, exit=False)
        except exceptions.IOError, e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
        else:
            if not repo:
                self.error(ERROR_REPO_NOT_FOUND, 'repo %s not found' % repoid, exit=False)
                return
            repo.cfg.set(repoid, parameter, value)
            try:
                repo.cfg.write(file(repo.repofile, 'w'))
            except IOError, e:
                self.error(ERROR_CANNOT_WRITE_REPO_CONFIG, _to_unicode(e))

    def install_signature(self, sigtype, key_id, package_id):
        try:
            self._check_init()
        except PkError, e:
            self.error(e.code, e.details, exit=False)
            return
        self.yumbase.conf.cache = 0 # Allow new files
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        if package_id.startswith(';;;'): #This is a repo signature
            repoid = package_id.split(';')[-1]
            repo = self.yumbase.repos.getRepo(repoid)
            if repo:
                try:
                    self.yumbase.repos.doSetup(thisrepo=repoid)
                    self.yumbase.getKeyForRepo(repo, callback = lambda x: True)
                except yum.Errors.YumBaseError, e:
                    self.error(ERROR_UNKNOWN, "cannot install signature: %s" % str(e))
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_GPG_FAILURE, "Error importing GPG Key for the %s repository: %s" % (repo, str(e)))
        else: # This is a package signature
            try:
                pkg, inst = self._findPackage(package_id)
            except PkError, e:
                self.error(e.code, e.details, exit=True)
                return
            if pkg:
                try:
                    self.yumbase.getKeyForPackage(pkg, askcb = lambda x, y, z: True)
                except yum.Errors.YumBaseError, e:
                    self.error(ERROR_UNKNOWN, "cannot install signature: %s" % str(e))
                except exceptions.IOError, e:
                    self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
                except Exception, e:
                    self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
                except:
                    self.error(ERROR_GPG_FAILURE, "Error importing GPG Key for %s" % pkg)


    def _check_init(self, lazy_cache=False):
        '''Just does the caching tweaks'''

        # this entire section can be cancelled
        self.allow_cancel(True)

        # clear previous transaction data
        self.yumbase._tsInfo = None

        # we are working offline
        if not self.has_network:
            for repo in self.yumbase.repos.listEnabled():
                repo.metadata_expire = -1  # never refresh
            self.yumbase.conf.cache = 1

        # choose a good default if the client didn't specify a timeout
        if self.cache_age == 0:
            self.cache_age = 60 * 60 * 24  # 24 hours
        for repo in self.yumbase.repos.listEnabled():
            # is physical media
            if repo.mediaid:
                continue
            repo.metadata_expire = self.cache_age

        # disable repos that are not contactable
        for repo in self.yumbase.repos.listEnabled():
            try:
                if not repo.mediaid:
                    repo.repoXML
                else:
                    root = self.yumbase._media_find_root(repo.mediaid)
                    if not root:
                        self.yumbase.repos.disableRepo(repo.id)
                        self.message(MESSAGE_REPO_METADATA_DOWNLOAD_FAILED,
                                     "Could not contact media source '%s', so it will be disabled" % repo.id)
            except exceptions.IOError, e:
                self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except yum.Errors.RepoError, e:
                self.yumbase.repos.disableRepo(repo.id)
                self.message(MESSAGE_REPO_METADATA_DOWNLOAD_FAILED, "Could not contact source '%s', so it will be disabled" % repo.id)

        # should we suggest yum-complete-transaction?
        unfinished = yum.misc.find_unfinished_transactions(yumlibpath=self.yumbase.conf.persistdir)
        if unfinished and not lazy_cache:
            raise PkError(ERROR_FAILED_INITIALIZATION, 'There are unfinished transactions remaining. Please run yum-complete-transaction as root.')

        # default to 100% unless method overrides
        self.yumbase.conf.throttle = "90%"

        # do not use parallel downloading
        self.yumbase.conf.async = False

    def _setup_yum(self):
        try:
            # setup Yum Config
            self.yumbase.doConfigSetup(errorlevel=-1, debuglevel=-1)
        except Exception, e:
            raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

        self.yumbase.rpmdb.auto_close = True
        self.dnlCallback = DownloadCallback(self, showNames=True)
        self.yumbase.repos.setProgressBar(self.dnlCallback)

class DownloadCallback(BaseMeter):
    """ Customized version of urlgrabber.progress.BaseMeter class """
    def __init__(self, base, showNames = False):
        BaseMeter.__init__(self)
        self.base = base
        self.percent_start = 0
        self.saved_pkgs = None
        self.number_packages = 0
        self.download_package_number = 0

    def setPackages(self, new_pkgs, percent_start, percent_length):
        self.saved_pkgs = new_pkgs
        self.number_packages = float(len(self.saved_pkgs))
        self.percent_start = percent_start

    def _getPackage(self, name):

        # no name
        if not name:
            return

        # no download data
        if not self.saved_pkgs:
            return None

        # split into name, version, release
        # for yum, name is:
        #  - gnote-0.1.2-2.fc11.i586.rpm
        # and for Presto:
        #  - gnote-0.1.1-4.fc11_0.1.2-2.fc11.i586.drpm
        sections = name.rsplit('-', 2)
        if len(sections) < 3:
            return None

        # we need to search the saved packages for a match and then return the pkg
        for pkg in self.saved_pkgs:
            if sections[0] == pkg.name:
                return pkg

        # nothing matched
        return None

    def update(self, amount_read, now=None):
        BaseMeter.update(self, amount_read, now)

    def _do_start(self, now=None):
        name = self._getName()
        if not name:
            return
        self.updateProgress(name, 0.0, "", "")

    def _do_update(self, amount_read, now=None):

        fread = format_number(amount_read)
        name = self._getName()
        if self.size is None:
            # Elapsed time
            etime = self.re.elapsed_time()
            frac = 0.0
            self.updateProgress(name, frac, fread, '')
        else:
            # Remaining time
            rtime = self.re.remaining_time()
            frac = self.re.fraction_read()
            self.updateProgress(name, frac, fread, '')

    def _do_end(self, amount_read, now=None):

        total_size = format_number(amount_read)
        name = self._getName()
        if not name:
            return
        self.updateProgress(name, 1.0, total_size, '')

    def _getName(self):
        '''
        Get the name of the package being downloaded
        '''
        return self.basename

    def updateProgress(self, name, frac, fread, ftime):
        '''
         Update the progressbar (Overload in child class)
        @param name: filename
        @param frac: Progress fracment (0 -> 1)
        @param fread: formated string containing BytesRead
        @param ftime: formated string containing remaining or elapsed time
        '''

        val = int(frac*100)

        # new package
        if val == 0 and name:
            pkg = self._getPackage(name)
            if pkg: # show package to download
                self.base._show_package(pkg, INFO_DOWNLOADING)
            else:
                for key in MetaDataMap.keys():
                    if key in name:
                        typ = MetaDataMap[key]
                        self.base.status(typ)
                        break

        # package finished
        if val == 100 and name:
            pkg = self._getPackage(name)
            if pkg:
                self.base._show_package(pkg, INFO_FINISHED)

        # set sub-percentage
        self.base.sub_percentage(val)

        # refine percentage with subpercentage
        pct_start = StatusPercentageMap[STATUS_DOWNLOAD]
        pct_end = StatusPercentageMap[STATUS_SIG_CHECK]

        if self.number_packages > 0:
            div = (pct_end - pct_start) / self.number_packages
            pct = pct_start + (div * self.download_package_number) + ((div / 100.0) * val)
            self.base.percentage(pct)

        # keep track of how many we downloaded
        if val == 100:
            self.download_package_number += 1

class PackageKitCallback(RPMBaseCallback):
    def __init__(self, base):
        RPMBaseCallback.__init__(self)
        self.base = base
        self.curpkg = None
        self.percent_start = 0
        self.percent_length = 0

    def _showName(self, status):
        # curpkg is a yum package object or simple string of the package name
        if type(self.curpkg) in types.StringTypes:
            package_id = self.base.get_package_id(self.curpkg, '', '', '')
            # we don't know the summary text
            self.base.package(package_id, status, "")
        else:
            # local file shouldn't put the path in the package_id
            repo_id = _to_unicode(self.curpkg.repo.id)
            if repo_id.find("/") != -1:
                repo_id = 'local'

            pkgver = _get_package_ver(self.curpkg)
            package_id = self.base.get_package_id(self.curpkg.name, pkgver, self.curpkg.arch, repo_id)
            self.base.package(package_id, status, self.curpkg.summary)

    def event(self, package, action, te_current, te_total, ts_current, ts_total):

        if str(package) != str(self.curpkg):
            self.curpkg = package
            try:
                self.base.status(TransactionsStateMap[action])
                self._showName(TransactionsInfoMap[action])
            except exceptions.IOError, e:
                self.base.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
            except exceptions.KeyError, e:
                self.base.message(MESSAGE_BACKEND_ERROR, "The constant '%s' was unknown, please report. details: %s" % (action, _to_unicode(e)))

        # do subpercentage
        if te_total > 0:
            val = (te_current*100L)/te_total
            self.base.sub_percentage(val)

        # find out the offset
        pct_start = StatusPercentageMap[STATUS_INSTALL]

        # do percentage
        if ts_total > 0:
            div = (100 - pct_start) / ts_total
            pct = div * (ts_current - 1) + pct_start + ((div / 100.0) * val)
            self.base.percentage(pct)

    def errorlog(self, msg):
        # grrrrrrrr
        pass

class ProcessTransPackageKitCallback:
    def __init__(self, base):
        self.base = base

    def event(self, state, data=None):

        if state == PT_DOWNLOAD:        # Start Downloading
            self.base.allow_cancel(True)
            pct_start = StatusPercentageMap[STATUS_DOWNLOAD]
            self.base.percentage(pct_start)
            self.base.status(STATUS_DOWNLOAD)
        elif state == PT_DOWNLOAD_PKGS:   # Packages to download
            self.base.dnlCallback.setPackages(data, 10, 30)
        elif state == PT_GPGCHECK:
            pct_start = StatusPercentageMap[STATUS_SIG_CHECK]
            self.base.percentage(pct_start)
            self.base.status(STATUS_SIG_CHECK)
        elif state == PT_TEST_TRANS:
            pct_start = StatusPercentageMap[STATUS_TEST_COMMIT]
            self.base.allow_cancel(False)
            self.base.percentage(pct_start)
            self.base.status(STATUS_TEST_COMMIT)
        elif state == PT_TRANSACTION:
            pct_start = StatusPercentageMap[STATUS_INSTALL]
            self.base.allow_cancel(False)
            self.base.percentage(pct_start)
        else:
            self.base.message(MESSAGE_BACKEND_ERROR, "unhandled transaction state: %s" % state)

class DepSolveCallback(object):

    # takes a PackageKitBackend so we can call StatusChanged on it.
    # That's kind of hurky.
    def __init__(self, backend):
        self.started = False
        self.backend = backend

    def start(self):
        if not self.started:
            self.backend.status(STATUS_DEP_RESOLVE)
            pct_start = StatusPercentageMap[STATUS_DEP_RESOLVE]
            self.backend.percentage(pct_start)

    # Be lazy and not define the others explicitly
    def _do_nothing(self, *args, **kwargs):
        pass

    def __getattr__(self, x):
        return self._do_nothing

class PackageKitYumBase(yum.YumBase):
    """
    Subclass of YumBase.  Needed so we can overload _checkSignatures
    and nab the gpg sig data
    """

    def __init__(self, backend):
        yum.YumBase.__init__(self)

        # YumBase.run_with_package_names is used to record which packages are
        # involved in executing a transaction
        if hasattr(self, 'run_with_package_names'):
            self.run_with_package_names.add('PackageKit-yum')

        # load the config file
        config = ConfigParser.ConfigParser()
        try:
            config.read('/etc/PackageKit/Yum.conf')
            disabled_plugins = config.get('Backend', 'DisabledPlugins').split(';')
        except ConfigParser.NoOptionError, e:
            disabled_plugins = []
        except Exception, e:
            raise PkError(ERROR_REPO_CONFIGURATION_ERROR, "Failed to load Yum.conf: %s" % _to_unicode(e))
        disabled_plugins.append('refresh-packagekit')

        # disable the PackageKit plugin when running under PackageKit
        try:
            pc = self.preconf
            pc.disabled_plugins = disabled_plugins
        except yum.Errors.ConfigError, e:
            raise PkError(ERROR_REPO_CONFIGURATION_ERROR, _to_unicode(e))
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except ValueError, e:
            raise PkError(ERROR_FAILED_CONFIG_PARSING, _to_unicode(e))

        # setup to use LANG for descriptions
        yum.misc.setup_locale(override_time=True, override_codecs=False)

        self.missingGPGKey = None
        self.dsCallback = DepSolveCallback(backend)
        self.backend = backend
        self.mediagrabber = self.MediaGrabber

        # Enable new callback mode on yum versions that support it
        self.use_txmbr_in_callback = True

        # setup Repo GPG support callbacks
        #
        # self.preconf may or may not exist at this point...
        # What we think is happening is that it's going:
        #
        #    PK.init => yum._getRepos => yum.conf =>
        #    plugins *.init_hook => RHN.init => conduit.getRepos() =>
        #    yum._getRepos
        #
        # ..at which point we try to setup from prerepoconf twice, and
        # the second time it fails.
        try:
            if hasattr(self, 'prerepoconf'):
                self.prerepoconf.confirm_func = self._repo_gpg_confirm
                self.prerepoconf.gpg_import_func = self._repo_gpg_import
            else:
                self.repos.confirm_func = self._repo_gpg_confirm
                self.repos.gpg_import_func = self._repo_gpg_import
        except exceptions.IOError, e:
            raise PkError(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % _to_unicode(e))
        except Exception, e:
            # helpfully, yum gives us TypeError when it can't open the rpmdb
            if str(e).find('rpmdb open failed') != -1:
                raise PkError(ERROR_FAILED_INITIALIZATION, _format_str(traceback.format_exc()))
            else:
                raise PkError(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

    def _media_find_root(self, media_id):
        """ returns the root "/media/Fedora Extras" or None """

        # search all the disks
        vm = gio.volume_monitor_get()
        mounts = vm.get_mounts()
        for mount in mounts:
            # is it mounted
            root = mount.get_root().get_path()

            # is it a media disc
            discinfo = "%s/.discinfo" % root
            if not os.path.exists(discinfo):
                continue

            # get the contents
            f = open(discinfo, "r")
            lines = f.readlines()
            f.close()

            # not enough lines to be a valid .discinfo
            if len(lines) < 3:
                continue

            # check this is the right disk
            media_id_tmp = lines[0].strip()
            if cmp(media_id_tmp, media_id) != 0:
                continue

            return root

        # nothing remaining
        return None

    def MediaGrabber(self, *args, **kwargs):
        """
        Handle physical media.
        """
        root = self._media_find_root(kwargs["mediaid"])
        if root:
            # the actual copying is done by URLGrabber
            ug = URLGrabber(checkfunc = kwargs["checkfunc"])
            try:
                ug.urlgrab("%s/%s" % (root, kwargs["relative"]),
                           kwargs["local"], text=kwargs["text"],
                           range=kwargs["range"], copy_local=1)
            except (IOError, URLGrabError), e:
                pass

        # we have to send a message to the client
        if not root:
            name = "%s Volume #%s" % (kwargs["name"], kwargs["discnum"])
            self.backend.media_change_required(MEDIA_TYPE_DISC, name, name)
            self.backend.error(ERROR_MEDIA_CHANGE_REQUIRED,
                               "Insert media labeled '%s' or disable media repos" % name,
                               exit=False)
            raise yum.Errors.MediaError, "The disc was not inserted"

        # yay
        return kwargs["local"]

    def _repo_gpg_confirm(self, keyData):
        """ Confirm Repo GPG signature import """
        if not keyData:
            self.backend.error(ERROR_BAD_GPG_SIGNATURE,
                       "GPG key not imported, and no GPG information was found.")
        repo = keyData['repo']
        fingerprint = keyData['fingerprint']()
        hex_fingerprint = "%02x" * len(fingerprint) % tuple(map(ord, fingerprint))
        # Borrowed from http://mail.python.org/pipermail/python-list/2000-September/053490.html

        self.backend.repo_signature_required(";;;%s" % repo.id,
                                     repo.id,
                                     keyData['keyurl'].replace("file://", ""),
                                     keyData['userid'],
                                     keyData['hexkeyid'],
                                     hex_fingerprint,
                                     time.ctime(keyData['timestamp']),
                                     'gpg')
        self.backend.error(ERROR_GPG_FAILURE, "GPG key %s required" % keyData['hexkeyid'])

    def _repo_gpg_import(self, repo, confirm):
        """ Repo GPG signature importer"""
        self.getKeyForRepo(repo, callback=confirm)

    def _checkSignatures(self, pkgs, callback):
        ''' The the signatures of the downloaded packages '''
        # This can be overloaded by a subclass.

        for po in pkgs:
            result, errmsg = self.sigCheckPkg(po)
            if result == 0:
                # verified ok, or verify not required
                continue
            elif result == 1:
                # verify failed but installation of the correct GPG key might help
                self.getKeyForPackage(po, fullaskcb=self._fullAskForGPGKeyImport)
            else:
                # fatal GPG verification error
                raise yum.Errors.YumGPGCheckError, errmsg
        return 0

    def _fullAskForGPGKeyImport(self, data):
        self.missingGPGKey = data

        raise GPGKeyNotImported()

    def _askForGPGKeyImport(self, po, userid, hexkeyid):
        '''
        Ask for GPGKeyImport
        '''
        # TODO: Add code here to send the RepoSignatureRequired signal
        return False

def main():
    backend = PackageKitYumBackend('', lock=True)
    backend.dispatcher(sys.argv[1:])

if __name__ == "__main__":
    main()
