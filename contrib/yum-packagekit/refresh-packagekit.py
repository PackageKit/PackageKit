# A plugin for yum which notifies PackageKit to refresh its data
#
# Copyright (c) 2007 James Bowes <jbowes@redhat.com>
# Copyright (c) 2012 Elad Alfassa <elad@fedoraproject.org>
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
# version 0.0.5

import dbus
from yum.plugins import TYPE_CORE

requires_api_version = '2.5'
plugin_type = TYPE_CORE

def posttrans_hook(conduit):
    """
    Tell PackageKit to refresh its state. Run only after an rpm transaction.
    """
    try:
        bus = dbus.SystemBus()
    except dbus.DBusException, e:
        conduit.info(2, "Unable to connect to dbus")
        conduit.info(6, "%s" %(e,))
        return
    try:
        packagekit_proxy = bus.get_object('org.freedesktop.PackageKit',
                                          '/org/freedesktop/PackageKit')
        packagekit_iface = dbus.Interface(packagekit_proxy, 'org.freedesktop.PackageKit')
        packagekit_iface.StateHasChanged('posttrans')
    except Exception, e:
        conduit.info(2, "Unable to send message to PackageKit")
        conduit.info(6, "%s" %(e,))

def init_hook(conduit):
    """
    Tell PackageKit to exit when we start yum, so it won't block command-line operations.
    """
    try:
        bus = dbus.SystemBus()
    except dbus.DBusException, e:
        conduit.info(2, "Unable to connect to dbus")
        conduit.info(6, "%s" %(e,))
        return
    try:
        packagekit_proxy = bus.get_object('org.freedesktop.PackageKit',
                                          '/org/freedesktop/PackageKit')
        packagekit_iface = dbus.Interface(packagekit_proxy, 'org.freedesktop.PackageKit')
        packagekit_iface.SuggestDaemonQuit()
    except Exception, e:
        conduit.info(2, "Unable to send message to PackageKit")
        conduit.info(6, "%s" %(e,))

