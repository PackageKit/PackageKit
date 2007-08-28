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

from conary.deps import deps
from conary.conaryclient import cmdline
from conary import conarycfg, conaryclient, queryrep, versions, updatecmd

from packagekit import *

class PackageKitConaryBackend(PackageKitBaseBackend):
    def __init__(self, args):
        PackageKitBaseBackend.__init__(self,args)
        self.cfg = conarycfg.ConaryConfiguration(True)
        self.cfg.initializeFlavors()

        self.client = conaryclient.ConaryClient(self.cfg)

    def _get_arch(self, flavor):
        isdep = deps.InstructionSetDependency
        arches = [ x.name for x in flavor.iterDepsByClass(isdep) ]
        if not arches:
            arches = [ 'noarch' ]
        return ','.join(arches)

    def _get_version(self, version):
        return version.asString()

    def get_package_id(self, name, version, flavor, fullVersion=None):
        version = self._get_version(version)
        arch = self._get_arch(flavor)
        return PackageKitBaseBackend.get_package_id(self, name, version,
                                                    arch, fullVersion)

    def _do_search(self,searchlist,filters):
        '''
        Search for conary packages
        @param searchlist: The conary package fields to search in
        @param options: package types to search (all,installed,available)
        '''
        repos = self.client.getRepos()
        db = conaryclient.ConaryClient(self.cfg).db
        affinityDb = self.client.db
        fltlist = filters.split(';')



        troveSpecs = [ cmdline.parseTroveSpec(searchlist, allowEmptyName=False)]

        try:
            # Look for packages with affinity
            troveTupleList = queryrep.getTrovesToDisplay(repos, troveSpecs,
                None, None, queryrep.VERSION_FILTER_LATEST,
                queryrep.FLAVOR_FILTER_BEST, self.cfg.installLabelPath,
                self.cfg.flavor, affinityDb)
            # Look for packages regardless of affinity
            troveTupleList.extend(queryrep.getTrovesToDisplay(repos, troveSpecs,
                None, None, queryrep.VERSION_FILTER_LATEST,
                queryrep.FLAVOR_FILTER_BEST, self.cfg.installLabelPath,
                self.cfg.flavor, None))
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
                version = troveTuple[1].trailingRevision()
                #version = troveTuple[1].trailingRevision().asString()
                # Hard code this until i get the flavor parsing right
                arch = "x86"
                fullVersion = troveTuple[1].asString()
                flavor = troveTuple[2]
                # We don't have summary data yet... so leave it blank for now
                summary = " "
                try:
                    localInstall = db.findTrove(None, troveTuple)
                    installed = 1
                except:
                    installed = 0

                if self._do_filtering(name,fltlist,installed):
                    id = self.get_package_id(name, version, flavor, fullVersion)
                    self.package(id, installed, summary)
        except:
            self.error('internal-error', 'An internal error has occurred')

    def search_name(self, options, searchlist):
        '''
        Implement the {backend}-search-name functionality
        '''
        self._do_search(searchlist, options)

    def search_details(self, opt, key):
        pass

    def get_deps(self, package_id):
        pass

    def update_system(self):
        pass

    def refresh_cache(self):
        pass

    def install(self, package_id):
        pass

    def remove(self, allowdep, package_id):
        pass

    def get_description(self, package_id):
        return ''

    def get_updates(self):
        updateItems = self.client.fullUpdateItemList()
        applyList = [ (x[0], (None, None), x[1:], True) for x in updateItems ]
        updJob = self.client.newUpdateJob()
        suggMap = self.client.prepareUpdateJob(updJob, applyList,
                                               resolveDeps=True,
                                               migrate=False)
        jobLists = updJob.getJobs()

        totalJobs = len(jobLists)
        for num, job in enumerate(jobLists):
            status = '2'

            name = job[0][0]
            version = job[0][2]
            flavor = job[0][3]

            id = self.get_package_id(name, version, flavor)
            summary = self._getdescription(id)

            print self.package(package_id, status, summary)

    def _do_filtering(self,pkg,filterList,installed):
        ''' Filter the package, based on the filter in filterList '''
        # do we print to stdout?
        do_print = False;
        if filterList == ['none']: # 'none' = all packages.
            return True
        elif 'installed' in filterList and installed == 1:
            do_print = True
        elif '~installed' in filterList and installed == 0:
            do_print = True

        if len(filterList) == 1: # Only one filter, return
            return do_print

        if do_print:
            return self._do_extra_filtering(pkg,filterList)
        else:
            return do_print

    def _do_extra_filtering(self,pkg,filterList):
        ''' do extra filtering (devel etc) '''

        for flt in filterList:
            if flt == 'installed' or flt =='~installed':
                continue
            elif flt =='devel' or flt=='~devel':
                if not self._do_devel_filtering(flt,pkg):
                    return False
        return True

    def _do_devel_filtering(self,flt,pkg):
        isDevel = False
        if flt == 'devel':
            wantDevel = True
        else:
            wantDevel = False
        #
        # TODO: Add Devel detection Code here.Set isDevel = True, if it is a devel app
        #
        regex =  re.compile(r'(:devel)')
        if regex.search(pkg.name):
            isDevel = True
        #
        #
        return isDevel == wantDevel
