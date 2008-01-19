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

PACKAGEKIT_DBUS_INTERFACE = 'org.freedesktop.PackageKitYum'
PACKAGEKIT_DBUS_SERVICE = 'org.freedesktop.PackageKitYum'
PACKAGEKIT_DBUS_PATH = '/org/freedesktop/PackageKitYum'

#sudo dbus-send --system --dest=org.freedesktop.PackageKitYum --type=method_call --print-reply /org/freedesktop/PackageKitYum org.freedesktop.PackageKitYum.SearchName string:filter string:search

class PackageKitYumService(dbus.service.Object):
    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='i', out_signature='')
    def exit(self, source):
        if source != 1:
            print 'foo'
            return
        print 'bar'

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ss', out_signature='')
    def SearchName(self, filterx, search):
        print 'filter'

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
        in_signature='', out_signature='i')
    def get_display_brightness(self):
        return 666

bus = dbus.SystemBus()
bus_name = dbus.service.BusName(PACKAGEKIT_DBUS_SERVICE, bus=bus)
manager = PackageKitYumService(bus_name, PACKAGEKIT_DBUS_PATH)

mainloop = gobject.MainLoop()
mainloop.run()

