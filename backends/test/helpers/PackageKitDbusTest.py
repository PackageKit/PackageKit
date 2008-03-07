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
import threading

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
from packagekit.daemonBackend import pklog

PACKAGEKIT_DBUS_SERVICE = 'org.freedesktop.PackageKitTestBackend'

#sudo dbus-send --system --dest=org.freedesktop.PackageKitTestBackend --type=method_call --print-reply /org/freedesktop/PackageKitBackend org.freedesktop.PackageKitBackend.SearchName string:filter string:search

# Setup threading support
gobject.threads_init()
dbus.glib.threads_init()

def sigquit(signum, frame):
    print >> sys.stderr, "Quit signal sent - exiting immediately"

    sys.exit(1)

class PackageKitTestBackendService(PackageKitBaseBackend):
    def threaded(func):
        '''
        Decorator to run a method in a separate thread
        '''
        def wrapper(*args, **kwargs):
            thread = threading.Thread(target=func, args=args, kwargs=kwargs)
            thread.setDaemon(True)
            thread.start()
        wrapper.__name__ = func.__name__
        return wrapper

    def __init__(self, bus_name, bus_path):
        signal.signal(signal.SIGQUIT, sigquit)

        self.bus_name = bus_name
        self.bus_path = bus_path
        self._canceled = threading.Event()
        PackageKitBaseBackend.__init__(self, bus_name, bus_path)

    @threaded
    def doInit(self):
        pklog.info( 'Init!')
        time.sleep(0.1)

    def doExit(self):
        pklog.info('Exit!')
        sys.exit(0)

    @threaded
    def doCancel(self):
        pklog.info('Cancel!')
        self.StatusChanged(STATUS_CANCEL)
        self._canceled.set()
        self._canceled.wait()
        pklog.debug('Cancel was successful!')

    @threaded
    def doSearchName(self, filters, search):
        pklog.info("SearchName (%s, %s)" % (filters, search))
        self.AllowCancel(True)
        self.StatusChanged(STATUS_QUERY)
        for id,desc in [("foo;0.0.1;i398;fedora", "Foo"),
                        ("foo-doc;0.0.1;i398;fedora", "Foo documentation"),
                        ("foo-devel;0.0.1;i398;fedora", "Foo build files")]:
            if self._canceled.isSet():
                self.ErrorCode(ERROR_TRANSACTION_CANCELLED,
                               "Search was canceled")
                self.Finished(EXIT_KILL)
                self._canceled.clear()
                return
            time.sleep(1)
            self.Package(INFO_AVAILABLE, id, desc)
        self.Finished(EXIT_SUCCESS)
        self.AllowCancel(False)

def main():
    bus = dbus.SystemBus()
    bus_name = dbus.service.BusName(PACKAGEKIT_DBUS_SERVICE, bus=bus)
    manager = PackageKitTestBackendService(bus_name, PACKAGEKIT_DBUS_PATH)

    mainloop = gobject.MainLoop()
    mainloop.run()

if __name__ == "__main__":
    main()
