#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Provides an apt backend to PackageKit

Copyright (C) 2007 Ali Sabil <ali.sabil@gmail.com>
Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
Copyright (C) 2008 Sebastian Heinlein <glatzor@ubuntu.com>

Licensed under the GNU General Public License Version 2

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

__author__  = "Sebastian Heinlein <devel@glatzor.de>"
__state__   = "experimental"

import os
import pty
import re
import signal
import time
import threading
import warnings

import apt
import apt_pkg
import dbus
import dbus.glib
import dbus.service
import dbus.mainloop.glib
import gobject

from packagekit.daemonBackend import PACKAGEKIT_DBUS_INTERFACE, PACKAGEKIT_DBUS_PATH, PackageKitBaseBackend, PackagekitProgress, pklog, threaded
from packagekit.enums import *

warnings.filterwarnings(action='ignore', category=FutureWarning)

PACKAGEKIT_DBUS_SERVICE = 'org.freedesktop.PackageKitAptBackend'

XAPIANDBPATH = os.environ.get("AXI_DB_PATH", "/var/lib/apt-xapian-index")
XAPIANDB = XAPIANDBPATH + "/index"
XAPIANDBVALUES = XAPIANDBPATH + "/values"

# Required for daemon mode
os.putenv("PATH",
          "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin")
# Avoid questions from the maintainer scripts as far as possible
os.putenv("DEBIAN_FRONTEND", "noninteractive")
os.putenv("APT_LISTCHANGES_FRONTEND", "none")

# Setup threading support
gobject.threads_init()
dbus.glib.threads_init()

class PackageKitOpProgress(apt.progress.OpProgress):
    '''
    Handle the cache opening process
    '''
    def __init__(self, backend, prange=(0,100), progress=True):
        self._backend = backend
        apt.progress.OpProgress.__init__(self)
        self.steps = []
        for v in [0.12, 0.25, 0.50, 0.75, 1.00]:
            s = prange[0] + (prange[1] - prange[0]) * v
            self.steps.append(s)
        self.pstart = float(prange[0])
        self.pend = self.steps.pop(0)
        self.pprev = None
        self.show_progress = progress

    # OpProgress callbacks
    def update(self, percent):
        progress = int(self.pstart + percent / 100 * (self.pend - self.pstart))
        if self.show_progress == True and self.pprev < progress:
            self._backend.PercentageChanged(progress)
            self.pprev = progress

    def done(self):
        self.pstart = self.pend
        try:
            self.pend = self.steps.pop(0)
        except:
            pklog.warning("An additional step to open the cache is required")

class PackageKitFetchProgress(apt.progress.FetchProgress):
    '''
    Handle the package download process
    '''
    def __init__(self, backend, prange=(0,100)):
        self._backend = backend
        apt.progress.FetchProgress.__init__(self)
        self.pstart = prange[0]
        self.pend = prange[1]
        self.pprev = None

    # FetchProgress callbacks
    def pulse(self):
        if self._backend._canceled.isSet():
            return False
        percent = ((self.currentBytes + self.currentItems)*100.0)/float(self.totalBytes+self.totalItems)
        progress = int(self.pstart + percent/100 * (self.pend - self.pstart))
        if self.pprev < progress:
            self._backend.PercentageChanged(progress)
            self.pprev = progress
        apt.progress.FetchProgress.pulse(self)
        return True

    def start(self):
        self._backend.StatusChanged(STATUS_DOWNLOAD)
        self._backend.AllowCancel(True)

    def stop(self):
        self._backend.PercentageChanged(self.pend)
        self._backend.AllowCancel(False)

    def mediaChange(self, medium, drive):
        #FIXME: use the Message method to notify the user
        self._backend.error(ERROR_INTERNAL_ERROR,
                            "Medium change needed")

