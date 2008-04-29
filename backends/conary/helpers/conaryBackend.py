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

from conary import errors
from conary.deps import deps
from conary import conarycfg, conaryclient
from conary import dbstore, queryrep, versions, updatecmd
from conary.local import database
from conary import trove

from packagekit.backend import *
from conaryCallback import UpdateCallback

groupMap = {
    '2DGraphics'          : GROUP_GRAPHICS,
    'Accessibility'       : GROUP_ACCESSIBILITY,
    'AdvancedSettings'    : GROUP_ADMIN_TOOLS,
    'Application'         : GROUP_OTHER,
    'ArcadeGame'          : GROUP_GAMES,
    'Audio'               : GROUP_MULTIMEDIA,
    'AudioVideo'          : GROUP_MULTIMEDIA,
    'BlocksGame'          : GROUP_GAMES,
    'BoardGame'           : GROUP_GAMES,
    'Calculator'          : GROUP_ACCESSORIES,
    'Calendar'            : GROUP_ACCESSORIES,
    'CardGame'            : GROUP_GAMES,
    'Compiz'              : GROUP_SYSTEM,
    'ContactManagement'   : GROUP_ACCESSORIES,
    'Core'                : GROUP_SYSTEM,
    'Database'            : GROUP_SERVERS,
    'DesktopSettings'     : GROUP_ADMIN_TOOLS,
    'Development'         : GROUP_PROGRAMMING,
    'Email'               : GROUP_INTERNET,
    'FileTransfer'        : GROUP_INTERNET,
    'Filesystem'          : GROUP_SYSTEM,
    'GNOME'               : GROUP_DESKTOP_GNOME,
    'GTK'                 : GROUP_DESKTOP_GNOME,
    'GUIDesigner'         : GROUP_PROGRAMMING,
    'Game'                : GROUP_GAMES,
    'Graphics'            : GROUP_GRAPHICS,
    'HardwareSettings'    : GROUP_ADMIN_TOOLS,
    'IRCClient'           : GROUP_INTERNET,
    'InstantMessaging'    : GROUP_INTERNET,
    'LogicGame'           : GROUP_GAMES,
    'Monitor'             : GROUP_ADMIN_TOOLS,
    'Music'               : GROUP_MULTIMEDIA,
    'Network'             : GROUP_INTERNET,
    'News'                : GROUP_INTERNET,
    'Office'              : GROUP_OFFICE,
    'P2P'                 : GROUP_INTERNET,
    'PackageManager'      : GROUP_ADMIN_TOOLS,
    'Photography'         : GROUP_MULTIMEDIA,
    'Player'              : GROUP_MULTIMEDIA,
    'Presentation'        : GROUP_OFFICE,
    'Publishing'          : GROUP_OFFICE,
    'RasterGraphics'      : GROUP_GRAPHICS,
    'Security'            : GROUP_SECURITY,
    'Settings'            : GROUP_ADMIN_TOOLS,
    'Spreadsheet'         : GROUP_OFFICE,
    'System'              : GROUP_SYSTEM,
    'Telephony'           : GROUP_COMMUNICATION,
    'TerminalEmulator'    : GROUP_ACCESSORIES,
    'TextEditor'          : GROUP_ACCESSORIES,
    'Utility'             : GROUP_ACCESSORIES,
    'VectorGraphics'      : GROUP_GRAPHICS,
    'Video'               : GROUP_MULTIMEDIA,
    'Viewer'              : GROUP_MULTIMEDIA,
    'WebBrowser'          : GROUP_INTERNET,
    'WebDevelopment'      : GROUP_PROGRAMMING,
    'WordProcessor'       : GROUP_OFFICE
}

revGroupMap = {}

for (con_cat, pk_group) in groupMap.items():
    if revGroupMap.has_key(pk_group):
        revGroupMap[pk_group].append(con_cat)
    else:
        revGroupMap[pk_group] = [con_cat]
       
