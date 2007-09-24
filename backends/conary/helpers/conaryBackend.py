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
import os

from conary.deps import deps
from conary import conarycfg, conaryclient
from conary import dbstore, queryrep, versions, updatecmd

from packagekit import *
from conaryCallback import UpdateCallback

class PackageKitConaryBackend(PackageKitBaseBackend):
    def __init__(self, args):
        PackageKitBaseBackend.__init__(self, args)
        self.cfg = conarycfg.ConaryConfiguration(True)
        self.cfg.initializeFlavors()
        self.client = conaryclient.ConaryClient(self.cfg)
        self.callback = UpdateCallback(self, self.cfg)
        self.client.setUpdateCallback(self.callback)

    def _get_arch(self, flavor):
        isdep = deps.InstructionSetDependency
        arches = [ x.name for x in flavor.iterDepsByClass(isdep) ]
        if not arches:
            arches = [ 'noarch' ]
        return ','.join(arches)

    def get_package_id(self, name, versionObj, flavor=None):
        version = versionObj.trailingRevision()
        fullVersion = versionObj.asString()
        if flavor is not None and flavor != 'noarch':
            arch = self._get_arch(flavor)
        else:
            arch = ""
        return PackageKitBaseBackend.get_package_id(self, name, version, arch,
                                                    fullVersion)

    def get_package_from_id(self, id):
        name, verString, archString, fullVerString = \
            PackageKitBaseBackend.get_package_from_id(self, id)

        if verString:
            version = versions.VersionFromString(fullVerString)
        else:
            version = None

        if archString:
            arches = 'is: %s' %  ' '.join(archString.split(','))
            flavor = deps.parseFlavor(arches)
        else:
            flavor = None

        return name, version, flavor

    def _do_search(self, searchlist, filters):
        fltlist = filters.split(';')
        troveSpecs = [ updatecmd.parseTroveSpec(searchlist,
                                                allowEmptyName=False) ]
        # get a hold of cached data
        cache = Cache()

        try:
            troveTupleList = cache.search(searchlist)
        finally:
            pass

        # Remove dupes
        tempDict = {}
        try:
            for element in troveTupleList:
                tempDict[element] = None
        except TypeError:
            del tempDict  # move on to the next method
        else:
            troveTupleList = tempDict.keys()

        # Get the latest first
        troveTupleList.sort()
        troveTupleList.reverse()

        for troveTuple in troveTupleList:
            troveTuple = tuple([item.encode('UTF-8') for item in troveTuple])
            name = troveTuple[0]
            version = versions.ThawVersion(troveTuple[1])
            flavor = deps.ThawFlavor(troveTuple[2])
            # We don't have summary data yet... so leave it blank for now
            summary = " "
            troveTuple = tuple([name, version, flavor])
            installed = self.check_installed(troveTuple)

            if self._do_filtering(name,fltlist,installed):
                id = self.get_package_id(name, version, flavor)
                self.package(id, installed, summary)

    def _do_search_live(self, searchlist, filters):
        '''
        Search for conary packages
        @param searchlist: The conary package fields to search in
        @param options: package types to search (all,installed,available)
        '''
        repos = self.client.getRepos()
        db = conaryclient.ConaryClient(self.cfg).db
        affinityDb = self.client.db
        fltlist = filters.split(';')

        troveSpecs = [ updatecmd.parseTroveSpec(searchlist,
                                                allowEmptyName=False) ]

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
            try:
                for element in troveTupleList:
                    tempDict[element] = None
            except TypeError:
                del tempDict  # move on to the next method
            else:
                troveTupleList = tempDict.keys()

            # Get the latest first
            troveTupleList.sort()
            troveTupleList.reverse()

            for troveTuple in troveTupleList:
                name = troveTuple[0]
                version = troveTuple[1]
                flavor = troveTuple[2]
                # We don't have summary data yet... so leave it blank for now
                summary = " "
                installed = self.check_installed(troveTuple)

                if self._do_filtering(name,fltlist,installed):
                    id = self.get_package_id(name, version, flavor)
                    self.package(id, installed, summary)
        except:
            self.error(ERROR_INTERNAL_ERROR, 'An internal error has occurred')

    def _do_update(self, applyList, apply=False):
        self.cfg.autoResolve = True

        updJob = self.client.newUpdateJob()
        suggMap = self.client.prepareUpdateJob(updJob, applyList)

        if apply:
            restartDir = self.client.applyUpdateJob(updJob)

        return updJob, suggMap

    def _do_package_update(self, name, version, flavor, apply=False):
        if name.startswith('-'):
            applyList = [(name, (version, flavor), (None, None), False)]
        else:
            applyList = [(name, (None, None), (version, flavor), False)]
        updJob, suggMap = self._do_update(applyList, apply=apply)
        return updJob, suggMap

    def check_installed(self, troveTuple):
        db = conaryclient.ConaryClient(self.cfg).db
        try:
            troveTuple = troveTuple[0], troveTuple[1], troveTuple[2]
            localInstall = db.findTrove(None, troveTuple)
            installed = 1
        except:
            installed = 0
        return installed

    def search_name(self, options, searchlist):
        '''
        Implement the {backend}-search-name functionality
        '''
        self._do_search(searchlist, options)

    def search_name_live(self, options, searchlist):
        '''
        Implement the {backend}-search-name-live functionality
        '''
        self._do_search_live(searchlist, options)

    def search_details(self, opt, key):
        pass

    def get_depends(self, package_id):
        pass

    def get_requires(self, package_id):
        name, version, flavor, installed = self._findPackage(package_id)

        if name:
            if installed:
                self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                    'Package already installed')

            updJob, suggMap = self._do_pacakge_update(name, version, flavor,
                                                      apply=False)

            for what, need in suggMap:
                id = self.get_package_id(need[0], need[1], need[2])
                self.package(id, False, '')
        else:
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                'Package was not found')

    def update_system(self):
        updateItems = self.client.fullUpdateItemList()
        applyList = [ (x[0], (None, None), x[1:], True) for x in updateItems ]
        updJob, suggMap = self._do_update(applyList, apply=True)

    def refresh_cache(self):
        self.percentage()
        cache = Cache()
        cache.populate_database()

    def install(self, package_id):
        '''
        Implement the {backend}-install functionality
        '''
        name, version, flavor, installed = self._findPackage(package_id)

        if name:
            if installed:
                self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                    'Package already installed')
            try:
                self.status(STATE_INSTALL)
                self._do_package_update(name, version, flavor, apply=True)
            except:
                pass
        else:
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                'Package was not found')

    def remove(self, allowDeps, package_id):
        '''
        Implement the {backend}-remove functionality
        '''
        name, version, flavor, installed = self._findPackage(package_id)

        if name:
            if not installed:
                self.error(ERROR_PACKAGE_NOT_INSTALLED,
                    'Package not installed')
            try:
                self.status(STATE_REMOVE)
                name = '-%s' % name
                self._do_package_update(name, version, flavor, apply=True)
            except:
                pass
        else:
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                'Package was not found')

    def get_description(self, package_id):
        '''
        Print a detailed description for a given package
        '''
        name, version, flavor, installed = self._findPackage(package_id)

        if name:
            id = self.get_package_id(name, version, flavor)
            desc = ""
            desc += "%s \n" % name
            desc += "%s \n" % version.trailingRevision()
            desc = desc.replace('\n\n',';')
            desc = desc.replace('\n',' ')
            detail = ""
            url = "http://www.foresightlinux.org/packages/" + name + ".html"
            group = "other"
            self.description(desc, id, group, detail, url)
        else:
            self.error(ERROR_INTERNAL_ERROR,'Package was not found')

    def get_updates(self):
        self.percentage()
        updateItems = self.client.fullUpdateItemList()
        applyList = [ (x[0], (None, None), x[1:], True) for x in updateItems ]
        updJob, suggMap = self._do_update(applyList, apply=False)

        jobLists = updJob.getJobs()

        totalJobs = len(jobLists)
        for num, job in enumerate(jobLists):
            status = '2'
            name = job[0][0]
            version = job[0][2][0]
            flavor = job[0][2][1]
            troveTuple = []
            troveTuple.append(name)
            troveTuple.append(version)
            installed = self.check_installed(troveTuple)
            id = self.get_package_id(name, version, flavor)
            summary = ""
            self.package(id, installed, summary)

    def _do_filtering(self, pkg, filterList, installed):
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

    def _do_extra_filtering(self, pkg, filterList):
        ''' do extra filtering (devel etc) '''

        for flt in filterList:
            if flt == 'installed' or flt =='~installed':
                continue
            elif flt =='devel' or flt=='~devel':
                if not self._do_devel_filtering(flt,pkg):
                    return False
        return True

    def _do_devel_filtering(self, flt, pkg):
        isDevel = False
        if flt == 'devel':
            wantDevel = True
        else:
            wantDevel = False
        #
        # TODO: Add Devel detection Code here.Set isDevel = True, if it is a
        #       devel app.
        #
        regex =  re.compile(r'(:devel)')
        if regex.search(pkg.name):
            isDevel = True
        #
        #
        return isDevel == wantDevel

    def _findPackage(self, id):
        '''
        find a package based on a package id (name;version;arch;repoid)
        '''
        name, version, flavor = self.get_package_from_id(id)
        troveTuple = (name, version, flavor)
        installed = self.check_installed(troveTuple)
        return name, version, flavor, installed


