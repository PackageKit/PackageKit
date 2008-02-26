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

import logging
import os
import pty
import re
import time
import warnings

import apt
import dbus
import dbus.service
import dbus.mainloop.glib
import xapian

from packagekit.daemonBackend import PACKAGEKIT_DBUS_INTERFACE, PACKAGEKIT_DBUS_PATH, PackageKitBaseBackend, PackagekitProgress
from packagekit.enums import *

logging.basicConfig()
log = logging.getLogger("aptDBUSBackend")
log.setLevel(logging.DEBUG)

warnings.filterwarnings(action='ignore', category=FutureWarning)

PACKAGEKIT_DBUS_SERVICE = 'org.freedesktop.PackageKitAptBackend'

XAPIANDBPATH = os.environ.get("AXI_DB_PATH", "/var/lib/apt-xapian-index")
XAPIANDB = XAPIANDBPATH + "/index"
XAPIANDBVALUES = XAPIANDBPATH + "/values"
DEFAULT_SEARCH_FLAGS = (xapian.QueryParser.FLAG_BOOLEAN |
                        xapian.QueryParser.FLAG_PHRASE |
                        xapian.QueryParser.FLAG_LOVEHATE |
                        xapian.QueryParser.FLAG_BOOLEAN_ANY_CASE)

# Avoid questions from the maintainer scripts as far as possible
os.putenv("DEBIAN_FRONTEND", "noninteractive")
os.putenv("APT_LISTCHANGES_FRONTEND", "none")

class PackageKitOpProgress(apt.progress.OpProgress):
    '''
    Handle the cache opening process
    '''
    def __init__(self, backend):
        self._backend = backend
        apt.progress.OpProgress.__init__(self)

    # OpProgress callbacks
    def update(self, percent):
        self._backend.PercentageChanged(int(percent))

    def done(self):
        self._backend.PercentageChanged(100)

class PackageKitFetchProgress(apt.progress.FetchProgress):
    '''
    Handle the package download process
    '''
    def __init__(self, backend):
        self._backend = backend
        apt.progress.FetchProgress.__init__(self)
    # FetchProgress callbacks
    def pulse(self):
        self._backend.StatusChanged(STATUS_DOWNLOAD)
        percent = ((self.currentBytes + self.currentItems)*100.0)/float(self.totalBytes+self.totalItems)
        self._backend.PercentageChanged(int(percent))
        apt.progress.FetchProgress.pulse(self)
        return True

    def stop(self):
        self._backend.PercentageChanged(100)

    def mediaChange(self, medium, drive):
        #FIXME: use the Message method to notify the user
        self._backend.error(ERROR_INTERNAL_ERROR,
                            "Medium change needed")

class PackageKitInstallProgress(apt.progress.InstallProgress):
    '''
    Handle the installation and removal process. Bits taken from
    DistUpgradeViewNonInteractive.
    '''
    def __init__(self, backend):
        apt.progress.InstallProgress.__init__(self)
        self._backend = backend
        self.timeout = 900

    def statusChange(self, pkg, percent, status):
        self._backend.PercentageChanged(int(percent))
        #FIXME: should represent the status better (install, remove, preparing)
        self._backend.StatusChanged(STATUS_INSTALL)
        if (self.last_activity + self.timeout) < time.time():
            logging.critical("Sending Crtl+C. Inactivity of %s "
                             "seconds (%s)" % (self.timeout, self.status))
            os.write(self.master_fd,chr(3))

    def startUpdate(self):
        self.last_activity = time.time()

    def updateInterface(self):
        apt.progress.InstallProgress.updateInterface(self)

    def fork(self):
        logging.debug("doing a pty.fork()")
        (self.pid, self.master_fd) = pty.fork()
        if self.pid != 0:
            logging.debug("pid is: %s" % self.pid)
        return self.pid

    def conffile(self, current, new):
        logging.warning("Config file prompt: '%s'" % current)
        # looks like we have a race here *sometimes*
        time.sleep(5)
        try:
            # don't overwrite
            os.write(self.master_fd,"n\n")
        except Exception, e:
            logging.error(e)


