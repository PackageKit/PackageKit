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

import dbus

class PackageKitException(Exception):
    def __init__(self, e=None):
        Exception.__init__(self)
        if e == None:
            self._pk_name = None
            self._full_str = None
        else:
            if not isinstance(e, dbus.exceptions.DBusException):
                raise Exception, "Can only handle DBusExceptions"
            self._pk_name = str(e.get_dbus_name())
            self._full_str = str(e)

    def get_backend_name(self):
        return self._pk_name

    def __str__(self):
        if self._full_str != None:
            return self._full_str
        else:
            return ""

class PackageKitNotStarted(PackageKitException):
    pass

class PackageKitAccessDenied(PackageKitException):
    pass

class PackageKitTransactionFailure(PackageKitException):
    pass

class PackageKitBackendFailure(PackageKitException):
    pass

class PackageKitBackendNotLocked(PackageKitException):
    pass