class PackageKitInstallProgress(apt.progress.InstallProgress):
    '''
    Handle the installation and removal process. Bits taken from
    DistUpgradeViewNonInteractive.
    '''
    def __init__(self, backend, prange=(0,100)):
        apt.progress.InstallProgress.__init__(self)
        self._backend = backend
        self.timeout = 900
        self.pstart = prange[0]
        self.pend = prange[1]
        self.pprev = None

    def statusChange(self, pkg, percent, status):
        progress = self.pstart + percent/100 * (self.pend - self.pstart)
        if self.pprev < progress:
            self._backend.PercentageChanged(int(progress))
            self.pprev = progress
        pklog.debug("PM status: %s" % status)

    def startUpdate(self):
        self._backend.StatusChanged(STATUS_INSTALL)
        self.last_activity = time.time()

    def updateInterface(self):
        pklog.debug("Updating interface")
        apt.progress.InstallProgress.updateInterface(self)

    def conffile(self, current, new):
        pklog.critical("Config file prompt: '%s'" % current)

def sigquit(signum, frame):
    pklog.error("Was killed")
    sys.exit(1)

class PackageKitAptBackend(PackageKitBaseBackend):
    '''
    PackageKit backend for apt
    '''

    def locked(func):
        '''
        Decorator to run a method with a lock
        '''
        def wrapper(*args, **kwargs):
            backend = args[0]
            backend._lock_cache()
            ret = func(*args, **kwargs)
            backend._unlock_cache()
            return ret
        wrapper.__name__ = func.__name__
        return wrapper

    def __init__(self, bus_name, dbus_path):
        pklog.info("Initializing APT backend")
        signal.signal(signal.SIGQUIT, sigquit)
        self._cache = None
        self._canceled = threading.Event()
        self._canceled.clear()
        self._locked = threading.Lock()
        # Check for xapian support
        self._use_xapian = False
        try:
            import xapian
        except ImportError:
            pass
        else:
            if os.access(XAPIANDB, os.R_OK):
                self._use_xapian = True
        PackageKitBaseBackend.__init__(self, bus_name, dbus_path)

    # Methods ( client -> engine -> backend )

    def doInit(self):
        pklog.info("Initializing cache")
        self.StatusChanged(STATUS_SETUP)
        self.AllowCancel(False)
        self.NoPercentageUpdates()
        self._open_cache(progress=False)

    def doExit(self):
        pass

    @threaded
    def doCancel(self):
        pklog.info("Canceling current action")
        self.StatusChanged(STATUS_CANCEL)
        self._canceled.set()
        self._canceled.wait()

    @threaded
    def doSearchName(self, filters, search):
        '''
        Implement the apt2-search-name functionality
        '''
        pklog.info("Searching for package name: %s" % search)
        self.StatusChanged(STATUS_QUERY)
        self.NoPercentageUpdates()
        self._check_init(progress=False)
        self.AllowCancel(True)

        for pkg in self._cache:
            if self._canceled.isSet():
                self.ErrorCode(ERROR_TRANSACTION_CANCELLED,
                               "The search was canceled")
                self.Finished(EXIT_KILL)
                self._canceled.clear()
                return
            elif search in pkg.name and self._is_package_visible(pkg, filters):
                self._emit_package(pkg)
        self.Finished(EXIT_SUCCESS)

    @threaded
    def doSearchDetails(self, filters, search):
        '''
        Implement the apt2-search-details functionality
        '''
        pklog.info("Searching for package name: %s" % search)
        self.StatusChanged(STATUS_QUERY)
        self.NoPercentageUpdates()
        self._check_init(progress=False)
        self.AllowCancel(True)
        results = []

        if self._use_xapian == True:
            search_flags = (xapian.QueryParser.FLAG_BOOLEAN |
                            xapian.QueryParser.FLAG_PHRASE |
                            xapian.QueryParser.FLAG_LOVEHATE |
                            xapian.QueryParser.FLAG_BOOLEAN_ANY_CASE)
            pklog.debug("Performing xapian db based search")
            db = xapian.Database(XAPIANDB)
            parser = xapian.QueryParser()
            query = parser.parse_query(unicode(search),
                                       search_flags)
            enquire = xapian.Enquire(db)
            enquire.set_query(query)
            matches = enquire.get_mset(0, 1000)
            for r in  map(lambda m: m[xapian.MSET_DOCUMENT].get_data(),
                          enquire.get_mset(0,1000)):
                if self._cache.has_key(r):
                    results.append(self._cache[r])
        else:
            pklog.debug("Performing apt cache based search")
            for p in self._cache._dict.values():
                if self._check_canceled("Search was canceled"): return
                needle = search.strip().lower()
                haystack = p.description.lower()
                if p.name.find(needle) >= 0 or haystack.find(needle) >= 0:
                    results.append(p)

        for r in results:
            if self._check_canceled("Search was canceled"): return
            if self._is_package_visible(r, filters) == True:
                self._emit_package(r)

        self.Finished(EXIT_SUCCESS)

    @threaded
    @locked
    def doGetUpdates(self, filters):
        '''
        Implement the {backend}-get-update functionality
        '''
        #FIXME: Implment the basename filter
        pklog.info("Get updates")
        self.StatusChanged(STATUS_INFO)
        self.AllowCancel(True)
        self.NoPercentageUpdates()
        self._check_init(progress=False)
        self._cache.upgrade(False)
        for pkg in self._cache.getChanges():
            if self._canceled.isSet():
                self.ErrorCode(ERROR_TRANSACTION_CANCELLED,
                               "Calculating updates was canceled")
                self.Finished(EXIT_KILL)
                self._canceled.clear()
                return
            else:
                self._emit_package(pkg)
        self._open_cache(progress=False)
        self.Finished(EXIT_SUCCESS)

    @threaded
    def GetDescription(self, pkg_id):
        '''
        Implement the {backend}-get-description functionality
        '''
        pklog.info("Get description of %s" % pkg_id)
        self.StatusChanged(STATUS_INFO)
        self.NoPercentageUpdates()
        self.AllowCancel(False)
        self._check_init(progress=False)
        name, version, arch, data = self.get_package_from_id(pkg_id)
        if not self._cache.has_key(name):
            self.ErrorCode(ERROR_PACKAGE_NOT_FOUND,
                           "Package %s isn't available" % name)
            self.Finished(EXIT_FAILED)
            return
        pkg = self._cache[name]
        #FIXME: should perhaps go to python-apt since we need this in
        #       several applications
        desc = pkg.description
        # Skip the first line - it's a duplicate of the summary
        i = desc.find('\n')
        desc = desc[i+1:]
        # do some regular expression magic on the description
        # Add a newline before each bullet
        p = re.compile(r'^(\s|\t)*(\*|0|-)',re.MULTILINE)
        desc = p.sub(ur'\n\u2022', desc)
        # replace all newlines by spaces
        p = re.compile(r'\n', re.MULTILINE)
        desc = p.sub(" ", desc)
        # replace all multiple spaces by newlines
        p = re.compile(r'\s\s+', re.MULTILINE)
        desc = p.sub('\n', desc)
        #FIXME: group and licence information missing
        self.Description(pkg_id, 'unknown', 'unknown', desc,
                         pkg.homepage, pkg.packageSize)
        self.Finished(EXIT_SUCCESS)

    @threaded
    @locked
    def doUpdateSystem(self):
        '''
        Implement the {backend}-update-system functionality
        '''
        pklog.info("Upgrading system")
        self.StatusChanged(STATUS_UPDATE)
        self.AllowCancel(False)
        self.PercentageChanged(0)
        self._check_init(prange=(0,5))
        try:
            self._cache.upgrade(distUpgrade=False)
            self._cache.commit(PackageKitFetchProgress(self, prange=(5,50)),
                               PackageKitInstallProgress(self, prange=(50,95)))
        except apt.cache.FetchFailedException:
            self._open_cache()
            self.ErrorCode(ERROR_PACKAGE_DOWNLOAD_FAILED, "Download failed")
            self.Finished(EXIT_FAILED)
            return
        except apt.cache.FetchCancelledException:
            self._open_cache(prange=(95,100))
            self.ErrorCode(ERROR_TRANSACTION_CANCELLED, "Download was canceled")
            self.Finished(EXIT_KILL)
            self._canceled.clear()
            return
        except:
            self._open_cache(prange=(95,100))
            self.ErrorCode(ERROR_INTERNAL_ERROR, "System update failed")
            self.Finished(EXIT_FAILED)
            return
        self.PercentageChanged(100)
        self.Finished(EXIT_SUCCESS)

    @threaded
    @locked
    def doRemovePackage(self, id, deps=True, auto=False):
        '''
        Implement the {backend}-remove functionality
        '''
        pklog.info("Removing package with id %s" % id)
        self.StatusChanged(STATUS_REMOVE)
        self.AllowCancel(False)
        self.PercentageChanged(0)
        self._check_init(prange=(0,10))
        pkg = self._find_package_by_id(id)
        if pkg == None:
            self.ErrorCode(ERROR_PACKAGE_NOT_FOUND,
                           "Package %s isn't available" % pkg.name)
            self.Finished(EXIT_FAILED)
            return
        if not pkg.isInstalled:
            self.ErrorCode(ERROR_PACKAGE_NOT_INSTALLED,
                           "Package %s isn't installed" % pkg.name)
            self.Finished(EXIT_FAILED)
            return
        name = pkg.name[:]
        try:
            pkg.markDelete()
            self._cache.commit(PackageKitFetchProgress(self, prange=(10,10)),
                               PackageKitInstallProgress(self, prange=(10,90)))
        except:
            self._open_cache(prange=(90,100))
            self.ErrorCode(ERROR_INTERNAL_ERROR, "Removal failed")
            self.Finished(EXIT_FAILED)
            return
        self._open_cache(prange=(90,100))
        self.PercentageChanged(100)
        if not self._cache.has_key(name) or not self._cache[name].isInstalled:
            self.Finished(EXIT_SUCCESS)
        else:
            self.ErrorCode(ERROR_INTERNAL_ERROR, "Package is still installed")
            self.Finished(EXIT_FAILED)

    @threaded
    @locked
    def doInstallPackage(self, id):
        '''
        Implement the {backend}-install functionality
        '''
        pklog.info("Installing package with id %s" % id)
        self.StatusChanged(STATUS_INSTALL)
        self.AllowCancel(False)
        self.PercentageChanged(0)
        self._check_init(prange=(0,10))
        pkg = self._find_package_by_id(id)
        if pkg == None:
            self.ErrorCode(ERROR_PACKAGE_NOT_FOUND,
                           "Package %s isn't available" % pkg.name)
            self.Finished(EXIT_FAILED)
            return
        if pkg.isInstalled:
            self.ErrorCode(ERROR_PACKAGE_ALREADY_INSTALLED,
                           "Package %s is already installed" % pkg.name)
            self.Finished(EXIT_FAILED)
            return
        name = pkg.name[:]
        try:
            pkg.markInstall()
            self._cache.commit(PackageKitFetchProgress(self, prange=(10,50)),
                               PackageKitInstallProgress(self, prange=(50,90)))
        except:
            self._open_cache(prange=(90,100))
            self.ErrorCode(ERROR_INTERNAL_ERROR, "Installation failed")
            self.Finished(EXIT_FAILED)
            return
        self._open_cache(prange=(90,100))
        self.PercentageChanged(100)
        if self._cache.has_key(name) and self._cache[name].isInstalled:
            self.Finished(EXIT_SUCCESS)
        else:
            self.ErrorCode(ERROR_INTERNAL_ERROR, "Installation failed")
            self.Finished(EXIT_FAILED)

    @threaded
    @locked
    def doRefreshCache(self, force):
        '''
        Implement the {backend}-refresh_cache functionality
        '''
        pklog.info("Refresh cache")
        self.StatusChanged(STATUS_REFRESH_CACHE)
        self.last_action_time = time.time()
        self.AllowCancel(False);
        self.PercentageChanged(0)
        self._check_init((0,10))
        try:
            self._cache.update(PackageKitFetchProgress(self, prange=(10,95)))
        except apt.cache.FetchFailedException:
            self.ErrorCode(ERROR_NO_NETWORK, "Download failed")
            self.Finished(EXIT_FAILED)
            return
        except apt.cache.FetchCancelledException:
            self._canceled.clear()
            self.ErrorCode(ERROR_TRANSACTION_CANCELLED, "Download was canceled")
            self.Finished(EXIT_KILL)
            return
        except:
            self._open_cache(prange=(95,100))
            self.ErrorCode(ERROR_INTERNAL_ERROR, "Refreshing cache failed")
            self.Finished(EXIT_FAILED)
            return
        self.PercentageChanged(100)
        self.Finished(EXIT_SUCCESS)

    # Helpers

    def _open_cache(self, prange=(0,100), progress=True):
        '''
        (Re)Open the APT cache
        '''
        pklog.debug("Open APT cache")
        self.StatusChanged(STATUS_REFRESH_CACHE)
        try:
            self._cache = apt.Cache(PackageKitOpProgress(self, prange,
                                                         progress))
        except:
            self.ErrorCode(ERROR_NO_CACHE, "Package cache could not be opened")
            self.Finished(EXIT_FAILED)
            self.Exit()
            return
        if self._cache._depcache.BrokenCount > 0:
            self.ErrorCode(ERROR_INTERNAL_ERROR,
                           "Not all dependecies can be satisfied")
            self.Finished(EXIT_FAILED)
            self.Exit()
            return

    def _lock_cache(self):
        '''
        Lock the cache
        '''
        pklog.debug("Locking cache")
        self._locked.acquire()

    def _unlock_cache(self):
        '''
        Unlock the cache
        '''
        pklog.debug("Releasing cache")
        self._locked.release()

    def _check_init(self, prange=(0,10), progress=True):
        '''
        Check if the backend was initialized well and try to recover from
        a broken setup
        '''
        pklog.debug("Check apt cache and xapian database")
        if not isinstance(self._cache, apt.cache.Cache) or \
           self._cache._depcache.BrokenCount > 0:
            self._open_cache(prange, progress)

    def _check_canceled(self, msg):
        '''
        Check if the current transaction was canceled. If so send the
        corresponding error message and return True
        '''
        if self._canceled.isSet():
             self.ErrorCode(ERROR_TRANSACTION_CANCELLED, msg)
             self.Finished(EXIT_KILL)
             self._canceled.clear()
             return True
        return False
 
    def get_id_from_package(self, pkg, installed=False):
        '''
        Return the id of the installation candidate of a core
        apt package. If installed is set to True the id of the currently
        installed package will be returned.
        '''
        origin = ''
        if installed == False and pkg.isInstalled:
            pkgver = pkg.installedVersion
        else:
            pkgver = pkg.candidateVersion
            if pkg.candidateOrigin:
                origin = pkg.candidateOrigin[0].label
        id = self._get_package_id(pkg.name, pkgver, pkg.architecture, origin)
        return id

    def _emit_package(self, pkg):
        '''
        Send the Package signal for a given apt package
        '''
        id = self.get_id_from_package(pkg)
        if pkg.isInstalled:
            status = INFO_INSTALLED
        else:
            status = INFO_AVAILABLE
        summary = pkg.summary
        self.Package(status, id, summary)

    def _is_package_visible(self, pkg, filters):
        '''
        Return True if the package should be shown in the user interface
        '''
        #FIXME: Needs to be optmized
        if filters == 'none':
            return True
        if FILTER_INSTALLED in filters and not pkg.isInstalled:
            return False
        if FILTER_NOT_INSTALLED in filters and pkg.isInstalled:
            return False
        if FILTER_GUI in filters and not self._package_has_gui(pkg):
            return False
        if FILTER_NOT_GUI in filters and self._package_has_gui(pkg):
            return False
        if FILTER_DEVELOPMENT in filters and not self._package_is_devel(pkg):
            return False
        if FILTER_NOT_DEVELOPMENT in filters and self._package_is_devel(pkg):
            return False
        return True

    def _package_has_gui(self, pkg):
        #FIXME: should go to a modified Package class
        #FIXME: take application data into account. perhaps checking for
        #       property in the xapian database
        return pkg.section.split('/')[-1].lower() in ['x11', 'gnome', 'kde']

    def _package_is_devel(self, pkg):
        #FIXME: should go to a modified Package class
        return pkg.name.endswith("-dev") or pkg.name.endswith("-dbg") or \
               pkg.section.split('/')[-1].lower() in ['devel', 'libdevel']

    def _find_package_by_id(self, id):
        '''
        Return a package matching to the given package id
        '''
        # FIXME: Perform more checks
        name, version, arch, data = self.get_package_from_id(id)
        if self._cache.has_key(name):
            return self._cache[name]
        else:
            return None


def main():
    loop = dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus(mainloop=loop)
    bus_name = dbus.service.BusName(PACKAGEKIT_DBUS_SERVICE, bus=bus)
    manager = PackageKitAptBackend(bus_name, PACKAGEKIT_DBUS_PATH)

if __name__ == '__main__':
    main()

# vim: ts=4 et sts=4
