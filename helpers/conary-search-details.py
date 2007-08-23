#!/usr/bin/python
#
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
# Copyright (C) 2007 Ken VanDine <ken@vandine.org>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import sys
from conary import conarycfg, conaryclient, queryrep, versions
from conary.conaryclient import cmdline


cfg = conarycfg.ConaryConfiguration()
client = conaryclient.ConaryClient(cfg)
cfg.readFiles()
cfg.initializeFlavors()
repos = client.getRepos()
db = conaryclient.ConaryClient(cfg).db

options = sys.argv[1]
searchterms = sys.argv[2]

try:
    localInstall = db.findTrove(None, (searchterms, None, None))
    installed = 1
except:
    installed = 0

troveSpecs = [ cmdline.parseTroveSpec(searchterms, allowEmptyName=False)]

try:
    troveTuple = repos.findTroves(cfg.installLabelPath, troveSpecs, cfg.flavor, getLeaves=True, acrossFlavors=False, acrossLabels=False, bestFlavor=True)
    for k,v in troveTuple.iteritems():
        name = v[0][0]
        version = str(v[0][1].trailingRevision())
        do_print = 0;
        if options == 'installed' and installed == 1:
            do_print = 1
        elif options == 'available' and installed == 0:
            do_print = 1
        elif options == 'all':
            do_print = 1
        # print in correct format
        if do_print == 1:
            print "package\t%s\t%s\t%s" % (installed, name, "")
except:
    pass
