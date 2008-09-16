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
    pk = PackageKitClient()

    #print '---- RefreshCache() -----'''
    #print pk.RefreshCache()

    print '---- Resolve() -----'
    pkg = pk.Resolve(FILTER_NONE, ['yum'])
    print pkg[0]

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
        details = pk.GetDetails([pkg['id']])
        print 79 *"-"
        print details[0]['detail']

    print '---- SearchFiles() ----'
    print pk.SearchFile(FILTER_INSTALLED,"/usr/bin/yum")

    print '---- GetUpdates() ----'
    print pk.GetUpdates(FILTER_INSTALLED)

    print '---- SearchName() -----'
    print pk.SearchName(FILTER_NOT_INSTALLED, 'coreutils')
    print pk.SearchName(FILTER_INSTALLED, 'coreutils')

    sys.exit(0)

    def cb(status, pc, spc, el, rem, c):
        print 'install pkg: %s, %i%%, cancel allowed: %s' % (status, pc, str(c))
        return True
        #return pc < 12

    print '---- UpdateSystem() ----'
    print pk.UpdateSystem()

    print '---- InstallPackages() -----'
    pk.InstallPackages(['pmount;0.9.17-2;i386;Ubuntu', 'quilt;0.46-6;all;Ubuntu'], cb)


    print '---- RemovePackages() -----'
    pk.RemovePackages(['pmount;0.9.17-2;i386;Ubuntu', 'quilt;0.46-6;all;Ubuntu'], cb)


    pk.SuggestDaemonQuit()

