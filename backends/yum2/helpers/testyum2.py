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
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

# Copyright (C) 2007
#    Tim Lauridsen <timlau@fedoraproject.org>

import sys
import dbus
from packagekit.enums import *

PACKAGEKIT_DBUS_SERVICE = 'org.freedesktop.PackageKitYumBackend'
PACKAGEKIT_DBUS_INTERFACE = 'org.freedesktop.PackageKitBackend'
PACKAGEKIT_DBUS_PATH = '/org/freedesktop/PackageKitBackend'
PKG_ID = 'supertux;0.3.0-3.fc8;x86_64;updates'

try:
    bus = dbus.SystemBus()
except dbus.DBusException, e:
    print  "Unable to connect to dbus"
    print "%s" %(e,)
    sys.exit(1)

try:
    proxy = bus.get_object(PACKAGEKIT_DBUS_SERVICE, PACKAGEKIT_DBUS_PATH)
    iface = dbus.Interface(proxy, PACKAGEKIT_DBUS_INTERFACE)
    print "Testing Init()"
    iface.Init()
    print "Testing GetUpdate()"
    iface.GetUpdates()
    print "Testing SearchName(FILTER_NONE,'yum')"
    iface.SearchName(FILTER_NONE,'yum')
    # print "SearchDetails(FILTER_NONE,'dbus')"
    # This one is failing because of some  UnicodeDecodeError in yum 
    #iface.SearchDetails(FILTER_NONE,'DBus')
    print "Testing SearchGroup(FILTER_NONE,GROUP_GAMES)"
    iface.SearchGroup(FILTER_NONE,GROUP_GAMES)
    print "Testing SearchFile(FILTER_NONE,'/usr/bin/yum')"
    iface.SearchFile(FILTER_NONE,'/usr/bin/yum')
    print "Testing GetRequires(PKG_ID,False)"
    iface.GetRequires(PKG_ID,False)
    print "Testing GetDepends(PKG_ID,False)"
    iface.GetDepends(PKG_ID,False)
    print "Testing RefreshCache()"
    iface.RefreshCache()
    print "Testing Resolve(FILTER_NONE,'yum')"
    iface.Resolve(FILTER_NONE,'yum')
    print "Testing GetDescription(PKG_ID)"
    iface.GetDescription(PKG_ID)
    print "Testing GetFiles(PKG_ID)"
    iface.GetFiles(PKG_ID)
    print "Testing GetPackages(FILTER_INSTALLED,'no')"
    iface.GetPackages(FILTER_INSTALLED,'no')
    print "Testing GetRepoList()"
    iface.GetRepoList()
    print "Testing GetUpdateDetail(PKG_ID)"
    iface.GetUpdateDetail(PKG_ID)
    #print "Testing "
    #iface.
    print "Testing RefreshCache()"
    iface.RefreshCache()
    print "Testing Exit()"
    iface.Exit()
    
except dbus.DBusException, e:
    print "Unable to send message on dbus"
    print "%s" %(e,)
    sys.exit(1)
