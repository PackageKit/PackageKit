#!/usr/bin/python
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

# Copyright (C) 2008
#    Richard Hughes <richard@hughsie.com>

from packagekit.enums import *
from yumComps import *
import os
import yum

def main():
    _yb = yum.YumBase()
    _db = "/var/cache/PackageKit/groups.sqlite"
    comps = yumComps(_yb, _db)
    comps.connect()
    print "pk group system"
    print 40 * "="
    _pkgs = comps.get_package_list('system')
    print _pkgs
    print "comps group games"
    print 40 * "="
    _pkgs = comps.get_meta_package_list('games')
    print _pkgs
    print "comps group kde-desktop"
    print 40 * "="
    _pkgs = comps.get_meta_package_list('kde-desktop')
    print _pkgs
    print "comps group other"
    print 40 * "="
    _pkgs = comps.get_groups('other')
    print _pkgs
    os.unlink(_db) # kill the db

if __name__ == "__main__":
    main()

