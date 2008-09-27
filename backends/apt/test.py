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

import os
import shutil
import subprocess
import time
import unittest

import apt
import dbus
import gobject

import apt
import apt_pkg
import aptDBUSBackend
from packagekit.enums import *
import packagekit.client

PKG_ID = 'yum;3.2.12-1.2;i386;Debian'

apt_pkg.InitConfig()
root = os.path.join(os.getcwd(), "testroot")
config = apt_pkg.Config
config.Set("Dir", root)
config.Set("Dir::State::status",
           os.path.join(root, "/var/lib/dpkg/status"))

class AptBackendTestCase(unittest.TestCase):

    def setUp(self):
        """
        Setup the package client which tiggers the tests and an apt cache to
        compare the results
        """
        self.running = True
        self.pk = packagekit.client.PackageKitClient()
        self.cache = apt.Cache()
        self.cache.open(None)

    def tearDown(self):
        """
        Completely remove the client interface and the cache
        """
        del(self.pk)
        del(self.cache)

    def _setUpChroot(self):
        if os.path.exists("testroot/"):
            shutil.rmtree("testroot/")

    def testGetUpdates(self):
        """
        Perfrom a call of GetUpdates and compare the results to the ones from
        an apt cache implementation

        FIXME: Should smart be used here instead of apt?
        """
        # Get updates from packagekit
        pkgs = self.pk.GetUpdates()
        # get updates from apt 
        self.cache.upgrade()
        blocked = map(lambda p: p.name,
                      filter(lambda p: p.isUpgradable and not p.markedUpgrade,
                             self.cache))
        marked = map(lambda p: p.name, self.cache.getChanges())
        for p in pkgs:
            name = p.id.split(";")[0]
            if p.info == INFO_BLOCKED:
                if name in blocked:
                    blocked.remove(name)
                else:
                    self.fail("PackageKit reported a blocked update in "
                              "contrast to apt")
            else:
                if name in marked:
                    marked.remove(name)
                else:
                    self.fail("PackageKit returned an update in "
                              "contrast to apt")
        self.assertTrue(marked == blocked == [], 
                        "PackageKit didn't return all updates:"
                        "blocked: %s, available: %s" % (blocked, marked))
 
    def testResolve(self):
        """
        Check if PackageKit can resolve the package name xterm to a correct
        package id
        """
        pkgs = self.pk.Resolve("none", "xterm")
        self.failUnless(len(pkgs) == 1 and pkgs[0].id.startswith("xterm;"),
                        "PackageKit returned wrong package(s) for xterm:"
                        "%s" % pkgs)

    def testInstallPackages(self):
        """
        Check installation of a package
        """
        if self.cache["yum"].isInstalled:
            self.pk.RemovePackages([PKG_ID], self._callback)
        self.pk.InstallPackages([PKG_ID], self._callback)
        self.cache.open(None)
        self.assertTrue(self.cache["yum"].isInstalled,
                        "yum was not installed")

    def testRemovePackages(self):
        """
        Check removal of a package
        """
        if not self.cache["yum"].isInstalled:
            self.pk.InstallPackages([PKG_ID], self._callback)
        self.pk.RemovePackages([PKG_ID], self._callback)
        self.cache.open(None)
        self.assertFalse(self.cache["yum"].isInstalled,
                         "yum is still installed")

    def _callback(self, status, percentage, subpercentage, elapsed,
                  remaining, allow_cancel):
        """
        Callback for packagekit methods which take a long time. 
        This method isn't currently of any use
        """
        return True


def main():
    unittest.main()

if __name__ == '__main__':
    main()

# vim: ts=4 et sts=4
