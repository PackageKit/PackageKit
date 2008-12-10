#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Provides unit test of the apt backend of PackageKit

Copyright (C) 2008 Sebastian Heinlein <glatzor@ubuntu.com>

Licensed under the GNU General Public License Version 2

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
Licensed under the GNU General Public License Version 2
"""

__author__  = "Sebastian Heinlein <devel@glatzor.de>"

import threading
import time
import os
import shutil
import sys
import tempfile
import unittest

import apt
import apt_pkg
import dbus
import dbus.mainloop.glib
import gobject
import mox
import nose.tools

from aptDBUSBackend import PackageKitAptBackend, PACKAGEKIT_DBUS_SERVICE
from packagekit.enums import *
from packagekit.daemonBackend import PACKAGEKIT_DBUS_INTERFACE, PACKAGEKIT_DBUS_PATH

TEMPDIR = tempfile.mkdtemp(prefix="apt-backend-test")

class AptBackendTestCase(mox.MoxTestBase):
    """Test suite for the APT backend"""

    def setUp(self):
        """Create a mox factory and a backend instance"""
        mox.MoxTestBase.setUp(self)
        dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
        bus = dbus.SessionBus()
        bus_name = dbus.service.BusName(PACKAGEKIT_DBUS_SERVICE, bus=bus)
        self.backend = PackageKitAptBackend(bus_name, PACKAGEKIT_DBUS_PATH)
        self.loop = gobject.MainLoop()

    def tearDown(self):
        mox.MoxTestBase.tearDown(self)
        context = self.loop.get_context()
        while context.pending():
            context.iteration()

    @nose.tools.timed(10)
    def testSearchName(self):
        """Test the doSearchName method"""
        self.mox.StubOutWithMock(self.backend, "Package")
        self.backend.Package('installed', 'xterm;235-1;i386;',
                             'X terminal emulator')
        self.mox.ReplayAll()
        self.backend.doSearchName("none", "xterm")
        while threading.activeCount() > 1:
            time.sleep(1)

    @nose.tools.timed(10)
    def testSearchFile(self):
        """Test the doSearchFile method"""
        self.mox.StubOutWithMock(self.backend, "Package")
        self.backend.Package('installed', 'xterm;235-1;i386;',
                             'X terminal emulator')
        self.mox.ReplayAll()
        self.backend.doSearchFile("none", "xterm")
        while threading.activeCount() > 1:
            time.sleep(1)


def setup():
    """Create a temporary and very simple chroot for apt"""
    apt_pkg.InitConfig()
    config = apt_pkg.Config
    config.Set("Dir", TEMPDIR)
    config.Set("Dir::State::status",
               os.path.join(TEMPDIR, "var/lib/dpkg/status"))
    os.makedirs(os.path.join(TEMPDIR, "var/lib/apt/lists"))
    os.makedirs(os.path.join(TEMPDIR, "var/cache/apt/partial"))
    os.makedirs(os.path.join(TEMPDIR, "var/lib/dpkg/info"))
    shutil.copy("status.test", os.path.join(TEMPDIR, "var/lib/dpkg/status"))
    shutil.copy("xterm.list.test", os.path.join(TEMPDIR,
                                                "var/lib/dpkg/info/xterm.list"))

def teardown():
    """Clear up temporary files"""
    if os.path.exists(TEMPDIR):
        shutil.rmtree(TEMPDIR)

def usage():
    print "ERROR: Run ./test.sh instead"
    sys.exit(1)

if __name__ == "__main__":
    usage()

# vim: ts=4 et sts=4
