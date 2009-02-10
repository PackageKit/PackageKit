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
# Copyright (C) 2008 Andres Vargas <zodman@foresightlinux.org>

import sys
import os
import re

from conary import errors
from conary.deps import deps
from conary import dbstore, queryrep, versions, updatecmd
from conary.local import database
from conary import trove
from conary.conaryclient import cmdline

from packagekit.backend import *
from packagekit.package import *
from conaryCallback import UpdateCallback
from conaryFilter import *
from XMLCache import XMLCache as Cache
from conaryInit import *

# zodman fix
#from Cache import Cache
from conaryInit import init_conary_config, init_conary_client
from conary import conarycfg, conaryclient
from conarypk import ConaryPk

pkpackage = PackagekitPackage()

from pkConaryLog import log, pdb

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
    'WordProcessor'       : GROUP_OFFICE,
    ''                    : GROUP_UNKNOWN
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

def _format_str(str):
    """
    Convert a multi line string to a list separated by ';'
    """
    if str:
        lines = str.split('\n')
        return ";".join(lines)
    else:
        return ""

def _format_list(lst):
    """
    Convert a multi line string to a list separated by ';'
    """
    if lst:
        return ";".join(lst)
    else:
        return ""

class PackageKitConaryBackend(PackageKitBaseBackend):
    # Packages there require a reboot
    rebootpkgs = ("kernel", "glibc", "hal", "dbus")

    def __init__(self, args):
        PackageKitBaseBackend.__init__(self, args)

        # conary configurations
        self.cfg = init_conary_config()
        self.client = init_conary_client()
        self.callback = UpdateCallback(self, self.cfg)
        self.client.setUpdateCallback(self.callback)

    def _freezeData(self, version, flavor):
        frzVersion = version.freeze()
        frzFlavor = flavor.freeze()
        return ','.join([frzVersion, frzFlavor])

    def _thawData(self, frzVersion, frzFlavor ):
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
    def check_installed(self, troveTuple):
        log.debug("============check installed =========")
        cli = ConaryPk()
        result = cli.query(troveTuple[0])
        if result:
            installed = INFO_INSTALLED
        else:
            installed = INFO_AVAILABLE

        return installed
           
    @ExceptionHandler
    def get_package_id(self, name, versionObj, flavor):

        version = versionObj.trailingRevision()

        arch = self._get_arch(flavor)

        cache = Cache()
        pkg  = cache.resolve(name)
        data = versionObj.asString() + "#"
        if pkg:
            try:
                data +=str(pkg)
            except:
                pass
        return pkpackage.get_package_id(name, version, arch, data)

    @ExceptionHandler
    def get_package_from_id(self, package_id):
        """ package_id(string) =
        "dpaster;0.1-3-1;x86;/foresight.rpath.org@fl:2-qa/0.1-3-1#{'version': '0.1-3-1', 'categorie': [], 'name': 'dpaster', 'label': 'foresight.rpath.org@fl:2-qa'}"
        """
        log.info("=========== get package from package_id ======================")
        name, verString, archString, data =  pkpackage.get_package_from_id(package_id)
        summary = data.split("#")
        repo = summary[0]
        metadata = eval(summary[1])
        cli = ConaryPk()
        return  cli.request_query(name)

    def _do_search(self,filters, searchlist):
        """
         searchlist(str)ist as the package for search like
         filters(str) as the filter
        """
        fltlist = filters.split(';')

        cache = Cache()
        log.debug((searchlist, fltlist))

        troveTupleList = cache.search(searchlist)

        if troveTupleList:
            log.info("FOUND!!!!!! %s " % troveTupleList )
        else:
            log.info("NOT FOUND %s " % searchlist )

        for troveDict in troveTupleList:
            log.info(" doing resolve ")
            log.info(troveDict)
            self.resolve( fltlist[0] , [ troveDict['name'] ] )


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

    def _convertPackage(self, pkg ):
       #version = versions.ThawVersion(pkg['version'])
       pass
    @ExceptionHandler
    def resolve(self, filters, package ):
        """ 
            @filters  (list)  list of filters
            @package (list ) list with packages name for resolve
        """
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        log.info("======== resolve =========")
        log.info("filters: %s package:%s " % (filters, package))

        cache = Cache()
        pkg_dict = cache.resolve( package[0] )

        if pkg_dict is None:
            self.error(ERROR_INTERNAL_ERROR, "Package Not found on repository")

        filter = ConaryFilter(filters)

        installed = filter._pkg_is_installed( pkg_dict )
        
        conary_cli = ConaryPk()

        troveTuple =  conary_cli.request_query( package[0] )

        log.info(">>> %s" % troveTuple)

        if installed:
            filter.add_installed( troveTuple  )
        else:
            filter.add_available( troveTuple )

        package_list = filter.post_process()
        log.info("package_list %s" % package_list)
        self._show_package_list(package_list)

        #if len(packages):
        #    for i in packages:
        #        self._do_search(i, filters)
        #else:
        #    self._do_search(packages, filters)

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
        pkgfilter = ConaryFilter(fltlist)
        pkgfilter = ConaryFilter(fltlist)
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
            category = troveTuple[3][0]
            category = category.encode('UTF-8')
            troveTuple = tuple([name, version, flavor])
            installed = self.check_installed(troveTuple)
            if installed:
                pkgfilter.add_installed([troveTuple])
            else:
                pkgfilter.add_available([troveTuple])

        # we couldn't do this when generating the list
        package_list = pkgfilter.post_process()
        log.info(package_list)
        log.info("package_list %s" % package_list)
        self._show_package_list(package_list)

    def _show_package_list(self, lst):
        """ 
            HOW its showed on packageKit
            @lst(list(tuple) = [ ( troveTuple, status ) ]
        """
        for troveTuple, status in lst:
            # take the basic info
            name = troveTuple[0]
            version = troveTuple[1]
            flavor = troveTuple[2]
            # get the string id from packagekit 
            package_id = self.get_package_id(name, version, flavor)
            
            # split the list for get Determine info
            summary = package_id.split(";")
            repo = summary[3].split("#")[0]

            metadata = eval(summary[3].split("#")[1])
            log.info("====== show the package ")
            log.info(metadata)
            if metadata.has_key("shortDesc"):
                meta = metadata["shortDesc"]
            else:
                meta = " "
            self.package(package_id, status, meta )

    @ExceptionHandler
    def search_name(self, options, searchlist):
        '''
        Implement the {backend}-search-name functionality
        '''
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)
        log.info("============= search_name ========")
        log.info("options: %s searchlist:%s "%(options, searchlist))
        self._do_search(options, searchlist)

    def search_details(self, opt, key):
        pass

    def get_requires(self, filters, package_ids, recursive_text):
        pass

    @ExceptionHandler
    def get_depends(self, filters, package_ids, recursive_text):
        name, version, flavor, installed = self._findPackage(package_ids[0])

        if name:
            if installed == INFO_INSTALLED:
                self.error(ERROR_PACKAGE_ALREADY_INSTALLED, 'Package already installed')

            else:
                updJob, suggMap = self._get_package_update(name, version,
                                                           flavor)
                for what, need in suggMap:
                    package_id = self.get_package_id(need[0], need[1], need[2])
                    depInstalled = self.check_installed(need[0])
                    if depInstalled == INFO_INSTALLED:
                        self.package(package_id, INFO_INSTALLED, '')
                    else:
                        self.package(package_id, INFO_AVAILABLE, '')
        else:
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED, 'Package was not found')

    @ExceptionHandler
    def get_files(self, package_ids):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        package_id = package_ids[0]
        def _get_files(troveSource, n, v, f):
            files = []
            troves = [(n, v, f)]
            trv = troveSource.getTrove(n, v, f)
            troves.extend([ x for x in trv.iterTroveList(strongRefs=True)
                                if troveSource.hasTrove(*x)])
            for n, v, f in troves:
                for (pathId, path, fileId, version, filename) in \
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

