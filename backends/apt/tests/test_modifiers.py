#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Provides unit tests for the APT backend."""
# Copyright (C) 2011 Sebastian Heinlein <devel@glatzor.de>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
# Licensed under the GNU General Public License Version 2

__author__  = "Sebastian Heinlein <devel@glatzor.de>"

import glob
import os
import shutil
import unittest

import apt_pkg
import mox

from core import get_tests_dir, Chroot
from packagekit import enums
import aptBackend

REPO_PATH = os.path.join(get_tests_dir(), "repo")


class CacheModifiersTests(mox.MoxTestBase):

    """Test cases for cache modifying methods of the APT backend.

    Note: In the test environment we emit the finished signal in the
          case of errors, too.
    """

    def setUp(self):
        mox.MoxTestBase.setUp(self)
        self.chroot = Chroot()
        self.chroot.setup()
        self.addCleanup(self.chroot.remove)
        self.backend = aptBackend.PackageKitAptBackend([])

    def _catch_callbacks(self, *args):
        methods = list(args)
        methods.extend(("error", "finished"))
        for meth in methods:
            self.mox.StubOutWithMock(self.backend, meth)

    def test_refresh_cache(self):
        """Test updating the cache using a local repository."""
        self._catch_callbacks()
        self.backend.finished()
        # Setup environment
        self.mox.ReplayAll()
        self.chroot.add_trusted_key()
        path = os.path.join(self.chroot.path,
                            "etc/apt/sources.list.d/test.list")
        with open(path, "w") as part_file:
            part_file.write("deb file://%s ./" % REPO_PATH)
        self.backend._open_cache()
        # Install the package
        self.backend.dispatch_command("refresh-cache", ["true"])

        self.backend._open_cache()
        pkg = self.backend._cache["silly-base"]
        self.assertTrue(pkg.candidate.origins[0].trusted)

    def test_update_system(self):
        """Test upgrading the system."""
        self._catch_callbacks()
        self.backend.finished()
        # Setup environment
        self.mox.ReplayAll()
        self.chroot.add_test_repository()
        self.chroot.install_debfile(os.path.join(REPO_PATH,
                                                 "silly-base_0.1-0_all.deb"))
        self.backend._open_cache()
        # Install the package
        self.backend.dispatch_command("update-system", ["true"])

        self.backend._open_cache()
        self.assertEqual(self.backend._cache["silly-base"].installed.version,
                         "0.1-0update1")

    def test_update_packages(self):
        """Test upgrading the system."""
        self._catch_callbacks()
        self.backend.finished()
        # Setup environment
        self.mox.ReplayAll()
        self.chroot.add_test_repository()
        self.chroot.install_debfile(os.path.join(REPO_PATH,
                                                 "silly-base_0.1-0_all.deb"))
        self.backend._open_cache()
        # Install the package
        self.backend.dispatch_command("update-packages",
                                      ["true", "silly-base;0.1-0update1;all;"])

        self.backend._open_cache()
        self.assertEqual(self.backend._cache["silly-base"].installed.version,
                         "0.1-0update1")

    def test_install(self):
        """Test installation of a package from a repository."""
        self._catch_callbacks()
        self.backend.finished()
        # Setup environment
        self.mox.ReplayAll()
        self.chroot.add_test_repository()
        self.backend._open_cache()
        # Install the package
        self.backend.dispatch_command("install-packages",
                                      ["True", "silly-base;0.1-0update1;all;"])
        self.backend._open_cache()
        self.assertEqual(self.backend._cache["silly-base"].is_installed, True)

    def test_install_fail(self):
        """Test handling the installation of failing packages."""
        self._catch_callbacks()
        self.backend.error(enums.ERROR_PACKAGE_FAILED_TO_INSTALL,
                           mox.IsA(str), True)
        self.backend.finished()
        # Setup environment
        self.mox.ReplayAll()
        self.chroot.add_test_repository()
        self.backend._open_cache()
        # Install the package
        self.backend.dispatch_command("install-packages",
                                      ["True", "silly-fail;0.1-0;all;"])
        self.backend._open_cache()
        self.assertEqual(self.backend._cache["silly-base"].is_installed, False)

    def test_install_timeout(self):
        """Test installation of hanging package."""
        if os.getuid() != 0:
            self.skipTest("The test requires root privileges")
        self._catch_callbacks()
        self.backend.error(enums.ERROR_PACKAGE_FAILED_TO_CONFIGURE,
                           mox.IsA(u""), True)
        self.backend.finished()
        # Setup environment
        self.mox.ReplayAll()
        self.chroot.add_test_repository()
        # Copy the files for a small execution environment to execute dash
        os.system("dpkg -L dash libc6 |"
                  "xargs -i cp --parents '{}' %s" % self.chroot.path)
        self.backend._open_cache()
        aptBackend.TIMEOUT_IDLE_INSTALLATION = 5
        # Install the package
        self.backend.dispatch_command("install-packages",
                                      ["True",
                                       "silly-postinst-input;0.1-0;all;"])

    def test_install_only_trusted(self):
        """Test if the installation of a not trusted package fails."""
        self._catch_callbacks()
        self.backend.error(enums.ERROR_MISSING_GPG_SIGNATURE,
                           mox.IsA(str), True)
        self.backend.finished()
        # Setup environment
        self.mox.ReplayAll()
        self.chroot.add_test_repository(copy_sig=False)
        self.backend._open_cache()
        # Install the package
        self.backend.dispatch_command("install-packages",
                                      ["true", "silly-base;0.1-0update1;all;"])
        self.backend._open_cache()
        self.assertEqual(self.backend._cache["silly-base"].is_installed, False)

    def test_simulate_install(self):
        """Test simulation of package installation."""
        self._catch_callbacks("package")
        self.backend.package("silly-base;0.1-0update1;all;",
                             enums.INFO_INSTALLING,
                             "working package")
        self.backend.finished()
        # Setup environment
        self.mox.ReplayAll()
        self.chroot.add_test_repository()
        self.backend._open_cache()
        # Run the command
        self.backend.dispatch_command("simulate-install-packages",
                                      ["silly-depend-base;0.1-0;all;"])

    def test_remove_essential(self):
        """Test if we forbid to remove essential packages."""
        self.chroot.install_debfile(os.path.join(REPO_PATH,
                                               "silly-essential_0.1-0_all.deb"))
        self.backend._open_cache()

        self._catch_callbacks()
        self.backend.error(enums.ERROR_CANNOT_REMOVE_SYSTEM_PACKAGE,
                           mox.IsA(str), True)
        self.backend.finished()

        self.mox.ReplayAll()
        # Run the command
        self.backend.dispatch_command("remove-packages",
                                      ["true", "true",
                                       "silly-essential;0.1-0;all;"])
        self.backend._open_cache()
        self.assertTrue(self.backend._cache["silly-essential"].is_installed)

    def test_remove_disallow_deps(self):
        """Test the removal of packages."""
        for pkg in ["silly-base_0.1-0_all.deb",
                    "silly-depend-base_0.1-0_all.deb"]:
            self.chroot.install_debfile(os.path.join(REPO_PATH, pkg))
        self.backend._open_cache()

        self._catch_callbacks()
        self.backend.error(enums.ERROR_DEP_RESOLUTION_FAILED,
                           mox.IsA(str), True)
        self.backend.finished()

        self.mox.ReplayAll()
        # Run the command
        self.backend.dispatch_command("remove-packages",
                                      ["false", "true",
                                       "silly-base;0.1-0;all;"])

        self.backend._open_cache()
        self.assertEqual(self.backend._cache["silly-base"].is_installed, True)
        self.assertEqual(self.backend._cache["silly-depend-base"].is_installed,
                         True)

    def test_remove(self):
        """Test the removal of packages."""
        self.backend._open_cache()
        for pkg in ["silly-base_0.1-0_all.deb",
                    "silly-depend-base_0.1-0_all.deb"]:
            self.chroot.install_debfile(os.path.join(REPO_PATH, pkg))
        ext_states = apt_pkg.config.find_file("Dir::State::extended_states")
        with open(ext_states, "w") as ext_states_file:
            ext_states_file.write("""Package: silly-base
Architecture: all
Auto-Installed: 1""")
        self.backend._open_cache()
        self._catch_callbacks()
        self.backend.finished()
        self.mox.ReplayAll()

        # Run the command
        self.backend.dispatch_command("remove-packages",
                                      ["true", "true",
                                       "silly-depend-base;0.1-0;all;"])

        self.backend._open_cache()
        self.assertRaises(KeyError,
                          lambda: self.backend._cache["silly-depend-base"])
        self.assertRaises(KeyError, lambda: self.backend._cache["silly-base"])

    def test_install_file(self):
        """Test the installation of a local package file."""
        self.chroot.add_test_repository()
        debfile = os.path.join(REPO_PATH, "silly-depend-base_0.1-0_all.deb")
        debfile2 = os.path.join(REPO_PATH, "silly-essential_0.1-0_all.deb")
        self._catch_callbacks()
        self.backend.finished()
        # Setup environment
        self.mox.ReplayAll()
        self.chroot.add_test_repository()
        self.backend._open_cache()
        # Install the package files
        self.backend.dispatch_command("install-files",
                                      ["true", "|".join((debfile, debfile2))])

        self.backend._open_cache()
        self.assertEqual(self.backend._cache["silly-base"].is_installed, True)
        self.assertEqual(self.backend._cache["silly-essential"].is_installed,
                         True)
        self.assertEqual(self.backend._cache["silly-depend-base"].is_installed,
                         True)

    def test_media_change_fail(self):
        """Test correct failure in the case of missing medium."""
        self._catch_callbacks()
        self.backend.error(enums.ERROR_MEDIA_CHANGE_REQUIRED, mox.IsA(unicode),
                           True)
        self.backend.finished()
        # Setup environment
        self.mox.ReplayAll()
        self.chroot.add_trusted_key()
        self.chroot.add_cdrom_repository()
        self.backend._open_cache()
        # Install the package
        self.backend.dispatch_command("install-packages",
                                      ["True", "silly-base;0.1-0;all;"])
        self.backend._open_cache()
        self.assertEqual(self.backend._cache["silly-base"].is_installed, False)


if __name__ == "__main__":
    unittest.main()

# vim: ts=4 et sts=4
