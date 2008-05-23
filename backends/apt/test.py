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

PACKAGEKIT_DBUS_SERVICE = 'org.freedesktop.PackageKitAptBackend'
PACKAGEKIT_DBUS_INTERFACE = 'org.freedesktop.PackageKitBackend'
PACKAGEKIT_DBUS_PATH = '/org/freedesktop/PackageKitBackend'
PKG_ID = 'xterm;232-1;i386;Debian'

try:
    bus = dbus.SystemBus()
except dbus.DBusException, e:
    print  "Unable to connect to dbus"
    print "%s" %(e,)
    sys.exit(1)

try:
    proxy = bus.get_object(PACKAGEKIT_DBUS_SERVICE, PACKAGEKIT_DBUS_PATH)
    iface = dbus.Interface(proxy, PACKAGEKIT_DBUS_INTERFACE)
    cmd = sys.argv[1]
    if cmd == 'init' or cmd == 'all':
        print "Testing Init()"
        iface.Init()
    if cmd == 'cancel':
        print "Canceling"
        iface.Cancel()
    if cmd == 'get-updates' or cmd == 'all':
        print "Testing GetUpdate()"
        iface.GetUpdates()
    if cmd == 'search-name' or cmd == 'all':
        print "Testing SearchName(FILTER_NONE,'apt')"
        iface.SearchName(FILTER_NONE,'apt')
    if cmd == 'search-details' or cmd == 'all':
        print "SearchDetails(FILTER_NONE,'dbus')"
        iface.SearchDetails(FILTER_NONE,'dbus')
    if cmd == 'search-group' or cmd == 'all':
        print "Testing SearchGroup(FILTER_NONE,GROUP_GAMES)"
        iface.SearchGroup(FILTER_NONE,GROUP_GAMES)
    if cmd == 'search-file' or cmd == 'all':
        print "Testing SearchFile(FILTER_NONE,'/usr/bin/yum')"
        iface.SearchFile(FILTER_NONE,'/usr/bin/yum')
    if cmd == 'get-requires' or cmd == 'all':
        print "Testing GetRequires(PKG_ID,False)"
        iface.GetRequires(PKG_ID,False)
    if cmd == 'get-depends' or cmd == 'all':
        print "Testing GetDepends(PKG_ID,False)"
        iface.GetDepends(PKG_ID,False)
    if cmd == 'refresh-cache' or cmd == 'all':
        print "Testing RefreshCache()"
        iface.RefreshCache()
    if cmd == 'resolve' or cmd == 'all':
        print "Testing Resolve(FILTER_NONE,'yum')"
        iface.Resolve(FILTER_NONE,'yum')
    if cmd == 'get-details' or cmd == 'all':
        print "Testing GetDetails(PKG_ID)"
        iface.GetDetails(PKG_ID)
    if cmd == 'get-files' or cmd == 'all':
        print "Testing GetFiles(PKG_ID)"
        iface.GetFiles(PKG_ID)
    if cmd == 'get-packages' or cmd == 'all':
        print "Testing GetPackages(FILTER_INSTALLED,'no')"
        iface.GetPackages(FILTER_INSTALLED,'no')
    if cmd == 'get-repolist' or cmd == 'all':
        print "Testing GetRepoList()"
        iface.GetRepoList()
    if cmd == 'get-updatedetail' or cmd == 'all':
        print "Testing GetUpdateDetail(PKG_ID)"
        iface.GetUpdateDetail(PKG_ID)
    #print "Testing "
    #iface.
    if cmd == 'exit' or cmd == 'all':
        print "Testing Exit()"
        iface.Exit()
    
except dbus.DBusException, e:
    print "Unable to send message on dbus"
    print "%s" %(e,)
    sys.exit(1)