class PackageKitAptBackend(PackageKitBaseBackend):
    '''
    PackageKit backend for apt
    '''
    def __init__(self, bus_name, dbus_path):
        log.info("Initializing backend")
        PackageKitBaseBackend.__init__(self, bus_name, dbus_path)
        self._cache = None
        self._xapian = None


    # Methods ( client -> engine -> backend )

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Init(self):
        self.last_action_time = time.time()
        log.info("Initializing cache")
        self.StatusChanged(STATUS_SETUP)
        self._open_cache()
        self._xapian = xapian.Database(XAPIANDB)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Exit(self):
        self.loop.quit()

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ss', out_signature='')
    def SearchName(self, filters, search):
        '''
        Implement the apt2-search-name functionality
        '''
        log.info("Searching for package name: %s" % search)
        self.last_action_time = time.time()
        self.AllowCancel(True)
        self.NoPercentageUpdates()

        self.StatusChanged(STATUS_QUERY)

        for pkg in self._cache:
            if search in pkg.name and self._is_package_visible(pkg, filters):
                self._emit_package(pkg)
        self.Finished(EXIT_SUCCESS)


    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ss', out_signature='')
    def SearchDetails(self, filters, search):
        '''
        Implement the apt2-search-details functionality
        '''
        log.info("Searching for package name: %s" % search)
        self.last_action_time = time.time()
        self.AllowCancel(True)
        self.NoPercentageUpdates()
        self.StatusChanged(STATUS_QUERY)

        self._xapian.reopen()
        parser = xapian.QueryParser()
        query = parser.parse_query(unicode(search),
                                   DEFAULT_SEARCH_FLAGS)
        enquire = xapian.Enquire(self._xapian)
        enquire.set_query(query)
        matches = enquire.get_mset(0, 1000)
        for m in matches:
            name = m[xapian.MSET_DOCUMENT].get_data()
            if self._cache.has_key(name):
                pkg = self._cache[name]
                if self._is_package_visible(pkg, filters) == True:
                    self._emit_package(pkg)

        self.Finished(EXIT_SUCCESS)


    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def GetUpdates(self):
        '''
        Implement the {backend}-get-update functionality
        '''
        self.last_action_time = time.time()
        self.AllowCancel(True)
        self.NoPercentageUpdates()
        self.StatusChanged(STATUS_INFO)
        self._cache.upgrade(False)
        for pkg in self._cache.getChanges():
            self._emit_package(pkg)
        self.Finished(EXIT_SUCCESS)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='s', out_signature='')
    def GetDescription(self, pkg_id):
        '''
        Implement the {backend}-get-description functionality
        '''
        self.last_action_time = time.time()
        self.AllowCancel(True)
        self.NoPercentageUpdates()
        self.StatusChanged(STATUS_INFO)
        name, version, arch, data = self.get_package_from_id(pkg_id)
        #FIXME: error handling
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
        # Get the homepage of the package
        # FIXME: switch to the new unreleased API
        if pkg.candidateRecord.has_key('Homepage'):
            homepage = pkg.candidateRecord['Homepage']
        else:
            homepage = ''
        #FIXME: group and licence information missing
        self.Description(pkg_id, 'unknown', 'unknown', desc,
                         homepage, pkg.packageSize)
        self.Finished(EXIT_SUCCESS)


    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Unlock(self):
        self.last_action_time = time.time()
        self.doUnlock()

    def doUnlock(self):
        if self.isLocked():
            self._locked = False

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Lock(self):
        self.last_action_time = time.time()
        self.doLock()

    def doLock(self):
        self._locked = True

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def UpdateSystem(self):
        '''
        Implement the {backend}-update-system functionality
        '''
        #FIXME: Better exception and error handling
        #FIXME: Distupgrade or Upgrade?
        #FIXME: Handle progress in a more sane way
        log.info("Upgrading system")
        self.last_action_time = time.time()
        self.StatusChanged(STATUS_UPDATE)
        self.AllowCancel(False)
        self.PercentageChanged(0)
        try:
            self._cache.upgrade(distUpgrade=True)
            self._cache.commit(PackageKitFetchProgress(self),
                               PackageKitInstallProgress(self))
        except:
            self.ErrorCode(ERROR_INTERNAL_ERROR, "Upgrade failed")
            self.Finished(EXIT_FAILED)
            self._open_cache()
            return
        self._open_cache()
        self.Finished(EXIT_SUCCESS)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='sb', out_signature='')
    def RemovePackage(self, id, deps=True):
        '''
        Implement the {backend}-remove functionality
        '''
        #FIXME: Better exception and error handling
        #FIXME: Handle progress in a more sane way
        log.info("Removing package with id %s" % id)
        self.last_action_time = time.time()
        self.StatusChanged(STATUS_REMOVE)
        self.AllowCancel(False)
        self.PercentageChanged(0)
        pkg = self._find_package_by_id(id)
        name = pkg.name[:]
        try:
            pkg.markDelete()
            self._cache.commit(PackageKitFetchProgress(self),
                               PackageKitInstallProgress(self))
        except:
            self.ErrorCode(ERROR_INTERNAL_ERROR, "Removal failed")
            self.Finished(EXIT_FAILED)
            self._open_cache()
            return
        # FIXME: handle error
        self._open_cache()
        if not self._cache.has_key(name) or not self._cache[name].isInstalled:
            self.Finished(EXIT_SUCCESS)
        else:
            self.ErrorCode(ERROR_INTERNAL_ERROR, "Removal failed")
            self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='s', out_signature='')
    def InstallPackage(self, id):
        '''
        Implement the {backend}-install functionality
        '''
        #FIXME: Exception and error handling
        #FIXME: Handle progress in a more sane way
        log.info("Installing package with id %s" % id)
        self.last_action_time = time.time()
        self.StatusChanged(STATUS_INSTALL)
        self.PercentageChanged(0)
        self.AllowCancel(False)
        pkg = self._find_package_by_id(id)
        name = pkg.name[:]
        try:
            pkg.markInstall()
            self._cache.commit(PackageKitFetchProgress(self),
                               PackageKitInstallProgress(self))
        except:
            self.ErrorCode(ERROR_INTERNAL_ERROR, "Installation failed")
            self.Finished(EXIT_FAILED)
            self._open_cache()
            return
        self._open_cache()
        if self._cache.has_key(name) and self._cache[name].isInstalled:
            self.Finished(EXIT_SUCCESS)
        else:
            self.ErrorCode(ERROR_INTERNAL_ERROR, "Installation failed")
            self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='b', out_signature='')
    def RefreshCache(self, force):
        '''
        Implement the {backend}-refresh_cache functionality
        '''
        self.last_action_time = time.time()
        self.AllowCancel(True);
        self.PercentageChanged(0)
        self.StatusChanged(STATUS_REFRESH_CACHE)
        try:
            self._cache.update(PackageKitFetchProgress(self))
        except:
            self._open_cache()
            self.ErrorCode(ERROR_NO_CACHE,
                           "Package cache could not be opened")
            self.Finished(EXIT_FAILED)
            return
        self._open_cache()
        self.Finished(EXIT_SUCCESS)

    # Helpers

    def _open_cache(self):
        '''
        (Re)Open the APT cache
        '''
        self.StatusChanged(STATUS_REFRESH_CACHE)
        self.doLock()
        try:
            self._cache = apt.Cache(PackageKitOpProgress(self))
        except:
            self.ErrorCode(ERROR_NO_CACHE, "Package cache could not be opened")
            self.Finished(EXIT_FAILED)
            self.doUnlock()
            self.Exit()
            return
        self.doUnlock()
        if self._cache._depcache.BrokenCount > 0:
            self.ErrorCode(ERROR_INTERNAL_ERROR,
                           "Not all dependecies can be satisfied")
            self.Finished(EXIT_FAILED)
            self.Exit()
            return

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
