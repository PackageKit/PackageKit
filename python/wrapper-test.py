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
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

# Copyright (C) 2008
#    Tim Lauridsen <timlau@fedoraproject.org>

import sys
from packagekit.client import PackageKitClient
from packagekit.enums import *

def show_packages(pkit, pkgs, details=False, limit=None):
    i = 0
    for pkg in pkgs:
        i += 1
        if limit and i == limit:
            break
        show_package(pkg)
        if details:
            detail = pkit.GetDetails(pkg.id)
            print 79 *"-"
            print detail[0].detail
            print 79 *"="

def show_package(pkg):
    if pkg:
        if isinstance(pkg, list):
            pkg = pkg[0]
        print str(pkg)
    else:
        print "no package found"

if __name__ == '__main__':
    if len(sys.argv) > 1:
        cmd = sys.argv[1:]
    else:
        cmd = 'all'

    pk = PackageKitClient()

    if 'all' in cmd or "refresh-cache" in cmd:
        print '---- RefreshCache() -----'''
        pk.RefreshCache()

    if 'all' in cmd or "resolve" in cmd:
        print '---- Resolve() -----'
        pkg = pk.Resolve(FILTER_NONE, 'yum')
        show_package(pkg)

    if 'all' in cmd or "get-packages" in cmd:
        print '---- GetPackages() ----'
        packages = pk.GetPackages(FILTER_INSTALLED)
        show_packages(pk, packages, details=True, limit=20)

    if 'all' in cmd or "search-file" in cmd:
        print '---- SearchFile() ----'
        pkgs = pk.SearchFile(FILTER_INSTALLED,"/usr/bin/yum")
        show_packages(pk, pkgs)

    if 'all' in cmd or "get-updates" in cmd:
        print '---- GetUpdates() ----'
        pkgs = pk.GetUpdates(FILTER_INSTALLED)
        if pkgs: # We have updates
            for p in pkgs:
                print p.id
                print pk.GetUpdateDetail(p.id)

    if 'all' in cmd or "search-name" in cmd:
        print '---- SearchName() -----'
        show_package(pk.SearchName(FILTER_NOT_INSTALLED, 'coreutils'))
        show_package(pk.SearchName(FILTER_INSTALLED, 'coreutils'))

    if  "search-group" in cmd:
        print '---- SearchGroup() -----'
        show_packages(pk, pk.SearchGroup(FILTER_NONE, GROUP_GAMES))
        show_packages(pk, pk.SearchGroup(FILTER_NONE, GROUP_COLLECTIONS))

    if  "get-distro-upgrades" in cmd:
        print '---- GetDistroUpgrades() -----'
        rc = pk.GetDistroUpgrades()
        if rc:
            print rc
        else:
            print "No distribution upgrades"


    def cb(status, pct, spct, elem, rem, cancel):
        print 'install pkg: %s, %i%%, cancel allowed: %s' % \
              (status, pct, str(cancel))
        return True
        #return pc < 12

    if "updates-system" in cmd:
        print '---- UpdateSystem() ----'
        print pk.UpdateSystem()

    if "install-packages" in cmd:
        print '---- InstallPackages() -----'
        pkg = pk.Resolve(FILTER_NOT_INSTALLED, 'yumex')
        if pkg:
            print "Installing : %s " % pkg[0].id
            pk.InstallPackages(pkg[0].id, cb)

    if "remove-packages" in cmd:
        print '---- RemovePackages() -----'
        pkg = pk.Resolve(FILTER_INSTALLED, 'yumex')
        if pkg:
            print "Removing : %s " % pkg[0].id
            pk.RemovePackages(pkg[0].id, cb)

    if "download-packages" in cmd:
        print '---- DownloadPackages() -----'
        pkg = pk.Resolve(FILTER_NOT_INSTALLED, 'yumex')
        if pkg:
            print "Installing : %s " % pkg[0].id
            print pk.DownloadPackages(pkg[0].id)

    pk.SuggestDaemonQuit()