#    @ExceptionHandler
    def refresh_cache(self):
        #log.debug("refresh-cache command ")
        self.percentage()
        self.status(STATUS_REFRESH_CACHE)
        cache = Cache()
        cache.refresh()
        #if not cache.is_populate_database:
        #    self.status(STATUS_WAIT)
        #    cache.populate_database()

    @ExceptionHandler
    def update(self, package_ids):
        '''
        Implement the {backend}-update functionality
        '''
        self.allow_cancel(True)
        self.percentage(0)
        self.status(STATUS_RUNNING)
        
        for package in package_ids.split(" "):
            name, version, flavor, installed = self._findPackage(package)
            if name:
               # self._do_package_update(name, version, flavor)
               cli = ConaryPk()
               cli.update(name)
            else:
                self.error(ERROR_PACKAGE_ALREADY_INSTALLED, 'No available updates')

    def install_packages(self, package_ids):
        """
            alias of update_packages
        """
        self.update_packages(package_ids)

    @ExceptionHandler
    def update_packages(self, package_ids):
        '''
        Implement the {backend}-{install, update}-packages functionality
        '''

        for package_id in package_ids:
            name, version, flavor, installed = self._findPackage(package_id)
            log.info((name, version, flavor, installed ))

            self.allow_cancel(True)
            self.percentage(0)
            self.status(STATUS_RUNNING)

            if name:
                if installed == INFO_INSTALLED:
                    self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                        'Package already installed')

                self.status(INFO_INSTALLING)
                log.info(">>> end Prepare Update")
                self._get_package_update(name, version, flavor)
                self.status(STATUS_WAIT)
                log.info(">>> end Prepare Update")
                self._do_package_update(name, version, flavor)
            else:
                self.error(ERROR_PACKAGE_ALREADY_INSTALLED, 'Package was not found')

    @ExceptionHandler
    def remove_packages(self, allowDeps, package_ids):
        '''
        Implement the {backend}-remove-packages functionality
        '''
        self.allow_cancel(True)
        self.percentage(0)
        self.status(STATUS_RUNNING)
        log.info("========== Remove Packages ============ ")
        #for package_id in package_ids.split('%'):
        for package_id in package_ids:
            name, version, flavor, installed = self._findPackage(package_id)

            if name:
                if not installed == INFO_INSTALLED:
                    self.error(ERROR_PACKAGE_NOT_INSTALLED, 'The package %s is not installed' % name)

                name = '-%s' % name
                self.status(INFO_REMOVING)
                self._get_package_update(name, version, flavor)
                self._do_package_update(name, version, flavor)
            else:
                self.error(ERROR_PACKAGE_ALREADY_INSTALLED, 'The package was not found')

    def _get_metadata(self, package_id, field):
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
        n, v, f = self.get_package_from_id(package_id)
        trvList = self.client.repos.findTrove(self.cfg.installLabelPath,
                                     (n, v, f),
                                     defaultFlavor = self.cfg.flavor)

        troves = self.client.repos.getTroves(trvList, withFiles=False)
        result = ''
        for trove in troves:
            result = trove.getMetadata()[field]
        return result

    def _get_update_extras(self, package_id):
        notice = self._get_metadata(package_id, 'notice') or " "
        urls = {'jira':[], 'cve' : [], 'vendor': []}
        if notice:
            # Update Details
            desc = notice['description']
            # Update References (Jira, CVE ...)
            refs = notice['references']
            if refs:
                for ref in refs:
                    typ = ref['type']
                    href = ref['href']
                    title = ref['title']
                    if typ in ('jira', 'cve') and href != None:
                        if title == None:
                            title = ""
                        urls[typ].append("%s;%s" % (href, title))
                    else:
                        urls['vendor'].append("%s;%s" % (ref['href'], ref['title']))

            # Reboot flag
            if notice.get_metadata().has_key('reboot_suggested') and notice['reboot_suggested']:
                reboot = 'system'
            else:
                reboot = 'none'
            return _format_str(desc), urls, reboot
        else:
            return "", urls, "none"

    def _check_for_reboot(self, name):
        if name in self.rebootpkgs:
            self.require_restart(RESTART_SYSTEM, "")

    @ExceptionHandler
    def get_update_detail(self, package_ids):
        '''
        Implement the {backend}-get-update_detail functionality
        '''
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        package_id = package_ids[0]
        name, version, flavor, installed = self._findPackage(package_id)
        #update = self._get_updated(pkg)
        update = ""
        obsolete = ""
        #desc, urls, reboot = self._get_update_extras(package_id)
        #cve_url = _format_list(urls['cve'])
        cve_url = ""
        #bz_url = _format_list(urls['jira'])
        bz_url = ""
        #vendor_url = _format_list(urls['vendor'])
        vendor_url = ""
        reboot = "none"
        desc = self._get_metadata(package_id, 'longDesc') or " "
        self.update_detail(package_id, update, obsolete, vendor_url, bz_url, cve_url,
                reboot, desc, changelog="", state="", issued="", updated="")

   # @ExceptionHandler
    def get_details(self, package_ids):
        '''
        Print a detailed description for a given package
        '''
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        log.info("========== get_details =============")
        log.info(package_ids[0])
        package_id = package_ids[0]
        name, version, flavor, installed = self._findPackage(package_id)
        
        summary = package_id.split(";")
        log.info("====== summar")
        log.info(summary)

        repo = summary[3].split("#")[0]
        metadata = eval(summary[3].split("#")[1])
        short_package_id  = ""
        for i in summary[0:3]:
            short_package_id += i +';'

        log.info("Metadata--------------------")
        log.info(metadata)

        if name:
            if metadata.has_key("shortDesc"):
                shortDesc = metadata["shortDesc"] 
            else:
                shortDesc = ""
            if metadata.has_key("longDesc"):
                longDesc = metadata["longDesc"] 
            else:
                longDesc = ""

            url = "http://www.foresightlinux.org/packages/%s.html" % name

            categories  = ""
            if metadata.has_key("categorie"):
                categories =  Cache()._getCategorieBase( groupMap, metadata['categorie'])
            else:
                categories = None
            # Package size goes here, but I don't know how to find that for conary packages.
            self.details(short_package_id, None, categories, longDesc, url, 0)
        else:
            self.error(ERROR_PACKAGE_NOT_FOUND, 'Package was not found')

    def _show_package(self, name, version, flavor, status):
        '''  Show info about package'''
        package_id = self.get_package_id(name, version, flavor)
        summary = package_id.split(";")
        metadata = eval(summary[3].split("#")[1])
        if metadata.has_key("shortDesc"):
            meta = metadata["shortDesc"]
        else:
            meta = " "
        self.package(package_id, status, meta)

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
    def get_updates(self, filters):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        log.info("============== get_updates ========================")
        updateItems = self.client.fullUpdateItemList()
        log.info("============== end get_updates ========================")
        applyList = [ (x[0], (None, None), x[1:], True) for x in updateItems ]
        log.info("_get_update ....")
        updJob, suggMap = self._get_update(applyList)
        log.info("_get_update ....end.")

        jobLists = updJob.getJobs()
        log.info("get Jobs")

        totalJobs = len(jobLists)
        for num, job in enumerate(jobLists):
            status = '2'
            log.info( (num, job)  )
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

    def _findPackage(self, package_id):
        '''
        find a package based on a package id (name;version;arch;repoid)
        '''
        log.info("========== _findPackage ==========")
        log.info(package_id)
        troveTuples = self.get_package_from_id(package_id)
        for troveTuple in troveTuples:
            log.info("======== trove ")
            log.info(troveTuple)
            installed =self.check_installed(troveTuple)
            log.info(installed)
            name, version, flavor = troveTuple
            return name, version, flavor, installed
        else:
            self.error(ERROR_INTERNAL_ERROR, "package_id Not Correct ")

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
        pass

def main():
    backend = PackageKitConaryBackend('')
    log.info("======== argv =========== ")
    log.info(sys.argv)
    backend.dispatcher(sys.argv[1:])

if __name__ == "__main__":
    main()
