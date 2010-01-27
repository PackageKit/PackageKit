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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
import mox
import nose.tools

from aptDBUSBackend import PackageKitAptBackend
from packagekit.enums import *

TEMPDIR = tempfile.mkdtemp(prefix="apt-backend-test")

class AptBackendTestCase(mox.MoxTestBase):
    """Test suite for the APT backend"""

    def setUp(self):
        """Create a mox factory and a backend instance"""
        mox.MoxTestBase.setUp(self)
        self.backend = PackageKitAptBackend(None, None)
        for cb in ["Package", "Finished"]:
            self.mox.StubOutWithMock(self.backend, cb)

    @nose.tools.timed(10)
    def test_00_Init(self):
        """Test the initialization"""
        self.mox.ReplayAll()
        self.backend.Init()
        while threading.activeCount() > 1:
            time.sleep(0.1)
        binary = os.path.join(TEMPDIR,
                              apt_pkg.Config["Dir::Cache"],
                              apt_pkg.Config["Dir::Cache::pkgcache"])
        source = os.path.join(TEMPDIR,
                              apt_pkg.Config["Dir::Cache"],
                              apt_pkg.Config["Dir::Cache::srcpkgcache"])
        self.assertTrue(os.path.exists(source))
        self.assertTrue(os.path.exists(binary))
        pkg = self.backend._cache["xterm"]
        self.assertEqual(pkg.candidateVersion, "235-1")

    @nose.tools.timed(10)
    def test_01_Refresh(self):
        """Test the Refresh of the cache method"""
        self.backend.Finished(EXIT_SUCCESS)
        self.mox.ReplayAll()
        self.backend.doRefreshCache(False)
        while threading.activeCount() > 1:
            time.sleep(0.1)
        self.assertEqual(self.backend._cache["xterm"].candidateVersion, "237-1")
        self.assertTrue(self.backend._cache.has_key("synaptic"))

    @nose.tools.timed(10)
    def test_20_SearchName(self):
        """Test the doSearchName method"""
        self.backend.Package(INFO_INSTALLED, "xterm;235-1;i386;",
                             "X terminal emulator")
        self.backend.Finished(EXIT_SUCCESS)
        self.mox.ReplayAll()
        self.backend.doSearchName(FILTER_NONE, "xterm")
        while threading.activeCount() > 1:
            time.sleep(0.1)

    @nose.tools.timed(10)
    def test_20_SearchFile(self):
        """Test the doSearchFile method"""
        self.backend.Package(INFO_INSTALLED, "xterm;235-1;i386;",
                             "X terminal emulator")
        self.backend.Finished(EXIT_SUCCESS)
        self.mox.ReplayAll()
        self.backend.doSearchFile(FILTER_NONE, "bin/xterm")
        while threading.activeCount() > 1:
            time.sleep(0.1)

    @nose.tools.timed(10)
    def test_20_GetUpdates(self):
        """Test the doGetUpdates method"""
        self.backend.Package(INFO_NORMAL, "xterm;237-1;i386;",
                             "X terminal emulator")
        self.backend.Finished(EXIT_SUCCESS)
        self.mox.ReplayAll()
        self.backend.doGetUpdates(FILTER_NONE)
        while threading.activeCount() > 1:
            time.sleep(0.1)


def setup():
    """Create a temporary and very simple chroot for apt"""
    apt_pkg.InitConfig()
    config = apt_pkg.Config
    config.Set("Dir::Etc::sourcelist", 
               os.path.join(TEMPDIR, "etc/apt/sources.list"))
    config.Set("Dir::Etc::sourceparts", "")
    config.Set("Dir", TEMPDIR)
    config.Set("Dir::State::status",
               os.path.join(TEMPDIR, "var/lib/dpkg/status"))
    os.makedirs(os.path.join(TEMPDIR, "var/lib/apt/lists/partial"))
    os.makedirs(os.path.join(TEMPDIR, "var/cache/apt/archives/partial"))
    os.makedirs(os.path.join(TEMPDIR, "var/lib/dpkg/info"))
    os.makedirs(os.path.join(TEMPDIR, "etc/apt"))
    os.makedirs(os.path.join(TEMPDIR, "repo"))
    shutil.copy("data/Packages", os.path.join(TEMPDIR, "repo/Packages"))
    shutil.copy("data/status", os.path.join(TEMPDIR, "var/lib/dpkg/status"))
    shutil.copy("data/xterm.list", os.path.join(TEMPDIR,
                                                "var/lib/dpkg/info/xterm.list"))
    sources = open(os.path.join(TEMPDIR, "etc/apt/sources.list"), "w")
    sources.write("deb file://%s/repo/ ./\n" % TEMPDIR)
    sources.close()


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
