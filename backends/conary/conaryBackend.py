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

from conary.conaryclient import DepResolutionFailure
from conary.errors import InternalConaryError
from conary.trove import TroveIntegrityError

from packagekit.backend import get_package_id, split_package_id, \
    PackageKitBaseBackend
from packagekit.enums import (ERROR_DEP_RESOLUTION_FAILED, ERROR_NO_CACHE,
        ERROR_NO_PACKAGES_TO_UPDATE, ERROR_UNKNOWN, FILTER_INSTALLED,
        FILTER_NOT_INSTALLED, INFO_INSTALLING, INFO_NORMAL, INFO_REMOVING,
        INFO_SECURITY, INFO_UPDATING, MESSAGE_COULD_NOT_FIND_PACKAGE,
        RESTART_APPLICATION, RESTART_NONE, RESTART_SYSTEM, STATUS_INFO,
        STATUS_QUERY, STATUS_REFRESH_CACHE, STATUS_RUNNING, STATUS_UPDATE,
        UPDATE_STATE_STABLE, UPDATE_STATE_TESTING, UPDATE_STATE_UNSTABLE)

from conaryCallback import UpdateCallback, GetUpdateCallback
from conaryCallback import RemoveCallback, UpdateSystemCallback
from conaryFilter import ConaryFilter
from XMLCache import XMLCache
import conarypk

# To use the logger, uncomment this line:
# from pkConaryLog import log

def ConaryExceptionHandler(func):
    '''Centralized handler for conary Exceptions

    Currently only considers conary install/erase/updateall.
    '''
    def wrapper(self, *args, **kwargs):
        try:
            return func(self, *args, **kwargs)
        except DepResolutionFailure as e:
            deps = [str(i[0][0]).split(":")[0] for i in e.cannotResolve]
            self.error(ERROR_DEP_RESOLUTION_FAILED, ", ".join(set(deps)))
        except InternalConaryError as e:
            if str(e) == "Stale update job":
                self.conary.clear_job_cache()
                # The UpdateJob can be invalid. It's probably because after the
                # update job is fozen, the state of the database has changed.
                self.error(ERROR_NO_CACHE,
                        "The previously cached update job is broken. Please try again.")
        except TroveIntegrityError:
            self.error(ERROR_NO_PACKAGES_TO_UPDATE, "Network error. Try again")
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

def _get_fits(branch):
    if "conary.rpath.com" in branch:
        return "http://issues.rpath.com; rPath Issue Tracking System"
    elif "foresight.rpath.org" in branch:
        return "http://issues.foresightlinux.org; Foresight Issue Tracking System"
    else:
        return ""

def _get_license(license_list):
    if license_list == "":
        return ""

    # license_list is a list of licenses in the format of
    # 'rpath.com/licenses/copyright/GPL-2'.
    return " ".join([i.split("/")[-1] for i in license_list])

