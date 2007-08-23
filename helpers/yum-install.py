#!/usr/bin/python
#
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
# Copyright (C) 2007 Red Hat Inc, Seth Vidal <skvidal@fedoraproject.org>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import yum
from yum.rpmtrans import RPMBaseCallback, RPMTransaction
import sys

my = yum.YumBase()
#my.doConfigSetup()
my.conf.cache = 1

sys.exit(1)

#DOES NOT WORK
class PackageKitCallback(RPMBaseCallback)
    def __init__(self):
        RPMBaseCallback.__init__()
        self.pct = 0
        
    def event(self, package, action, te_current, te_total, ts_current, ts_total):
        val = (ts_current*100L)/ts_total
        if val != self.pct:
            self.pct = val
            print >> sys.stderr, pct
    
    def errorlog(self, msg):
        # grrrrrrrr
        pass
        
my = yum.YumBase()

term = sys.argv[1]


my.install(name=term)
my.buildTransaction()

# download pkgs
# check
# order
# run
cb = RPMTransaction(self, display=PackageKitCallback)
my.runTransaction(cb=cb)

