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
#
# Copyright (C) 2007 Ken VanDine <ken@vandine.org>
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>

import os
from conary import errors
from conary.deps import deps
from conary import conarycfg, conaryclient
from conary import dbstore, queryrep, versions, updatecmd
from conary.local import database
from conary import trove



from pkConaryLog import log

class Cache(object):
    # Database name and path
    dbName = 'cache.db'
    # Someday we might want to make this writable by users
    #if 'HOME' in os.environ:
    #    dbPath = '%s/.conary/cache/data/' % os.environ['HOME']
    #else:
    #    dbPath = '/var/cache/conary/'
    dbPath = '/var/cache/conary/'
    jobPath = dbPath + 'jobs'

    def __init__(self):
        """ Class to retrieve and cache package information from label. """

        self.is_populate_database = False
        if not os.path.isdir(self.dbPath):
            os.makedirs(self.dbPath)
        if not os.path.isdir(self.jobPath):
            os.mkdir(self.jobPath)

        self.conn = dbstore.connect(os.path.join(self.dbPath, self.dbName))
        self.cursor = self.conn.cursor()
        self.cursor.execute("PRAGMA count_changes=0", start_transaction=False)

        if os.path.isfile(os.path.join(self.dbPath, self.dbName)):
            self._validate_tables()

    def _validate_tables(self):
        """ Validates that all tables are up to date. """
        #backend = PackageKitBaseBackend(self)
        stmt = ("select tbl_name from sqlite_master "
                "where type = 'table' and tbl_name like 'conary_%'")
        self.cursor.execute(stmt)
        # List of all tables with names that start with "conary_"
        tbllist = self.cursor.fetchall()
        if tbllist == [('conary_packages',)]:
            self.cursor.execute('DROP TABLE conary_packages')
            self.conn.commit()
            tbllist = []
        if tbllist != []:
            return True
            #print "Verified packages table"
        else:
            log.info("Creando tablas")
            # Create all tables if database is empty
            if len(tbllist) == 0:
                self._create_database()
                #ackend.status(STATUS_WAIT)
                self.is_populate_database = True
                self.populate_database()
                return True

    def _getJobCachePath(self, applyList):
        from conary.lib import sha1helper
        applyStr = '\0'.join(['%s=%s[%s]--%s[%s]%s' % (x[0], x[1][0], x[1][1], x[2][0], x[2][1], x[3]) for x in applyList])
        return self.jobPath + '/' + sha1helper.sha1ToString(sha1helper.sha1String(applyStr))

    def checkCachedUpdateJob(self, applyList):
        jobPath = self._getJobCachePath(applyList)
        if os.path.exists(jobPath):
            return jobPath

    def cacheUpdateJob(self, applyList, updJob):
        jobPath = self._getJobCachePath(applyList)
        if os.path.exists(jobPath):
            from conary.lib import util
            util.rmtree(jobPath)
        os.mkdir(jobPath)
        updJob.freeze(jobPath)

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
        #FIXME: delete the category column. it's not useful
        """ Creates a blank database. """
        sql = '''CREATE TABLE conary_packages (
            packageId INTEGER,
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

        sql = '''CREATE TABLE conary_categories (
            categoryId INTEGER,
            categoryName text)'''

        self.cursor.execute(sql)

        sql = '''CREATE TABLE conary_category_package_map (
            categoryId INTEGER,
            packageId INTEGER)'''

        self.cursor.execute(sql)

        sql = '''CREATE TABLE conary_licenses (
            licenseId INTEGER,
            licenseName text)'''

        self.cursor.execute(sql)

        sql = '''CREATE TABLE conary_license_package_map (
            licenseId INTEGER,
            packageId INTEGER)'''

        self.cursor.execute(sql)

        #self.conn.createIndex('conary_catagories', 'conary_category_name_idx', ['categoryName'])
        #self.conn.createIndex('conary_catagories', 'conary_category_id_idx', ['categoryId'])
        self.conn.commit()



    def commit(self):
        self.cursor.commit()

    def getTroves(self, label=None):
        """
        Returns all troves for now.  Add filtering capability.
        """
        stmt = ("select distinct trove, version, flavor, description, "
                "category, packagegroup, size from conary_packages")

        self.cursor.execute(stmt)
        return self.cursor.fetchall()

    def search(self, package, fullVersion=None):
        """
        Returns all troves for now.  Add filtering capability.
        """
        #log.debug(package)
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
            log.debug(results)
            return results
        except Exception, e:
            print str(e)
            return None

    def searchByGroups(self, groups):
        """
        Returns all troves for given groups. (trove, version, flavor)
        Needs filtering capability.
        ['all'] means all packages
        FIXME: No filtering done on group text - SQL injection
        """
        if not groups:
            groups = ["all"]

        if "all" in groups:
            stmt = ("SELECT DISTINCT CP.trove, CP.version, CP.flavor, CC.categoryName"
                    "           FROM conary_packages CP, conary_categories CC, conary_category_package_map CCMap"
                    "          WHERE CCMap.packageId = CP.packageId"
                    "            AND CCMap.categoryId = CC.categoryId"
                    "       GROUP BY CP.trove, CP.version, CP.flavor"
                    "       ORDER BY CP.trove, CP.version DESC, CP.flavor")
        else:
            group_string = ", ".join(groups)
            stmt = ("SELECT DISTINCT CP.trove, CP.version, CP.flavor, CC.categoryName"
                    "           FROM conary_packages CP, conary_categories CC, conary_category_package_map CCMap"
                    "          WHERE CC.categoryName IN (%s)"
                    "            AND CCMap.packageId = CP.packageId"
                    "            AND CCMap.categoryId = CC.categoryId"
                    "       GROUP BY CP.trove, CP.version, CP.flavor"
                    "       ORDER BY CP.trove, CP.version DESC, CP.flavor" % group_string)

        try:
            self.cursor.execute(stmt)
            return self.cursor.fetchall()
        except Exception, e:
            print str(e)
            return None

    def _insert(self, trove):
        """
        Insert trove into database.
        """
        res = self.cursor.execute("SELECT COALESCE(max(packageId), 0) + 1 FROM conary_packages")
        pkgId = res.fetchone()[0] + 1
        trove = [pkgId] + trove[:]

        values = [str(field) for field in trove]
        cols = ", ".join("?" * len(trove))
        sql = "INSERT INTO conary_packages VALUES (%s)" % cols

        try:
            self.cursor.execute(sql, values)
            #self.conn.commit()
        except Exception, e:
            print str(e)

    def _clear_table(self, tableName='conary_packages'):
        """
        Deletes * records from table.
        """
        stmt = "DELETE FROM %s" % tableName
        try:
            self.cursor.execute(stmt)
        except dbstore.sqlerrors.InvalidTable:
            pass

    def populate_database(self):
        packages = self.conaryquery()
        # Clear table first
        for tblName in ('conary_packages', 'conary_category_package_map',
                'conary_categories'):
            self._clear_table(tblName)
        log.info("Insertando datos")
        for package in packages:
            self._insert(package)
        self.conn.commit()
        log.info("Datos insertados")

    def _addPackageCategory(self, trv, category):
        res = self.cursor.execute( \
                'SELECT packageId FROM conary_packages WHERE trove=? and version=? and flavor = ?', trv.getName(), trv.getVersion().freeze(), trv.getFlavor().freeze())
        res = res.fetchone()
        if res:
            # we have a packageID
            pkgId = res[0]
        else:
            # we really should have had this data
            raise RuntimeError

        # now look up/make the categoryId
        res = self.cursor.execute('SELECT categoryId FROM conary_categories WHERE categoryName=?', category)
        res = res.fetchone()
        if not res:
            res = self.cursor.execute('SELECT COALESCE(MAX(categoryId), 0) + 1 FROM conary_categories')
            catId = res.fetchone()[0]
            self.cursor.execute('INSERT INTO conary_categories VALUES(?, ?)',
                    catId, category)
        else:
            catId = category

        self.cursor.execute("INSERT INTO conary_category_package_map VALUES(?, ?)", catId, pkgId)
        self.conn.commit()

    def populate_metadata(self, csList):
        for cs in csList:
            for troveCS in cs.iterNewTroveList():
                trv = trove.Trove(troveCS)
                if ':' in trv.getName():
                    # components aren't tracked at the moment
                    continue
                metadata = trv.getMetadata()
                categories = metadata.get('categories', [])
                for category in categories:
                    self._addPackageCategory(trv, category)
                #licenses = metadata.get('licenses', [])
                #for license in licenses:
                #    self._addPackageLicense(trv, license)
