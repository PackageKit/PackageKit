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
affinityDb = client.db

options = sys.argv[1]
searchterms = sys.argv[2]

sys.stderr.write('no-percentage-updates\n')

try:
    localInstall = db.findTrove(None, (searchterms, None, None))
    installed = 1
except:
    installed = 0

troveSpecs = [ cmdline.parseTroveSpec(searchterms, allowEmptyName=False)]

try:
    # Look for packages with affinity
    troveTupleList = queryrep.getTrovesToDisplay(repos, troveSpecs,
        None, None, queryrep.VERSION_FILTER_LATEST,
        queryrep.FLAVOR_FILTER_BEST, cfg.installLabelPath,
        cfg.flavor, affinityDb)
    # Look for packages regardless of affinity
    troveTupleList.extend(queryrep.getTrovesToDisplay(repos, troveSpecs,
        None, None, queryrep.VERSION_FILTER_LATEST,
        queryrep.FLAVOR_FILTER_BEST, cfg.installLabelPath,
        cfg.flavor, None))
    # Remove dupes
    tempDict = {}
    for element in troveTupleList:
        tempDict[element] = None
        troveTupleList = tempDict.keys()

    # Get the latest first
    troveTupleList.sort()
    troveTupleList.reverse()

    for troveTuple in troveTupleList:
        name = troveTuple[0]
        version = troveTuple[1].trailingRevision().asString()
        # Hard code this until i get the flavor parsing right
        arch = "x86"
        fullVersion = troveTuple[1].asString()
        flavor = str(troveTuple[2])
        data = fullVersion + " " + flavor
        # We don't have summary data yet... so leave it blank for now
        summary = " "
        package_id = name + ";" + version + ";" + arch + ";" + data
        do_print = 0;
        if options == 'installed' and installed == 1:
            do_print = 1
        elif options == 'available' and installed == 0:
            do_print = 1
        elif options == 'all':
            do_print = 1
        # print in correct format
        if do_print == 1:
            print "package\t%s\t%s\t%s" % (installed, package_id, summary)
except:
    sys.stderr.write('error\tinternal-error\tAn internal error has occurred')
