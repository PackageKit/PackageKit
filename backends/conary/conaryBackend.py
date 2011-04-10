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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Copyright (C) 2007 Ken VanDine <ken@vandine.org>
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
# Copyright (C) 2009-2010 Andres Vargas <zodman@foresightlinux.org>
#                         Scott Parkerson <scott.parkerson@gmail.com>

import sys
import os
import re
import xmlrpclib

from conary import conaryclient, errors, trove, versions
from conary.deps import deps
from conary.lib import util

from packagekit.backend import get_package_id, split_package_id, \
    PackageKitBaseBackend
from packagekit.enums import *

from conaryCallback import UpdateCallback, GetUpdateCallback
from conaryCallback import RemoveCallback, UpdateSystemCallback
from conaryFilter import ConaryFilter
from XMLCache import XMLCache
from pkConaryLog import log
import conarypk

sys.excepthook = util.genExcepthook()

def ExceptionHandler(func):
    return func
    def display(error):
        return str(error).replace('\n', ' ').replace("\t",'')
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

def _get_trovespec_from_ids(package_ids):
    ret = []
    for p in package_ids:
        name, version, arch, data = split_package_id(p)
        trovespec = name
        # Omitting version and label. Depend on conary to find the proper package.
        # This may be problematic.
        # Also omitting flavor for now.
        #if arch:
        #    trovespec = '%s[is: %s]' % (trovespec, arch)
        ret.append(trovespec)
    return ret

