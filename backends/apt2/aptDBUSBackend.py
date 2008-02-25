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

import warnings
warnings.filterwarnings(action='ignore', category=FutureWarning)

import apt

import dbus
import dbus.service
import dbus.mainloop.glib

from packagekit.daemonBackend import PACKAGEKIT_DBUS_INTERFACE, PACKAGEKIT_DBUS_PATH, PackageKitBaseBackend, PackagekitProgress
from packagekit.enums import *

PACKAGEKIT_DBUS_SERVICE = 'org.freedesktop.PackageKitAptBackend'

class PackageKitOpProgress(apt.progress.OpProgress):
    def __init__(self, backend):
        self._backend = backend
        apt.progress.OpProgress.__init__(self)

    # OpProgress callbacks
    def update(self, percent):
        self._backend.PercentageChanged(int(percent))

    def done(self):
        self._backend.PercentageChanged(100)

class PackageKitFetchProgress(apt.progress.FetchProgress):
    def __init__(self, backend):
        self._backend = backend
        apt.progress.FetchProgress.__init__(self)
    # FetchProgress callbacks
    def pulse(self):
        apt.progress.FetchProgress.pulse(self)
        self._backend.percentage(self.percent)
        return True

    def stop(self):
        self._backend.percentage(100)

    def mediaChange(self, medium, drive):
        #FIXME: use the Message method to notify the user
        self._backend.error(ERROR_INTERNAL_ERROR,
                "Medium change needed")

class PackageKitInstallProgress(apt.progress.InstallProgress):
    def __init__(self, backend):
        apt.progress.InstallProgress.__init__(self)

class PackageKitAptBackend(PackageKitBaseBackend):
    def __init__(self, bus_name, dbus_path):
        PackageKitBaseBackend.__init__(self, bus_name, dbus_path)
        self._cache = None

    # Methods ( client -> engine -> backend )

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Init(self):
        self._cache = apt.Cache(PackageKitProgress(self))

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Exit(self):
        self.loop.quit()

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ss', out_signature='')
    def SearchName(self, filters, search):
        '''
        Implement the {backend}-search-name functionality
        '''
        self.AllowCancel(True)
        self.NoPercentageUpdates()

        self.StatusChanged(STATUS_QUERY)

        for pkg in self._cache:
            if search in pkg.name:
                self._emit_package(pkg)
        self.Finished(EXIT_SUCCESS)


    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def GetUpdates(self):
        '''
        Implement the {backend}-get-update functionality
        '''
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
        desc = p.sub('\n*', desc)
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
        self.doUnlock()

    def doUnlock(self):
        if self.isLocked():
            PackageKitBaseBackend.doUnlock(self)


    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Lock(self):
        self.doLock()

    def doLock(self):
        pass

    #
    # Helpers
    #
    def get_id_from_package(self, pkg, installed=False):
        '''
        Returns the id of the installation candidate of a core
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

    def _emit_package(self, pkg, installed=False):
        '''
        Send the Package signal for a given apt package
        '''
        id = self.get_id_from_package(pkg, installed)
        if installed and pkg.isInstalled:
            status = INFO_INSTALLED
        else:
            status = INFO_AVAILABLE
        summary = pkg.summary
        self.Package(status, id, summary)

if __name__ == '__main__':
    loop = dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus(mainloop=loop)
    bus_name = dbus.service.BusName(PACKAGEKIT_DBUS_SERVICE, bus=bus)
    manager = PackageKitAptBackend(bus_name, PACKAGEKIT_DBUS_PATH)

# vim: ts=4 et sts=4
