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

if __name__ == '__main__':
    if len(sys.argv) > 1:
        cmd = sys.argv[1:]
    else:
        cmd = 'all'

    pk = PackageKitClient()

    if 'all' in cmd or "refresh-cache" in cmd:
        print '---- RefreshCache() -----'''
        print pk.RefreshCache()

    if 'all' in cmd or "resolve" in cmd:
        print '---- Resolve() -----'
        pkg = pk.Resolve(FILTER_NONE, 'yum')
        if pkg:
            print pkg

    if 'all' in cmd or "get-packages" in cmd:
        print '---- GetPackages() ----'
        packages = pk.GetPackages(FILTER_INSTALLED)
        i = 0
        for pkg in packages:
            i += 1
            if i == 20:
                break
            (name,ver,arch,repo) = tuple(pkg['id'].split(";"))
            p =  "%s-%s.%s" % (name,ver,arch)
            print "%-40s : %s" % (p,pkg['summary'])
            details = pk.GetDetails(pkg['id'])
            print 79 *"-"
            print details[0]['detail']

    if 'all' in cmd or "search-file" in cmd:
        print '---- SearchFile() ----'
        print pk.SearchFile(FILTER_INSTALLED,"/usr/bin/yum")

    if 'all' in cmd or "get-updates" in cmd:
        print '---- GetUpdates() ----'
        pkgs = pk.GetUpdates(FILTER_INSTALLED)
        if pkgs: # We have updates
            for p in pkgs:
                print p['id']
                print pk.GetUpdateDetail(p['id'])

    if 'all' in cmd or "search-name" in cmd:
        print '---- SearchName() -----'
        print pk.SearchName(FILTER_NOT_INSTALLED, 'coreutils')
        print pk.SearchName(FILTER_INSTALLED, 'coreutils')


    def cb(status, pc, spc, el, rem, c):
        print 'install pkg: %s, %i%%, cancel allowed: %s' % (status, pc, str(c))
        return True
        #return pc < 12

    if "updates-system" in cmd:
        print '---- UpdateSystem() ----'
        print pk.UpdateSystem()

    if "install-packages" in cmd:
        print '---- InstallPackages() -----'
        pkg = pk.Resolve(FILTER_NOT_INSTALLED, 'yumex')
        if pkg:
            print "Installing : %s " % pkg[0]['id']
            pk.InstallPackages(pkg[0]['id'], cb)

    if "remove-packages" in cmd:
        print '---- RemovePackages() -----'
        pkg = pk.Resolve(FILTER_INSTALLED, 'yumex')
        if pkg:
            print "Removing : %s " % pkg[0]['id']
            pk.RemovePackages(pkg[0]['id'], cb)


    pk.SuggestDaemonQuit()