def _get_branch(branch):
    branchList = branch.split("@")
    if "2-qa" in branchList[1]:
        return UPDATE_STATE_TESTING
    elif "2-devel" in branchList[1]:
        return UPDATE_STATE_UNSTABLE
    else:
        return UPDATE_STATE_STABLE

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
        self.xmlcache = XMLCache(self.conary.get_labels())

    def _get_package_name_from_ids(self, package_ids):
        return [split_package_id(x)[0] for x in package_ids]

    def _format_package_summary(self, name, short_desc):
        data = short_desc
        if data == "." or data == "":
            data = name.replace("-",' ').capitalize()
        return data

    def _search_package(self, pkg_list, name):
        for pkg in pkg_list:
            if pkg["name"] == name:
                return pkg
        return None

    def _convert_package(self, trovetuple, metadata):
        return dict(
                trove = trovetuple,
                metadata = metadata
            )

    def _do_search(self, filters, searchlist, where = "name"):
        """
         searchlist(str)ist as the package for search like
         filters(str) as the filter
        """
        if where not in ("name", "details", "group", "all"):
            self.error(ERROR_UNKNOWN, "DORK---- search where not found")

        pkgList = self.xmlcache.search(searchlist, where )

        if len(pkgList) > 0 :
            pkgs = self._resolve_list(pkgList, filters)
            self._show_package_list(pkgs)
        else:
            self.message(MESSAGE_COULD_NOT_FIND_PACKAGE,"search not found")

    def _resolve_list(self, pkg_list, filters):
        pkgFilter = ConaryFilter()

        installed = []
        if FILTER_NOT_INSTALLED not in filters:
            installed = self._resolve_local(pkgFilter, pkg_list)

        if FILTER_INSTALLED not in filters:
            pkg_list = [x for x in pkg_list if x not in installed]
            self._resolve_repo(pkgFilter, pkg_list)

        package_list = pkgFilter.post_process()
        return package_list

    def _resolve_local(self, pkgFilter, pkg_list):
        '''Find out installed packages from pkg_list

        If a package from pkg_list can be found locally, add it (after some
        convertion) to pkgFilter.

        Returns the list of installed packages.
        '''
        ret = []

        troves_all = [(p["name"], None, None) for p in pkg_list]
        troves_local = self.client.db.findTroves(None, troves_all,
                allowMissing=True)

        for trv in troves_local:
            pkg = self._search_package(pkg_list, trv[0])
            ret.append(pkg)

            # A package may have different versions/flavors installed.
            for t in troves_local[trv]:
                pkgFilter.add_installed([self._convert_package(t, pkg)])

        return ret

    def _resolve_repo(self, pkgFilter, pkg_list):
        '''Find out packages from pkg_list that are available in the repository

        If a package from pkg_list can be found in the repo, add it (after some
        convertion) to pkgFilter.

        No return value.
        '''
        troves_all = [(pkg["name"], None, self.conary.flavor) for pkg in
                pkg_list]
        troves_repo = self.client.repos.findTroves(self.conary.default_label,
                troves_all, allowMissing=True)

        for trv in troves_repo:
            # only use the first trove in the list
            t = troves_repo[trv][0]
            pkg = self._search_package(pkg_list, t[0])
            pkgFilter.add_available([self._convert_package(t, pkg)])

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

        pkgs = self._resolve_list([pkg_dict], filters)
        self._show_package_list(pkgs)

    def _show_package_list(self, lst):
        '''Emit Package signals for a list of packages

        pkgs should be a list of (trove, status) tuples.

        Trove is a dict of {(name, version, flavor), metadata}, as constructed
        by _convert_package.
        '''
        def is_redirected_package(version):
            # The format of a revision string is
            #   "<upstream version>-<source count>-<build count>".
            # If upstream version is 0, the package has become nil.
            return version.split("-")[0] == "0"

        for pkg, status in lst:
            name, v, f = pkg["trove"]
            version = str(v.trailingRevision())
            if is_redirected_package(version):
                continue
            label = str(v.trailingLabel())
            arch = conarypk.get_arch(f)

            pkg_id = get_package_id(name, version, arch, label)

            summary = self._format_package_summary(name,
                    pkg["metadata"].get("shortDesc", "").decode("UTF"))
            self.package(pkg_id, status, summary)

    def search_group(self, options, searchlist):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)
        self._do_search(options, searchlist, 'group')

    def search_file(self, filters, search ):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)
        name = self.conary.search_path( search )
        if name:
            if ":" in name:
                name = name.split(":")[0]
            self.resolve( filters, [name])

    def search_name(self, options, searchlist):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)
        self._do_search(options, searchlist, 'name')

    def search_details(self, options, search):
        self.allow_cancel(True)
        #self.percentage(None)
        self.status(STATUS_QUERY)
        self._do_search(options, search, 'details' )

    def get_packages(self, filters):
        self.allow_cancel(False)
        self.status(STATUS_QUERY)
        self._do_search(filters, "", 'all' )

    def get_files(self, package_ids):
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for package_id in package_ids:
            name, version, arch, data = split_package_id(package_id)
            files = self.conary.list_files('%s=%s[is: %s]' %
                    (name, version, arch))
            self.files(package_id, ';'.join(files))

    @ConaryExceptionHandler
    def update_system(self, only_trusted):
        # FIXME: use only_trusted
        self.allow_cancel(False)
        self.status(STATUS_UPDATE)
        cb = UpdateSystemCallback(self, self.cfg)
        self.conary.updateall(cb, dry_run=False)

    def refresh_cache(self, force):
        # TODO: use force ?
        self.percentage(None)
        self.status(STATUS_REFRESH_CACHE)
        self.percentage(None)
        self.xmlcache.refresh()

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

        pkgs = [(self._convert_package((n, v, f), {}), info)
                for (n, v, f, info) in ret]
        self._show_package_list(pkgs)

    def install_packages(self, only_trusted, package_ids):
        self._install_packages(only_trusted, package_ids)

    def simulate_install_packages(self, package_ids):
        return self._install_packages(False, package_ids, simulate=True)

    @ConaryExceptionHandler
    def _install_packages(self, only_trusted, package_ids, simulate=False):
        self.allow_cancel(False)
        self.percentage(0)
        self.status(STATUS_RUNNING)

        pkglist = _get_trovespec_from_ids(package_ids)
        cb = UpdateCallback(self, self.cfg)
        updJob, suggMap = self.conary.install(pkglist, cb, simulate)
        if simulate:
            pkgs = self._get_package_name_from_ids(package_ids)
            installs, erases, updates = conarypk.parse_jobs(updJob,
                    excludes=pkgs, show_components=False)
            self._display_update_jobs(installs, erases, updates)

    def remove_packages(self, allowDeps, autoremove, package_ids):
        self. _remove_packages(allowDeps, autoremove, package_ids)

    def simulate_remove_packages(self, package_ids):
        return self._remove_packages(False, False, package_ids, simulate=True)

    @ConaryExceptionHandler
    def _remove_packages(self, allowDeps, autoremove, package_ids, simulate=False):
        # TODO: use autoremove
        self.allow_cancel(False)
        self.percentage(0)
        self.status(STATUS_RUNNING)

        pkglist = _get_trovespec_from_ids(package_ids)
        cb = RemoveCallback(self, self.cfg)
        updJob, suggMap = self.conary.erase(pkglist, cb, simulate)
        if simulate:
            pkgs = self._get_package_name_from_ids(package_ids)
            installs, erases, updates = conarypk.parse_jobs(updJob,
                    excludes=pkgs, show_components=False)
            self._display_update_jobs(installs, erases, updates)

    def _check_for_reboot(self, name):
        if name in self.rebootpkgs:
            self.require_restart(RESTART_SYSTEM, "")

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
                state = _get_branch(label)
                bz_url = _get_fits(label)
                self.update_detail(package_id, update, obsolete, vendor_url, bz_url, cve_url,
                        reboot, desc, changelog="", state= state, issued="", updated = "")

    def get_details(self, package_ids):
        '''
        Print a detailed description for a given package
        '''
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for package_id in package_ids:
            name, version, arch, data = split_package_id(package_id)
            pkgDict = self.xmlcache.resolve(name)
            if name and pkgDict:
                longDesc = ""
                url = ""
                categories  = None
                licenses = ""

                longDesc = pkgDict.get("longDesc", "")
                url = pkgDict.get("url", "")
                categories = self.xmlcache.getGroup(pkgDict.get("category",""))
                licenses = _get_license(pkgDict.get("licenses",""))
                size = pkgDict.get("size", 0)
                self.details(package_id, licenses, categories, longDesc, url, size)

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

    def _display_updates(self, jobs):
        '''Emit Package signals for a list of update jobs

        jobs should only contain installs and updates. Shouldn't get any erase
        jobs.
        '''
        ret = []
        for (name, (oldVer, oldFla), (newVer, newFla)) in jobs:
            info = self._get_update_priority(name)
            ret.append((name, newVer, newFla, info))

        pkgs = [(self._convert_package((n, v, f), {}), info)
                for (n, v, f, info) in ret]
        self._show_package_list(pkgs)

    @ConaryExceptionHandler
    def get_updates(self, filters):
        self.allow_cancel(True)
        self.percentage(0)
        self.status(STATUS_INFO)

        cb = GetUpdateCallback(self, self.cfg)
        updJob, suggMap = self.conary.updateall(cb, dry_run=True)
        installs, erases, updates = conarypk.parse_jobs(updJob,
                show_components=False)
        self._display_updates(installs + updates)

    def get_repo_list(self, filters):
        labels = self.conary.get_labels()
        self.status(STATUS_QUERY)
        for repo in labels:
            self.repo_detail(repo, repo, True)

def main():
    backend = PackageKitConaryBackend('')
    backend.dispatcher(sys.argv[1:])

if __name__ == "__main__":
    main()
