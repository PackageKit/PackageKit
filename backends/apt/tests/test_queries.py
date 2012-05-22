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

import os
import os.path
import unittest
import tempfile
import shutil
import sys

import apt_pkg
import mox

from core import get_tests_dir, Chroot
from packagekit import enums
import aptBackend

REPO_PATH = os.path.join(get_tests_dir(), "repo")

chroot = Chroot()


class QueryTests(mox.MoxTestBase):

    """Test cases for non-destructive methods."""

    def setUp(self):
        mox.MoxTestBase.setUp(self)
        self.backend = aptBackend.PackageKitAptBackend([])

        self.workdir = tempfile.mkdtemp()
        self.orig_sys_path = sys.path

    def tearDown(self):
        shutil.rmtree(self.workdir)
        sys.path = self.orig_sys_path

    def _catch_callbacks(self, *args):
        methods = list(args)
        methods.extend(("error", "finished"))
        self.mox.UnsetStubs()
        for meth in methods:
            self.mox.StubOutWithMock(self.backend, meth)

    def test_resolve(self):
        """Test resolving the name of package."""
        self._catch_callbacks("package")
        self.backend.package("silly-base;0.1-0;all;",
                             enums.INFO_INSTALLED,
                             "working package")
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()
        # Install the package
        self.backend.dispatch_command("resolve",
                                      ["None", "silly-base"])

    def test_search_name(self):
        """Test searching for package names."""
        self._catch_callbacks("package")
        self.backend.package("silly-base;0.1-0update1;all;",
                             enums.INFO_AVAILABLE,
                             "working package")
        self.backend.package("silly-base;0.1-0;all;",
                             enums.INFO_INSTALLED,
                             "working package")
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("search-name",
                                      ["None", "silly-base"])

    def test_search_details(self):
        """Test searching for package descriptions."""
        self._catch_callbacks("package")
        self.backend.package("silly-fail;0.1-0;all;",
                             enums.INFO_AVAILABLE,
                             mox.IsA(str))
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()
        # Install the package
        aptBackend.XAPIAN_SUPPORT = False
        self.backend.dispatch_command("search-details",
                                      ["None", "always fail"])


    def test_what_provides_codec(self):
        """Test searching for package providing a codec."""
        # invalid query
        self._catch_callbacks()
        self.backend.error("not-supported", mox.StrContains("search term is invalid"), True)
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides",
                                      ["None", enums.PROVIDES_CODEC, "audio/mpeg"])

        self._catch_callbacks("package")
        self.backend.package("gstreamer0.10-silly;0.1-0;all;",
                             enums.INFO_AVAILABLE,
                             mox.IsA(str))
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides",
                                      ["None", enums.PROVIDES_CODEC, "gstreamer0.10(decoder-audio/ac3)"])

    def test_what_provides_modalias(self):
        """Test searching for package providing a driver."""

        # invalid query
        self._catch_callbacks()
        self.backend.error("not-supported", mox.StrContains("search term is invalid"), True)
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides",
                                      ["None", enums.PROVIDES_MODALIAS, "pci 1"])

        # no match
        self._catch_callbacks()
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides",
                                      ["None", enums.PROVIDES_MODALIAS, "pci:v0000DEADd0000BEEFsv00sd00bc02sc00i00"])




        # match
        self._catch_callbacks("package")
        self.backend.package("silly-driver;0.1-0;all;",
                             enums.INFO_AVAILABLE,
                             mox.IsA(str))
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides",
                                      ["None", enums.PROVIDES_MODALIAS, "pci:v0000DEADd0000BEEFsv00sd00bc03sc00i00"])


        # second match
        self._catch_callbacks("package")
        self.backend.package("silly-driver;0.1-0;all;",
                             enums.INFO_AVAILABLE,
                             mox.IsA(str))
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides",
                                      ["None", enums.PROVIDES_MODALIAS, "pci:v0000DEADd0000FACEsv00sd00bc03sc00i00"])


    def test_what_provides_any(self):
        """Test searching for package providing "any"."""
        # codec
        self._catch_callbacks("package")
        self.backend.package("gstreamer0.10-silly;0.1-0;all;",
                             enums.INFO_AVAILABLE,
                             mox.IsA(str))
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides",
                                      ["None", enums.PROVIDES_ANY, "gstreamer0.10(decoder-audio/ac3)"])

        # modalias
        self._catch_callbacks("package")
        self.backend.package("silly-driver;0.1-0;all;",
                             enums.INFO_AVAILABLE,
                             mox.IsA(str))
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides",
                                      ["None", enums.PROVIDES_ANY, "pci:v0000DEADd0000BEEFsv00sd00bc03sc00i00"])

        # invalid query; ANY should not return an error, just not return any results
        self._catch_callbacks("package")
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides", 
                                      ["None", enums.PROVIDES_ANY, "pci:1"])


    def test_what_provides_unsupported(self):
        """Test searching for package providing an unsupported type."""

        self._catch_callbacks()
        self.backend.error("not-supported", mox.StrContains("not implemented"), True)
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides",
                                      ["None", enums.PROVIDES_PLASMA_SERVICE, "plasma4(dataengine-weather)"])

    def test_what_provides_plugin(self):
        """Test what-provides plugins"""

        # add plugin for extra codecs
        f = open(os.path.join(self.workdir, "extra_codecs.py"), "w")
        f.write("""from packagekit import enums

def fake_what_provides(cache, type, search):
    if type in (enums.PROVIDES_CODEC, enums.PROVIDES_ANY):
        return [cache["gstreamer0.10-silly"]]
    raise NotImplementedError('cannot handle type ' + str(type))
""")
        f.close()
        os.mkdir(os.path.join(self.workdir, "extra_codecs-0.egg-info"))
        f = open(os.path.join(self.workdir, "extra_codecs-0.egg-info", 'entry_points.txt'), "w")
        f.write("[packagekit.apt.plugins]\nwhat_provides=extra_codecs:fake_what_provides\n")
        f.close()

        # invalid plugin, should not stop the valid ones
        os.mkdir(os.path.join(self.workdir, "nonexisting-1.egg-info"))
        f = open(os.path.join(self.workdir, "nonexisting-1.egg-info", 'entry_points.txt'), "w")
        f.write("[packagekit.apt.plugins]\nwhat_provides=nonexisting:what_provides\n")
        f.close()

        # another plugin to test chaining and a new type
        f = open(os.path.join(self.workdir, "more_stuff.py"), "w")
        f.write("""from packagekit import enums

def my_what_provides(cache, type, search):
    if type in (enums.PROVIDES_CODEC, enums.PROVIDES_ANY):
        return [cache["silly-base"]]
    if type in (enums.PROVIDES_LANGUAGE_SUPPORT, enums.PROVIDES_ANY):
        return [cache["silly-important"]]
    raise NotImplementedError('cannot handle type ' + str(type))
""")
        f.close()
        os.mkdir(os.path.join(self.workdir, "more_stuff-0.egg-info"))
        f = open(os.path.join(self.workdir, "more_stuff-0.egg-info", 'entry_points.txt'), "w")
        f.write("[packagekit.apt.plugins]\nwhat_provides=more_stuff:my_what_provides\n")
        f.close()

        sys.path.insert(0, self.workdir)

        # reinitialize backend to pick up new sys.path
        self.backend = aptBackend.PackageKitAptBackend([])

        # search for CODEC (note that gstreamer0.10-silly does not advertise
        # audio/vorbis, so it isn't caught by the internal implementation)
        self._catch_callbacks("package")
        self.backend.package("gstreamer0.10-silly;0.1-0;all;",
                             enums.INFO_AVAILABLE,
                             mox.IsA(str))
        self.backend.package("silly-base;0.1-0;all;",
                             enums.INFO_INSTALLED,
                             mox.IsA(str))
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides",
                                      ["None", enums.PROVIDES_CODEC, "gstreamer0.10(decoder-audio/vorbis)"])

        # test filtering
        self._catch_callbacks("package")
        self.backend.package("silly-base;0.1-0;all;",
                             enums.INFO_INSTALLED,
                             mox.IsA(str))
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides",
                                      ["installed", enums.PROVIDES_CODEC, "gstreamer0.10(decoder-audio/vorbis)"])

        # test new type
        self._catch_callbacks("package")
        self.backend.package("silly-important;0.1-0;all;",
                             enums.INFO_AVAILABLE,
                             mox.IsA(str))
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides", 
                                      ["None", enums.PROVIDES_LANGUAGE_SUPPORT, "locale(de_DE)"])


        # test unsupported type
        self._catch_callbacks()
        self.backend.error("not-supported", mox.StrContains("not implemented"), True)
        self.backend.finished()
        self.mox.ReplayAll()
        self.backend._open_cache()

        self.backend.dispatch_command("what-provides",
                                      ["None", enums.PROVIDES_PLASMA_SERVICE, "plasma4(dataengine-weather)"])


def setUp():
    chroot.setup()
    chroot.add_test_repository()
    chroot.install_debfile(os.path.join(REPO_PATH, "silly-base_0.1-0_all.deb"))

def tearDown():
    chroot.remove()


if __name__ == "__main__":
    setUp()
    unittest.main()
    tearDown()

# vim: ts=4 et sts=4
