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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Copyright (C) 2007
#    Tim Lauridsen <timlau@fedoraproject.org>
#    Tom Parker <palfrey@tevp.net>
#    Robin Norwood <rnorwood@redhat.com>

# Handle the dbus bits, which are almost the same in the frontend and backend.

import dbus
from dbus.mainloop.glib import DBusGMainLoop
from pkexceptions import PackageKitException, PackageKitNotStarted
from pkexceptions import PackageKitAccessDenied, PackageKitTransactionFailure
from pkexceptions import PackageKitBackendFailure

def dbusException(func):
    def wrapper(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except dbus.exceptions.DBusException, e:
            if e.get_dbus_name() == "org.freedesktop.DBus.Error.AccessDenied":
                raise PackageKitAccessDenied(e)
            elif e.get_dbus_name() == "org.freedesktop.DBus.Error.NoReply":
                raise PackageKitBackendFailure(e)
            else:
                raise PackageKitException(e)
    return wrapper

class PackageKitDbusInterface:

    def __init__(self, interface, service, path):
        DBusGMainLoop(set_as_default=True)
        bus = dbus.SystemBus()
        try:
            pk = bus.get_object(service, path)
            self.pk_iface = dbus.Interface(pk, dbus_interface=interface)
        except dbus.exceptions.DBusException, e:
            if e.get_dbus_name() == "org.freedesktop.DBus.Error.ServiceUnknown":
                raise PackageKitNotStarted
            else:
                raise PackageKitException(e)

        bus.add_signal_receiver(self.catchall_signal_handler, interface_keyword='dbus_interface', member_keyword='member', dbus_interface=interface)

        def catchall_signal_handler(self, *args, **kwargs):
            raise NotImplementedError()