class PackageKitConaryBackend(PackageKitBaseBackend):
    # Packages that require a reboot
    rebootpkgs = ("kernel", "glibc", "hal", "dbus")
    restartpkgs = ("PackageKit","gnome-packagekit")

    def __init__(self, args):
        PackageKitBaseBackend.__init__(self, args)

        # conary configurations
        conary = conarypk.ConaryPk()
        self.cfg = conary.cfg
        self.client = conary.cli
        self.conary = conary
        self.xmlcache = XMLCache()

    def _get_package_name_from_ids(self, package_ids):
        return [split_package_id(x)[0] for x in package_ids]

    def _format_package_summary(self, name, metadata):
        data = ""
        if "shortDesc" in metadata:
            data = metadata['shortDesc'].decode("UTF")
        if data == "." or data == "":
            data = name.replace("-",' ').capitalize()
        return data

    def _search_package(self, pkg_list, name):
        for pkg in pkg_list:
            if pkg["trove"][0] == name:
                return pkg
        return None

    def _convert_package( self, trove , pkgDict ):
        return dict(
                trove = trove ,
                metadata = pkgDict
            )

    def _do_search(self, filters, searchlist, where = "name"):
        """
         searchlist(str)ist as the package for search like
         filters(str) as the filter
        """
        fltlist = filters
        if where not in ("name", "details", "group", "all"):
            self.error(ERROR_UNKNOWN, "DORK---- search where not found")

        pkgList = self.xmlcache.search(searchlist, where )

        if len(pkgList) > 0 :
            to_resolve = [self._convert_package((p["name"], None, None), p)
                    for p in pkgList]

            self._resolve_list(to_resolve, fltlist)
        else:
            self.message(MESSAGE_COULD_NOT_FIND_PACKAGE,"search not found")

    def _do_conary_update(self, op, *args):
        '''Wrapper around ConaryPk.install/erase() so we can add exception
        handling
        '''
        try:
            if op == 'install':
                ret = self.conary.install(*args)
            elif op == 'erase':
                ret = self.conary.erase(*args)
            else:
                self.error(ERROR_INTERNAL_ERROR, 'Unkown command: %s' % op)
        except conaryclient.DepResolutionFailure as e:
            deps = [str(i[0][0]).split(":")[0] for i in e.cannotResolve]
            self.error(ERROR_DEP_RESOLUTION_FAILED, ", ".join(set(deps)))
        except errors.InternalConaryError as e:
            if str(e) == "Stale update job":
                self.conary.clear_job_cache()
                # The UpdateJob can be invalid. It's probably because after the
                # update job is fozen, the state of the database has changed.
                self.error(ERROR_INVALID_PACKAGE_FILE,
                        "Previously cached file is broken. Try again")
        except trove.TroveIntegrityError:
            self.error(ERROR_NO_PACKAGES_TO_UPDATE, "Network error. Try again")
        return ret

    def _resolve_list(self, pkg_list, filters):
        pkgFilter = ConaryFilter()

        installed = []
        if FILTER_NOT_INSTALLED not in filters:
            installed = self._resolve_local(pkgFilter, pkg_list)

        if FILTER_INSTALLED not in filters:
            pkg_list = [x for x in pkg_list if x not in installed]
            self._resolve_repo(pkgFilter, pkg_list)

        package_list = pkgFilter.post_process()
        self._show_package_list(package_list)

    def _resolve_local(self, pkgFilter, pkg_list):
        '''Find out installed packages from pkg_list

        If a package from pkg_list can be found locally, add it (after some
        convertion) to pkgFilter.

        Returns the list of installed packages.
        '''
        ret = []

        list_trove_all = [p.get("trove") for p in pkg_list]
        db_trove_list = self.client.db.findTroves(None, list_trove_all, allowMissing=True)

        list_installed = []
        for trove in list_trove_all:
            if trove in db_trove_list:
                pkg = self._search_package(pkg_list, trove[0])
                # A package may have different versions/flavors installed.
                for t in db_trove_list[trove]:
                    list_installed.append(self._convert_package(t, pkg["metadata"]))
                ret.append(pkg)
        pkgFilter.add_installed(list_installed)

        return ret

    def _resolve_repo(self, pkgFilter, pkg_list):
        '''Find out packages from pkg_list that are available in the repository

        If a package from pkg_list can be found in the repo, add it (after some
        convertion) to pkgFilter.

        No return value.
        '''
        list_trove_all = []
        for pkg in pkg_list:
            name,version,flavor = pkg.get("trove")
            trove = (name, version, self.conary.flavor)
            list_trove_all.append(trove)

        repo_trove_list = self.client.repos.findTroves(self.conary.default_label,
                list_trove_all, allowMissing=True)

        list_available = []
        for trove in list_trove_all:
            if trove in repo_trove_list:
                # only use the first trove in the list
                t = repo_trove_list[trove][0]
                pkg = self._search_package(pkg_list, t[0])
                pkg["trove"] = t
                list_available.append(pkg)
        pkgFilter.add_available(list_available)

    @ExceptionHandler
    def resolve(self, filters, package ):
        """
            @filters  (list)  list of filters
            @package (list ) list with packages name for resolve
        """
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        pkg_dict = self.xmlcache.resolve( package[0] )
        if pkg_dict is None:
            return None

        # Our list of troves doesn't contain information about whether trove is
        # installed, so ConaryFilter can't do proper filtering. Don't pass
        # @filters to it. Instead manually check the filters before calling
        # add_installed() and add_available().
        filter = ConaryFilter()

        is_found_locally = False
        if FILTER_NOT_INSTALLED not in filters:
            trove_installed = self.conary.query(pkg_dict.get("name"))
            for trv in trove_installed:
                pkg = self._convert_package(trv, pkg_dict)
                filter.add_installed([pkg])
                is_found_locally = True

        if not is_found_locally and FILTER_INSTALLED not in filters:
            trove_available = self.conary.repo_query(pkg_dict.get("name"))
            for trv in trove_available:
                pkg = self._convert_package(trv, pkg_dict)
                filter.add_available([pkg])

        package_list = filter.post_process()
        self._show_package_list(package_list)

    def _show_package_list(self, lst):
        """@lst(list(tuple) = [ ( troveTuple, status ) ]
        """
        for pkg, status in lst:
            name, v, f = pkg["trove"]
            version = str(v.trailingRevision())
            label = str(v.trailingLabel())
            arch = conarypk.get_arch(f)

            pkg_id = get_package_id(name, version, arch, label)
            summary = self._format_package_summary(name, pkg["metadata"])
            self.package(pkg_id, status, summary)

    @ExceptionHandler
    def search_group(self, options, searchlist):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)
        self._do_search(options, searchlist, 'group')

    @ExceptionHandler
    def search_file(self, filters, search ):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)
        name = self.conary.search_path( search )
        if name:
            if ":" in name:
                name = name.split(":")[0]
            self.resolve( filters, [name])

    @ExceptionHandler
    def search_name(self, options, searchlist):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)
        self._do_search(options, searchlist, 'name')

    @ExceptionHandler
    def search_details(self, options, search):
        self.allow_cancel(True)
        #self.percentage(None)
        self.status(STATUS_QUERY)
        self._do_search(options, search, 'details' )

    @ExceptionHandler
    def get_packages(self, filter ):
        self.allow_cancel(False)
        self.status(STATUS_QUERY)
        self._do_search(filter, "", 'all' )

    @ExceptionHandler
    def get_files(self, package_ids):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for package_id in package_ids:
            name, version, arch, data = split_package_id(package_id)
            files = self.conary.list_files('%s=%s[is: %s]' %
                    (name, version, arch))
            self.files(package_id, ';'.join(files))

    def _do_conary_updateall(self, callback, dry_run):
        '''Wrapper around ConaryPk.updateall() so we can add exception handling
        '''
        try:
            ret = self.conary.updateall(callback, dry_run)
        except xmlrpclib.ProtocolError as e:
            self.error(ERROR_NO_NETWORK, '%s. Try again.' % str(e))
        return ret

    @ExceptionHandler
    def update_system(self, only_trusted):
        # FIXME: use only_trusted
        self.allow_cancel(False)
        self.status(STATUS_UPDATE)
        cb = UpdateSystemCallback(self, self.cfg)
        self._do_conary_updateall(cb, dry_run=False)

    def refresh_cache(self, force):
        # TODO: use force ?
        self.percentage(None)
        self.status(STATUS_REFRESH_CACHE)
        self.percentage(None)
        self.xmlcache.refresh()

    def _show_packages(self, pkgs):
        '''Emit Package signals for a list of packages

        pkgs should be a list of (name, Version, Flavor, status) tuples.
        '''
        for (name, v, f, status) in pkgs:
            version = str(v.trailingRevision())
            arch = conarypk.get_arch(f)
            label = str(v.trailingLabel())
            pkg_id = get_package_id(name, version, arch, label)
            summary = ''
            self.package(pkg_id, status, summary)

    def _display_update_jobs(self, install_jobs, erase_jobs, update_jobs):
        '''Emit package status for a list of installing/erasing/updating jobs
        '''
        ret = []
        for (name, (oldVer, oldFla), (newVer, newFla)) in install_jobs:
            ret.append((name, newVer, newFla, INFO_INSTALLING))

        for (name, (oldVer, oldFla), (newVer, newFla)) in erase_jobs:
            ret.append((name, oldVer, oldFla, INFO_REMOVING))

        for (name, (oldVer, oldFla), (newVer, newFla)) in update_jobs:
            ret.append((name, oldVer, oldFla, INFO_UPDATING))

        self._show_packages(ret)

    def install_packages(self, only_trusted, package_ids, simulate=False):
        self.allow_cancel(False)
        self.percentage(0)
        self.status(STATUS_RUNNING)

        pkglist = _get_trovespec_from_ids(package_ids)
        cb = UpdateCallback(self, self.cfg)
        updJob, suggMap = self._do_conary_update('install', pkglist, cb, simulate)
        if simulate:
            pkgs = self._get_package_name_from_ids(package_ids)
            jobs = conarypk.parse_jobs(updJob, excludes=pkgs,
                    show_components=False)
            self._display_update_jobs(*jobs)

    @ExceptionHandler
    def remove_packages(self, allowDeps, autoremove, package_ids, simulate=False):
        # TODO: use autoremove
        self.allow_cancel(False)
        self.percentage(0)
        self.status(STATUS_RUNNING)

        pkglist = _get_trovespec_from_ids(package_ids)
        cb = RemoveCallback(self, self.cfg)
        updJob, suggMap = self._do_conary_update('remove', pkglist, cb, simulate)
        if simulate:
            pkgs = self._get_package_name_from_ids(package_ids)
            jobs = conarypk.parse_jobs(updJob, excludes=pkgs,
                    show_components=False)
            self._display_update_jobs(*jobs)

    def _check_for_reboot(self, name):
        if name in self.rebootpkgs:
            self.require_restart(RESTART_SYSTEM, "")

    @ExceptionHandler
    def get_update_detail(self, package_ids):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        for package_id in package_ids:
            name, version, arch, label = split_package_id(package_id)
            pkgDict = self.xmlcache.resolve(name)
            update = ""
            obsolete = ""
            cve_url = ""
            if pkgDict:
                vendor_url = pkgDict.get("url","")
                desc = pkgDict.get("longDesc","")
                reboot = self._get_restart(name)
                state = self._get_branch(label)
                bz_url = self._get_fits(label)
                self.update_detail(package_id, update, obsolete, vendor_url, bz_url, cve_url,
                        reboot, desc, changelog="", state= state, issued="", updated = "")

   # @ExceptionHandler
    def get_details(self, package_ids):
        '''
        Print a detailed description for a given package
        '''
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for package_id in package_ids:
            name,version,arch,data = split_package_id(package_id)
            pkgDict = self.xmlcache.resolve(name)
            if name and pkgDict:
                longDesc = ""
                url = ""
                categories  = None
                license = ""

                longDesc = pkgDict.get("longDesc", "")
                url = pkgDict.get("url", "")
                categories = self.xmlcache.getGroup(pkgDict.get("category",""))
                license = self._get_license(pkgDict.get("licenses",""))
                size = pkgDict.get("size", 0)
                self.details(package_id, license, categories, longDesc, url, size)

    def _get_restart(self, name):
        if name in self.rebootpkgs:
            return RESTART_SYSTEM
        elif name in self.restartpkgs:
            return RESTART_APPLICATION
        else:
            return RESTART_NONE

    def _get_update_priority(self, name):
        if name in self.rebootpkgs:
            return INFO_SECURITY
        elif name in self.restartpkgs:
            return INFO_SECURITY
        else:
            return INFO_NORMAL

    def _get_fits(self, branch):
        if "conary.rpath.com" in branch:
            return "http://issues.rpath.com;rPath Issues Tracker"
        elif "foresight.rpath.org" in branch:
            return "http://issues.foresightlinux.org; Foresight Issues Tracker"
        else:
            return ""
    def _get_license(self, license_list ):
        if license_list == "":
            return ""

        # license_list is a list of licenses in the format of
        # 'rpath.com/licenses/copyright/GPL-2'.
        return " ".join([i.split("/")[-1] for i in license_list])

    def _get_branch(self, branch ):
        branchList = branch.split("@")
        if "2-qa" in branchList[1]:
            return UPDATE_STATE_TESTING
        elif "2-devel" in branchList[1]:
            return UPDATE_STATE_UNSTABLE
        else:
            return UPDATE_STATE_STABLE

    def _display_updates(self, jobs):
        '''Emit Package signals for a list of update jobs

        jobs should only contain installs and updates. Shouldn't get any erase
        jobs.
        '''
        ret = []
        for (name, (oldVer, oldFla), (newVer, newFla)) in jobs:
            info = self._get_update_priority(name)
            ret.append((name, newVer, newFla, info))

        self._show_packages(ret)

    @ExceptionHandler
    def get_updates(self, filters):
        self.allow_cancel(True)
        self.percentage(0)
        self.status(STATUS_INFO)

        cb = GetUpdateCallback(self, self.cfg)
        updJob, suggMap = self._do_conary_updateall(cb, dry_run=True)
        installs, erases, updates = conarypk.parse_jobs(updJob,
                show_components=False)
        self._display_updates(installs + updates)

    def get_repo_list(self, filters):
        labels = self.conary.get_labels_from_config()
        self.status(STATUS_QUERY)
        for repo in labels:
            repo_name = repo.split("@")[0]
            repo_branch  = repo.split("@")[1]
            self.repo_detail(repo,repo,True)

    def simulate_install_packages(self, package_ids):
        return self.install_packages(False, package_ids, simulate=True)

    def simulate_remove_packages(self, package_ids):
        return self.remove_packages(False, False, package_ids, simulate=True)

def main():
    backend = PackageKitConaryBackend('')
    backend.dispatcher(sys.argv[1:])

if __name__ == "__main__":
    main()
