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

PACKAGEKIT_DBUS_INTERFACE = 'org.freedesktop.PackageKitTestBackend'
PACKAGEKIT_DBUS_SERVICE = 'org.freedesktop.PackageKitTestBackend'
PACKAGEKIT_DBUS_PATH = '/org/freedesktop/PackageKitTestBackend'

#sudo dbus-send --system --dest=org.freedesktop.PackageKitTestBackend --type=method_call --print-reply /org/freedesktop/PackageKitTestBackend org.freedesktop.PackageKitTestBackend.SearchName string:filter string:search

class PackageKitTestBackendService(dbus.service.Object):
    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Init(self):
        print 'Init!'

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Lock(self):
        print 'Lock!'

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Unlock(self):
        print 'Unlock!'

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Exit(self):
        sys.exit(0)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ss', out_signature='')
    def SearchName(self, filterx, search):
        print 'filter'
        self.Finished(0)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='u')
    def Finished(self, exit):
        print "Finished (%d)" % (exit)

bus = dbus.SystemBus()
bus_name = dbus.service.BusName(PACKAGEKIT_DBUS_SERVICE, bus=bus)
manager = PackageKitTestBackendService(bus_name, PACKAGEKIT_DBUS_PATH)

mainloop = gobject.MainLoop()
mainloop.run()