class Cache(object):
    # Database name and path
    dbName = 'cache.db'
    # Someday we might want to make this writable by users
    #if 'HOME' in os.environ:
    #    dbPath = '%s/.conary/cache/data/' % os.environ['HOME']
    #else:
    #    dbPath = '/var/cache/conary/'
    dbPath = '/var/cache/conary/'

    """ Class to retrieve and cache package information from label. """
    def __init__(self):
        if not os.path.isdir(self.dbPath):
            os.makedirs(self.dbPath)

        self.conn = dbstore.connect(os.path.join(self.dbPath, self.dbName))
        self.cursor = self.conn.cursor()
        self.cursor.execute("PRAGMA count_changes=0", start_transaction=False)

        if os.path.isfile(os.path.join(self.dbPath, self.dbName)):
            self._validate_tables()

    def _validate_tables(self):
        """ Validates that all tables are up to date. """
        stmt = ("select tbl_name from sqlite_master "
                "where type = 'table' and tbl_name like 'conary_%'")
        self.cursor.execute(stmt)
        # List of all tables with names that start with "conary_"
        tbllist = self.cursor.fetchall()
        if tbllist != []:
            return True
            #print "Verified packages table"
        else:
            #print "Creating packages table..."
            # Create all tables if database is empty
            if len(tbllist) == 0:
                self._create_database()
                return True

    def conaryquery(self):
        self.cfg = conarycfg.ConaryConfiguration()
        self.client = conaryclient.ConaryClient(self.cfg)
        self.cfg.readFiles()
        self.cfg.initializeFlavors()
        self.repos = self.client.getRepos()
        self.db = conaryclient.ConaryClient(self.cfg).db

        troves = queryrep.getTrovesToDisplay(self.repos, None, None, None,
            queryrep.VERSION_FILTER_LEAVES, queryrep.FLAVOR_FILTER_BEST, 
            self.cfg.installLabelPath, self.cfg.flavor, None)

        packages = []

        for troveTuple in troves:
            # troveTuple is probably what we want to store in the cachedb
            # Then use the below methods to present them in a nicer fashion
            if troveTuple[0].endswith(':source'):
                continue
            if ":" in troveTuple[0]:
                fragments = troveTuple[0].split(":")
                trove = fragments[0]
                component = fragments[1]
            else:
                trove = troveTuple[0]
                component = ""

            installed = 0
            flavor = troveTuple[2].freeze()
            fullVersion = troveTuple[1].freeze()
            label = str(troveTuple[1].branch().label())
            description = ""
            category = ""
            packagegroup = ""
            size = ""
            packages.append([trove, component, fullVersion, label, flavor,
                             description, category, packagegroup, size])

        return packages

    def connect_memory(self):
        return sqlite.connect(':memory:')

    def cursor(self, connection):
        return connection.cursor()

    def _create_database(self):
        """ Creates a blank database. """
        sql = '''CREATE TABLE conary_packages (
            trove text,
            component text,
            version text,
            label text,
            flavor text,
            description text,
            category text,
            packagegroup text,
            size text)'''

        self.cursor.execute(sql)

    def commit(self):
        self.cursor.commit()

    def getTroves(self, label=None):
        """
        Returns all troves for now.  Add filtering capability.
        """
        stmt = ("select distinct trove, version, flavor, description, "
                "category, packagegroup, size from conary_packages")

        try:
            self.cursor.execute(stmt)
            return self.cursor.fetchall()
        except Exception, e:
            print str(e)
            return None

    def search(self, package, fullVersion=None):
        """
        Returns all troves for now.  Add filtering capability.
        """
        stmt = ("select distinct trove, version, flavor, description, "
                "category, packagegroup, size from conary_packages")

        if package and fullVersion:
            stmt = ("select distinct trove, version, flavor from "
                    "conary_packages where trove ='%s' and version = '%s'"
                    % (package, fullVersion))
        elif package:
            stmt += (" where trove like '%%%s%%' and component = '' order by "
                     "version desc" % package)

        try:
            self.cursor.execute(stmt)
            results = self.cursor.fetchall()
            return results
        except Exception, e:
            print str(e)
            return None

    def _insert(self, trove):
        """
        Insert trove into database.
        """
        values = [str(field) for field in trove]
        cols = ",".join("?" * len(trove))
        sql = "INSERT INTO conary_packages VALUES (%s)" % cols

        try:
            self.cursor.execute(sql, values)
            self.conn.commit()
        except Exception,e:
            print str(e)

    def _clear_table(self, tableName='conary_packages'):
        """
        Deletes * records from table.
        """
        stmt = "DELETE FROM %s" % tableName
        self.cursor.execute(stmt)

    def populate_database(self):
        try:
            packages = self.conaryquery()
            # Clear table first
            self._clear_table()
            for package in packages:
                self._insert(package)
        except Exception, e:
            print str(e)
