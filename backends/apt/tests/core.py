#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Small helpers for the test suite."""
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
__all__ = ("get_tests_dir", "Chroot", "AptDaemonTestCase")

import inspect
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

import apt_pkg


class Chroot(object):

    """Provides a chroot which can be used by APT."""

    def __init__(self, prefix="tmp"):
        self.path = tempfile.mkdtemp(prefix)

    def setup(self):
        """Setup the chroot and modify the apt configuration."""
        for subdir in ["alternatives", "info", "parts", "updates", "triggers"]:
            path = os.path.join(self.path, "var", "lib", "dpkg", subdir)
            os.makedirs(path)
        for fname in ["status", "available"]:
            with open(os.path.join(self.path, "var", "lib", "dpkg", fname),
                      "w"):
                pass
        os.makedirs(os.path.join(self.path, "var/cache/apt/archives/partial"))
        os.makedirs(os.path.join(self.path, "var/lib/apt/lists"))
        os.makedirs(os.path.join(self.path, "var/lib/apt/lists/partial"))
        os.makedirs(os.path.join(self.path, "etc/apt/apt.conf.d"))
        os.makedirs(os.path.join(self.path, "etc/apt/sources.list.d"))
        os.makedirs(os.path.join(self.path, "var/log"))
        os.makedirs(os.path.join(self.path, "usr/bin"))
        os.makedirs(os.path.join(self.path, "media"))

        # Make apt use the new chroot
        shutil.copy(os.path.join(get_tests_dir(), "dpkg-wrapper.sh"),
                    os.path.join(self.path, "usr", "bin", "dpkg"))
        os.environ["ROOT"] = self.path
        apt_pkg.init_system()

    def remove(self):
        """Remove the files of the chroot."""
        apt_pkg.config.clear("Dir")
        apt_pkg.config.clear("Dir::State::Status")
        apt_pkg.init()
        shutil.rmtree(self.path)

    def add_trusted_key(self):
        """Add glatzor's key to the trusted ones."""
        gpg_cmd = "gpg --ignore-time-conflict --no-options " \
                  "--no-default-keyring --quiet " \
                  "--secret-keyring %s/etc/apt/secring.gpg " \
                  "--trustdb-name %s/etc/apt/trustdb.gpg " \
                  "--primary-keyring %s/etc/apt/trusted.gpg " % \
                  (self.path, self.path, self.path)
        os.system("%s --import %s/repo/glatzor.gpg" % (gpg_cmd,
                                                       get_tests_dir()))
        os.system("echo 'D0BF65B7DBE28DB62BEDBF1B683C53C7CF982D18:6:'| "
                  "%s --import-ownertrust " % gpg_cmd)

    def install_debfile(self, path, force_depends=False):
        """Install a package file into the chroot."""
        cmd_list = ["fakeroot", "dpkg", "--root", self.path,
                    "--log=%s/var/log/dpkg.log" % self.path]
        if force_depends:
            cmd_list.append("--force-depends")
        cmd_list.extend(["--install", path])
        cmd = subprocess.Popen(cmd_list,
                               env={"PATH": "/sbin:/bin:/usr/bin:/usr/sbin"})
        cmd.communicate()

    def add_test_repository(self, copy_list=True, copy_sig=True):
        """Add the test repository to the to the chroot."""
        return self.add_repository(os.path.join(get_tests_dir(), "repo"),
                                   copy_list, copy_sig)

    def add_cdrom_repository(self):
        """Emulate a repository on removable device."""
        # Create the content of a fake cdrom
        media_path = os.path.join(self.path, "tmp/fake-cdrom")
        # The cdom gets identified by the info file
        os.makedirs(os.path.join(media_path, ".disk"))
        with open(os.path.join(media_path, ".disk/info"), "w") as info:
            info.write("This is a fake CDROM")
        # Copy the test repository "on" the cdrom
        shutil.copytree(os.path.join(get_tests_dir(), "repo"),
                        os.path.join(media_path, "repo"))

        # Call apt-cdrom add
        mount_point = self.mount_cdrom()
        os.system("apt-cdrom add -m -d %s "
                  "-o 'Debug::Acquire::cdrom'=True "
                  "-o 'Acquire::cdrom::AutoDetect'=False "
                  "-o 'Dir'=%s" % (mount_point, self.path))
        self.unmount_cdrom()

        config_path = os.path.join(self.path, "etc/apt/apt.conf.d/11cdrom")
        with open(config_path, "w") as cnf:
            cnf.write('Debug::Acquire::cdrom True;\n'
                      'Acquire::cdrom::AutoDetect False;\n'
                      'Acquire::cdrom::NoMount True;\n'
                      'Acquire::cdrom::mount "%s";' % mount_point)

    def mount_cdrom(self):
        """Copy the repo information to the CDROM mount point."""
        mount_point = os.path.join(self.path, "media/cdrom")
        os.symlink(os.path.join(self.path, "tmp/fake-cdrom"), mount_point)
        return mount_point

    def unmount_cdrom(self):
        """Remove all files from the mount point."""
        os.unlink(os.path.join(self.path, "media/cdrom"))

    def add_repository(self, path, copy_list=True, copy_sig=True):
        """Add a sources.list entry to the chroot."""
        # Add a sources list
        lst_path = os.path.join(self.path, "etc/apt/sources.list")
        with open(lst_path, "w") as lst_file:
            lst_file.write("deb file://%s ./ # Test repository" % path)
        if copy_list:
            filename = apt_pkg.uri_to_filename("file://%s/." % path)
            shutil.copy(os.path.join(path, "Packages"),
                        "%s/var/lib/apt/lists/%s_Packages" % (self.path,
                                                              filename))
            if os.path.exists(os.path.join(path, "Release")):
                shutil.copy(os.path.join(path, "Release"),
                            "%s/var/lib/apt/lists/%s_Release" % (self.path,
                                                                 filename))
            if copy_sig and os.path.exists(os.path.join(path, "Release.gpg")):
                shutil.copy(os.path.join(path, "Release.gpg"),
                            "%s/var/lib/apt/lists/%s_Release.gpg" % (self.path,
                                                                     filename))

def get_tests_dir():
    """Return the absolute path to the tests directory."""
    # Try to detect a relative tests dir if we are running from the source
    # directory
    try:
        path = inspect.getsourcefile(sys.modules["core"])
    except KeyError as error:
        path = inspect.getsourcefile(inspect.currentframe())
    dir = os.path.dirname(path)
    if os.path.exists(os.path.join(dir, "repo/Packages")):
        return os.path.normpath(dir)
    else:
        raise Exception("Could not find tests direcotry")


# vim: ts=4 et sts=4
