# -*- coding: utf-8 -*-
#
# Copyright (C) 2022 Gordon Messmer
#
# Licensed under the GNU Lesser General Public License Version 2.1
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

"""
Notify packagekitd when packages are installed, updated, or removed. 
"""

import dbus
import dnf
from dnfpluginscore import _


class NotifyPackagekit(dnf.Plugin):
    name = "notify-packagekit"

    def __init__(self, base, cli):
        super(NotifyPackagekit, self).__init__(base, cli)
        self.base = base
        self.cli = cli

    def transaction(self):
        try:
            bus = dbus.SystemBus()
            proxy = bus.get_object('org.freedesktop.PackageKit', '/org/freedesktop/PackageKit')
            iface = dbus.Interface(proxy, dbus_interface='org.freedesktop.PackageKit')
            iface.StateHasChanged('posttrans')
        except:
            pass
