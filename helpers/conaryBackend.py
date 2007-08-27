#
# Copyright (C) 2007 Ken VanDine <ken@vandine.org>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import sys
from packagekit import *
from conary import conarycfg, conaryclient, queryrep, versions, updatecmd
from conary.conaryclient import cmdline


class PackageKitConaryBackend(PackageKitBaseBackend):

    def __init__(self,args):
        PackageKitBaseBackend.__init__(self,args)

    def get_updates(self):
        cfg = conarycfg.ConaryConfiguration()
        client = conaryclient.ConaryClient(cfg)
        cfg.readFiles()
        cfg.initializeFlavors()
        updateItems = client.fullUpdateItemList()
        applyList = [ (x[0], (None, None), x[1:], True) for x in updateItems ]
        updJob = client.newUpdateJob()
        suggMap = client.prepareUpdateJob(updJob, applyList, resolveDeps=True, migrate=False)
        jobLists = updJob.getJobs()

        totalJobs = len(jobLists)
        for num, job in enumerate(jobLists):
            status = "2"
            summary = ""
            name = job[0][0]
            version = job[0][2]
            # package;version;arch;data
            package_id = job[0][0] + ";" + "" + ";;"
            #package_id = job[0][0] + ";" + job[0][2].trailingRevision().asString() + ";;"
            print "package\t%s\t%s\t%s" % (status, package_id, summary)

    def _do_search(self,searchlist,options):
        '''
        Search for conary packages
        @param searchlist: The conary package fields to search in
        @param options: package types to search (all,installed,available)
        '''
        cfg = conarycfg.ConaryConfiguration()
        client = conaryclient.ConaryClient(cfg)
        cfg.readFiles()
        cfg.initializeFlavors()
        repos = client.getRepos()
        db = conaryclient.ConaryClient(cfg).db
        affinityDb = client.db

        try:
            localInstall = db.findTrove(None, (searchlist, None, None))
            installed = 1
        except:
            installed = 0

        troveSpecs = [ cmdline.parseTroveSpec(searchlist, allowEmptyName=False)]

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
                    id = self.get_package_id(name, version, arch, data)
                    self.package(id,installed, summary)
                    #print "package\t%s\t%s\t%s" % (installed, package_id, summary)
        except:
            sys.stderr.write('error\tinternal-error\tAn internal error has occurred')

    def search_name(self, options, searchlist):
        '''
        Implement the {backend}-search-name functionality
        '''
        self._do_search(searchlist, options)
