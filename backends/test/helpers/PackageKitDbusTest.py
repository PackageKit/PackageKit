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

PACKAGEKIT_DBUS_INTERFACE = 'org.freedesktop.PackageKitDbus'
PACKAGEKIT_DBUS_SERVICE = 'org.freedesktop.PackageKitDbus'
PACKAGEKIT_DBUS_PATH = '/org/freedesktop/PackageKitDbus'

#sudo dbus-send --system --dest=org.freedesktop.PackageKitDbus --type=method_call --print-reply /org/freedesktop/PackageKitDbus org.freedesktop.PackageKitDbus.SearchName string:filter string:search

class PackageKitDbusService(dbus.service.Object):
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
                         signature='i')
    def Finished(self, exit):
        print "%d exit status" % (exit)

bus = dbus.SystemBus()
bus_name = dbus.service.BusName(PACKAGEKIT_DBUS_SERVICE, bus=bus)
manager = PackageKitDbusService(bus_name, PACKAGEKIT_DBUS_PATH)

mainloop = gobject.MainLoop()
mainloop.run()

