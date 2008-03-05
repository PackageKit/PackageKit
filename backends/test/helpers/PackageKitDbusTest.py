#!/usr/bin/python

# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import os
import sys

import dbus
import dbus.glib
import dbus.service
import gobject
from packagekit.enums import *
import time
import signal

# This is common between backends
from packagekit.daemonBackend import PACKAGEKIT_DBUS_INTERFACE, PACKAGEKIT_DBUS_PATH
from packagekit.daemonBackend import PackageKitBaseBackend

PACKAGEKIT_DBUS_SERVICE = 'org.freedesktop.PackageKitTestBackend'

#sudo dbus-send --system --dest=org.freedesktop.PackageKitTestBackend --type=method_call --print-reply /org/freedesktop/PackageKitBackend org.freedesktop.PackageKitBackend.SearchName string:filter string:search

def sigquit(signum, frame):
    print >> sys.stderr, "Quit signal sent - exiting immediately"

    sys.exit(1)

class PackageKitTestBackendService(PackageKitBaseBackend):

    def __init__(self, bus_name, bus_path):
        signal.signal(signal.SIGQUIT, sigquit)

        self.bus_name = bus_name
        self.bus_path = bus_path
        PackageKitBaseBackend.__init__(self, bus_name, bus_path)

    def doInit(self):
        print 'Init!'
        time.sleep(0.1)

    def doLock(self):
        print 'Lock!'

    def doUnlock(self):
        print 'Unlock!'

    def doExit(self):
        sys.exit(0)

    def doSearchName(self, filters, search):
        print "SearchName (%s, %s)" % (filters, search)
        self.AllowCancel(True)
        self.StatusChanged(STATUS_QUERY)
        time.sleep(1)
        self.Package(INFO_AVAILABLE, "foo;0.0.1;i398;fedora", "Foo")
        time.sleep(1)
        self.Package(INFO_AVAILABLE, "foo-devel;0.0.1;i398;fedora", "Foo build files")
        self.Finished(EXIT_SUCCESS)


bus = dbus.SystemBus()
bus_name = dbus.service.BusName(PACKAGEKIT_DBUS_SERVICE, bus=bus)
manager = PackageKitTestBackendService(bus_name, PACKAGEKIT_DBUS_PATH)

mainloop = gobject.MainLoop()
mainloop.run()