#from conary.lib import util
#sys.excepthook = util.genExcepthook()

def ExceptionHandler(func):
    def display(error):
        return str(error).replace('\n', ' ')

    def wrapper(self, *args, **kwargs):
        try:
            return func(self, *args, **kwargs)
        #except Exception:
        #    raise
        except conaryclient.NoNewTrovesError:
            return
        except conaryclient.DepResolutionFailure, e:
            self.error(ERROR_DEP_RESOLUTION_FAILED, display(e), exit=True)
        except conaryclient.UpdateError, e:
            # FIXME: Need a enum for UpdateError
            self.error(ERROR_UNKNOWN, display(e), exit=True)
        except Exception, e:
            self.error(ERROR_UNKNOWN, display(e), exit=True)
    return wrapper

class PackageKitConaryBackend(PackageKitBaseBackend):
    def __init__(self, args):
        PackageKitBaseBackend.__init__(self, args)

        self.cfg = conarycfg.ConaryConfiguration(True)
        self.cfg.initializeFlavors()
        self.cfg.autoResolve = True
        self.cfg.keepRequired = True

        self.client = conaryclient.ConaryClient(self.cfg)
        self.callback = UpdateCallback(self, self.cfg)
        self.client.setUpdateCallback(self.callback)

    def _freezeData(self, version, flavor):
        frzVersion = version.freeze()
        frzFlavor = flavor.freeze()
        return ','.join([frzVersion, frzFlavor])

    def _thawData(self, data):
        frzVersion, frzFlavor = data.split(',')
        version = versions.ThawVersion(frzVersion)
        flavor = deps.ThawFlavor(frzFlavor)
        return version, flavor

    def _get_arch(self, flavor):
        isdep = deps.InstructionSetDependency
        arches = [ x.name for x in flavor.iterDepsByClass(isdep) ]
        if not arches:
            arches = [ 'noarch' ]
        return ','.join(arches)

    @ExceptionHandler
    def get_package_id(self, name, versionObj, flavor):
        version = versionObj.trailingRevision()
        arch = self._get_arch(flavor)
        data = self._freezeData(versionObj, flavor)
        return PackageKitBaseBackend.get_package_id(self, name, version, arch,
                                                    data)

    @ExceptionHandler
    def get_package_from_id(self, id):
        name, verString, archString, data = \
            PackageKitBaseBackend.get_package_from_id(self, id)

        version, flavor = self._thawData(data)

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
            id = self.get_package_id(name, version, flavor)
            summary = self._get_metadata(id, 'shortDesc') or " "
            troveTuple = tuple([name, version, flavor])
            installed = self.check_installed(troveTuple)

            if self._do_filtering(name,fltlist,installed):
                self.package(id, installed, summary)

    def _get_update(self, applyList, cache=True):
        updJob = self.client.newUpdateJob()
        suggMap = self.client.prepareUpdateJob(updJob, applyList)
        if cache:
            Cache().cacheUpdateJob(applyList, updJob)
        return updJob, suggMap

    def _do_update(self, applyList):
        jobPath = Cache().checkCachedUpdateJob(applyList)
        if jobPath:
            updJob = self.client.newUpdateJob()
            try:
                updJob.thaw(jobPath)
            except IOError, err:
                updJob = None
        else:
            updJob = self._get_update(applyList, cache=False)
        self.allow_cancel(False)
        restartDir = self.client.applyUpdateJob(updJob)
        return updJob

    def _get_package_update(self, name, version, flavor):
        if name.startswith('-'):
            applyList = [(name, (version, flavor), (None, None), False)]
        else:
            applyList = [(name, (None, None), (version, flavor), True)]
        return self._get_update(applyList)

    def _do_package_update(self, name, version, flavor):
        if name.startswith('-'):
            applyList = [(name, (version, flavor), (None, None), False)]
        else:
            applyList = [(name, (None, None), (version, flavor), True)]
        return self._do_update(applyList)

    @ExceptionHandler
    def resolve(self, filter, package):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        self._do_search(package, filter)

    @ExceptionHandler
    def check_installed(self, troveTuple):
        db = conaryclient.ConaryClient(self.cfg).db
        try:
            troveTuple = troveTuple[0], troveTuple[1], troveTuple[2]
            localInstall = db.findTrove(None, troveTuple)
            installed = INFO_INSTALLED
        except:
            installed = INFO_AVAILABLE
        return installed

    @ExceptionHandler
    def search_group(self, filters, key):
        '''
        Implement the {backend}-search-name functionality
        FIXME: Ignoring filters for now.
        '''
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)

        fltlist = filters.split(';')
        cache = Cache()

        try:
            troveTupleList = cache.searchByGroups(revGroupMap[key])
        finally:
            # FIXME: Really need to send an error here
            pass

        troveTupleList.sort()
        troveTupleList.reverse()
        
        for troveTuple in troveTupleList:
            troveTuple = tuple([item.encode('UTF-8') for item in troveTuple[0:2]])
            name = troveTuple[0]
            version = versions.ThawVersion(troveTuple[1])
            flavor = deps.ThawFlavor(troveTuple[2])
            id = self.get_package_id(name, version, flavor)
            summary = self._get_metadata(id, 'shortDesc') or " "
            category = troveTuple[3][0]
            category = category.encode('UTF-8')
            troveTuple = tuple([name, version, flavor])
            installed = self.check_installed(troveTuple)
            
            if self._do_filtering(name,fltlist,installed):
                self.package(id, installed, summary)

    @ExceptionHandler
    def search_name(self, options, searchlist):
        '''
        Implement the {backend}-search-name functionality
        '''
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)

        self._do_search(searchlist, options)

    def search_details(self, opt, key):
        pass

    def get_requires(self, filters, package_id):
        pass

    @ExceptionHandler
    def get_depends(self, filters, package_id):
        name, version, flavor, installed = self._findPackage(package_id)

        if name:
            if installed == INFO_INSTALLED:
                self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                    'Package already installed')

            else:
                updJob, suggMap = self._get_package_update(name, version,
                                                           flavor)
                for what, need in suggMap:
                    id = self.get_package_id(need[0], need[1], need[2])
                    depInstalled = self.check_installed(need[0])
                    if depInstalled == INFO_INSTALLED:
                        self.package(id, INFO_INSTALLED, '')
                    else:
                        self.package(id, INFO_AVAILABLE, '')
        else:
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                'Package was not found')

    @ExceptionHandler
    def get_files(self, package_id):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        def _get_files(troveSource, n, v, f):
            files = []
            troves = [(n, v, f)]
            trv = troveSource.getTrove(n, v, f)
            troves.extend([ x for x in trv.iterTroveList(strongRefs=True)
                                if troveSource.hasTrove(*x)])
            for n, v, f in troves:
                for (pathId, path, fileId, version, file) in \
                    troveSource.iterFilesInTrove(n, v, f, sortByPath = True,
                                                 withFiles = True):
                    files.append(path)
            return files

        name, version, flavor, installed = self._findPackage(package_id)

        if installed == INFO_INSTALLED:
            files = _get_files(self.client.db, name, version, flavor)
        else:
            files = _get_files(self.client.repos, name, version, flavor)

        self.files(package_id, ';'.join(files))

    @ExceptionHandler
    def update_system(self):
        self.allow_cancel(True)
        updateItems = self.client.fullUpdateItemList()
        applyList = [ (x[0], (None, None), x[1:], True) for x in updateItems ]
        updJob, suggMap = self._do_update(applyList)

    @ExceptionHandler
    def refresh_cache(self):
        self.percentage()
        self.status(STATUS_REFRESH_CACHE)
        cache = Cache()
        cache.populate_database()

    @ExceptionHandler
    def update(self, packages):
        '''
        Implement the {backend}-update functionality
        '''
        self.allow_cancel(True);
        self.percentage(0)
        self.status(STATUS_RUNNING)

        for package in packages.split(" "):
            name, version, flavor, installed = self._findPackage(package)
            if name:
                self._do_package_update(name, version, flavor)
            else:
                self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                    'No available updates')

    @ExceptionHandler
    def install(self, package_id):
        '''
        Implement the {backend}-install functionality
        '''
        name, version, flavor, installed = self._findPackage(package_id)

        self.allow_cancel(True)
        self.percentage(0)
        self.status(STATUS_INSTALL)

        if name:
            if installed == INFO_INSTALLED:
                self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                    'Package already installed')

            self.status(STATUS_INSTALL)
            self._get_package_update(name, version, flavor)
            self._do_package_update(name, version, flavor)
        else:
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                'Package was not found')

    @ExceptionHandler
    def remove(self, allowDeps, package_id):
        '''
        Implement the {backend}-remove functionality
        '''
        name, version, flavor, installed = self._findPackage(package_id)

        self.allow_cancel(True)

        if name:
            if not installed == INFO_INSTALLED:
                self.error(ERROR_PACKAGE_NOT_INSTALLED,
                    'Package not installed')

            self.status(STATUS_REMOVE)
            name = '-%s' % name
            self._get_package_update(name, version, flavor)
            self._do_package_update(name, version, flavor)
        else:
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                'Package was not found')

    def _get_metadata(self, id, field):
        '''
        Retrieve metadata from the repository and return result
        field should be one of:
                bibliography
                url
                notes
                crypto
                licenses
                shortDesc
                longDesc
                categories
        '''

        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)

        n, v, f = self.get_package_from_id(id)

        trvList = self.client.repos.findTrove(self.cfg.installLabelPath,
                                     (n, v, f),
                                     defaultFlavor = self.cfg.flavor)

        troves = self.client.repos.getTroves(trvList, withFiles=False)
        result = ''
        for trove in troves:
            result = trove.getMetadata()[field]
        return result

    def _format_list(self,lst):
        """
        Convert a multi line string to a list separated by ';'
        """
        if lst:
            return ";".join(lst)
        else:
            return ""

    def _get_update_extras(self,id):
        notice = self._get_metadata(id, 'notice') or " "
        urls = {'jira':[], 'cve' : [], 'vendor': []}
        if notice:
            # Update Description
            desc = notice['description']
            # Update References (Jira,CVE ...)
            refs = notice['references']
            if refs:
                for ref in refs:
                    typ = ref['type']
                    href = ref['href']
                    title = ref['title']
                    if typ in ('jira','cve') and href != None:
                        if title == None:
                            title = ""
                        urls[typ].append("%s;%s" % (href,title))
                    else:
                        urls['vendor'].append("%s;%s" % (ref['href'],ref['title']))

            # Reboot flag
            if notice.get_metadata().has_key('reboot_suggested') and notice['reboot_suggested']:
                                reboot = 'system'
            else:
                reboot = 'none'
            return self._format_str(desc),urls,reboot
        else:
            return "",urls,"none"


    @ExceptionHandler
    def get_update_detail(self,id):
        '''
        Implement the {backend}-get-update_detail functionality
        '''
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        name, version, flavor, installed = self._findPackage(id)
        #update = self._get_updated(pkg)
        update = ""
        obsolete = ""
        #desc,urls,reboot = self._get_update_extras(id)
        #cve_url = self._format_list(urls['cve'])
        cve_url = ""
        #bz_url = self._format_list(urls['jira'])
        bz_url = ""
        #vendor_url = self._format_list(urls['vendor'])
        vendor_url = ""
        reboot = "none"
        desc = self._get_metadata(id, 'longDesc') or " "
        self.update_detail(id,update,obsolete,vendor_url,bz_url,cve_url,reboot,desc)

    @ExceptionHandler
    def get_description(self, id):
        '''
        Print a detailed description for a given package
        '''
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        name, version, flavor, installed = self._findPackage(id)

        if name:
            shortDesc = self._get_metadata(id, 'shortDesc') or name
            longDesc = self._get_metadata(id, 'longDesc') or ""
            url = "http://www.foresightlinux.org/packages/" + name + ".html"
            categories = self._get_metadata(id, 'categories') or "unknown"

            # Package size goes here, but I don't know how to find that for conary packages.
            self.description(shortDesc, id, categories, longDesc, url, 0)
        else:
            self.error(ERROR_PACKAGE_NOT_FOUND,'Package was not found')

    def _show_package(self,name, version, flavor, status):
        '''  Show info about package'''
        id = self.get_package_id(name, version, flavor)
        summary = self._get_metadata(id, 'shortDesc') or ""
        self.package(id, status, summary)

    def _get_status(self, notice):
        # We need to figure out how to get this info, this is a place holder
        #ut = notice['type']
        # TODO : Add more types to check
        #if ut == 'security':
        #    return INFO_SECURITY
        #else:
        #    return INFO_NORMAL
        return INFO_NORMAL

    @ExceptionHandler
    def get_updates(self, filter):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        updateItems = self.client.fullUpdateItemList()
        applyList = [ (x[0], (None, None), x[1:], True) for x in updateItems ]
        updJob, suggMap = self._get_update(applyList)

        jobLists = updJob.getJobs()

        totalJobs = len(jobLists)
        for num, job in enumerate(jobLists):
            status = '2'
            name = job[0][0]

            # On an erase display the old version/flavor information.
            version = job[0][2][0]
            if version is None:
                version = job[0][1][0]

            flavor = job[0][2][1]
            if flavor is None:
                flavor = job[0][1][1]

            troveTuple = []
            troveTuple.append(name)
            troveTuple.append(version)
            installed = self.check_installed(troveTuple)
            self._show_package(name, version, flavor, INFO_NORMAL)

    def _do_filtering(self, pkg, filterList, installed):
        ''' Filter the package, based on the filter in filterList '''
        # do we print to stdout?
        do_print = False;
        if filterList == ['none']: # 'none' = all packages.
            return True
        elif FILTER_INSTALLED in filterList and installed == INFO_INSTALLED:
            do_print = True
        elif FILTER_NOT_INSTALLED in filterList and installed == INFO_AVAILABLE:
            do_print = True

        if len(filterList) == 1: # Only one filter, return
            return do_print

        if do_print:
            return self._do_extra_filtering(pkg,filterList)
        else:
            return do_print

    def _do_extra_filtering(self, pkg, filterList):
        ''' do extra filtering (devel etc) '''

        for filter in filterList:
            if filter in (FILTER_INSTALLED, FILTER_NOT_INSTALLED):
                continue
            elif filter in (FILTER_DEVELOPMENT, FILTER_NOT_DEVELOPMENT):
                if not self._do_devel_filtering(flt,pkg):
                    return False
        return True

    def _do_devel_filtering(self, flt, pkg):
        isDevel = False
        if flt == FILTER_DEVELOPMENT:
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

    def repo_set_data(self, repoid, parameter, value):
        '''
        Implement the {backend}-repo-set-data functionality
        '''
        pass

    def get_repo_list(self, filters):
        '''
        Implement the {backend}-get-repo-list functionality
        '''
        pass

    def repo_enable(self, repoid, enable):
        '''
        Implement the {backend}-repo-enable functionality
        '''


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

    """ Class to retrieve and cache package information from label. """
    def __init__(self):
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
        backend = PackageKitBaseBackend(self)
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
            #print "Creating packages table..."
            # Create all tables if database is empty
            if len(tbllist) == 0:
                self._create_database()
                backend.status(STATUS_WAIT)
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
        for package in packages:
            self._insert(package)

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
