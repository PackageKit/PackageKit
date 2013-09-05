#!/usr/bin/python
# -*- coding: utf-8 -*-
#
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
# Copyright (C) 2007 S.Çağlar Onur <caglar@pardus.org.tr>

import pisi
import pisi.ui
from packagekit.backend import *
from packagekit.package import PackagekitPackage
from packagekit import enums
import os.path
import piksemel


class SimplePisiHandler(pisi.ui.UI):

    def __init(self):
        pisi.ui.UI.__init__(self, False, False)

    def display_progress(self, **ka):
        self.the_callback(**ka)


class PackageKitPisiBackend(PackageKitBaseBackend, PackagekitPackage):

    MAPPING_FILE = "/etc/PackageKit/pisi.conf"

    def __init__(self, args):
        self._load_mapping_from_disk()
        PackageKitBaseBackend.__init__(self, args)

        self.componentdb = pisi.db.componentdb.ComponentDB()
        self.filesdb = pisi.db.filesdb.FilesDB()
        self.installdb = pisi.db.installdb.InstallDB()
        self.packagedb = pisi.db.packagedb.PackageDB()
        self.repodb = pisi.db.repodb.RepoDB()

        # Do not ask any question to users
        self.options = pisi.config.Options()
        self.options.yes_all = True

        self.saved_ui = pisi.context.ui

    def _load_mapping_from_disk(self):
        """ Load the PK Group-> PiSi component mapping """
        if os.path.exists(self.MAPPING_FILE):
            with open(self.MAPPING_FILE, "r") as mapping:
                self.groups = {}
                for line in mapping.readlines():
                    line = line.replace("\r", "").replace("\n", "").strip()
                    if line.strip() == "" or "#" in line:
                        continue
                    splits = line.split("=")
                    pisi_component = splits[0].strip()
                    pk_group = splits[1].strip()
                    self.groups[pisi_component] = pk_group
        else:
            self.groups = {}

    def __get_package_version(self, package):
        """ Returns version string of given package """
        # Internal FIXME: PiSi may provide this
        if package.build is not None:
            version = "%s-%s-%s" % (package.version, package.release,
                                    package.build)
        else:
            version = "%s-%s" % (package.version, package.release)
        return version

    def __get_package(self, package, filters=None):
        """ Returns package object suitable for other methods """
        if self.installdb.has_package(package):
            status = INFO_INSTALLED
            pkg = self.installdb.get_package(package)
        elif self.packagedb.has_package(package):
            status = INFO_AVAILABLE
            pkg = self.packagedb.get_package(package)
        else:
            self.error(ERROR_PACKAGE_NOT_FOUND, "Package was not found")

        if filters:
            if "none" not in filters:
                if FILTER_INSTALLED in filters and status != INFO_INSTALLED:
                    return
                if FILTER_NOT_INSTALLED in filters and status == INFO_INSTALLED:
                    return
                if FILTER_GUI in filters and "app:gui" not in pkg.isA:
                    return
                if FILTER_NOT_GUI in filters and "app:gui" in pkg.isA:
                    return

        version = self.__get_package_version(pkg)

        id = self.get_package_id(pkg.name, version, pkg.architecture, "")

        return self.package(id, status, pkg.summary)

    def get_depends(self, filters, package_ids, recursive):
        """ Prints a list of depends for a given package """
        self.allow_cancel(True)
        self.percentage(None)

        package = self.get_package_from_id(package_ids[0])[0]

        for pkg in self.packagedb.get_package(package).runtimeDependencies():
            # FIXME: PiSi API has really inconsistent for return types
            # and arguments!
            self.__get_package(pkg.package)

    def get_details(self, package_ids):
        """ Prints a detailed description for a given package """
        self.allow_cancel(True)
        self.percentage(None)

        package = self.get_package_from_id(package_ids[0])[0]

        if self.packagedb.has_package(package):
            pkg = self.packagedb.get_package(package)
            repo = self.packagedb.get_package_repo(pkg.name, None)
            pkg_id = self.get_package_id(pkg.name,
                                         self.__get_package_version(pkg),
                                         pkg.architecture, repo[1])

            if pkg.partOf in self.groups:
                group = self.groups[pkg.partOf]
            else:
                group = GROUP_UNKNOWN

            homepage = pkg.source.homepage if pkg.source.homepage is not None\
                else ''

            self.details(pkg_id, ",".join(pkg.license), group, pkg.description,
                         homepage, pkg.packageSize)
        else:
            self.error(ERROR_PACKAGE_NOT_FOUND, "Package was not found")

    def get_files(self, package_ids):
        """ Prints a file list for a given package """
        self.allow_cancel(True)
        self.percentage(None)

        package = self.get_package_from_id(package_ids[0])[0]

        if self.installdb.has_package(package):
            pkg = self.packagedb.get_package(package)
            repo = self.packagedb.get_package_repo(pkg.name, None)
            pkg_id = self.get_package_id(pkg.name,
                                         self.__get_package_version(pkg),
                                         pkg.architecture, repo[1])

            pkg = self.installdb.get_files(package)

            files = map(lambda y: "/%s" % y.path, pkg.list)

            file_list = ";".join(files)
            self.files(pkg_id, file_list)

    def get_repo_list(self, filters):
        """ Prints available repositories """
        self.allow_cancel(True)
        self.percentage(None)

        for repo in pisi.api.list_repos():
            # Internal FIXME: What an ugly way to get repo uri
            # FIXME: Use repository enabled/disabled state
            uri = self.repodb.get_repo(repo).indexuri.get_uri()
            self.repo_detail(repo, uri, True)

    def get_requires(self, filters, package_ids, recursive):
        """ Prints a list of requires for a given package """
        self.allow_cancel(True)
        self.percentage(None)

        package = self.get_package_from_id(package_ids[0])[0]

        # FIXME: Handle packages which is not installed from repository
        for pkg in self.packagedb.get_rev_deps(package):
            self.__get_package(pkg[0])

    def get_updates(self, filter):
        """ Prints available updates and types """
        self.allow_cancel(True)
        self.percentage(None)

        for package in pisi.api.list_upgradable():

            pkg = self.packagedb.get_package(package)

            version = self.__get_package_version(pkg)
            id = self.get_package_id(pkg.name, version, pkg.architecture, "")
            installed_package = self.installdb.get_package(package)

            # FIXME: PiSi must provide this information as a single API call :(
            updates = [i for i in self.packagedb.get_package(package).history
                       if pisi.version.Version(i.release) >
                       installed_package.release]
            if pisi.util.any(lambda i: i.type == "security", updates):
                self.package(id, INFO_SECURITY, pkg.summary)
            else:
                self.package(id, INFO_NORMAL, pkg.summary)

    def _extract_update_details(self, pindex, package_name):
        document = piksemel.parse(pindex)
        packages = document.tags("Package")
        for pkg in packages:
            if pkg.getTagData("Name") == package_name:
                history = pkg.getTag("History")
                update = history.tags("Update")
                update_message = "Updated"
                update_release = 0
                update_data = ""
                needsReboot = False
                for update in update:
                    if int(update.getAttribute("release")) > update_release:
                        update_release = int(update.getAttribute("release"))
                        updater = update.getTagData("Name")
                        update_message = update.getTagData("Comment")
                        update_message = update_message.replace("\n", ";")
                        update_date = update.getTagData("Date")
                        needsReboot = False
                        try:
                            requires = update.getTag("Requires")
                            action = requires.getTagData("Action")
                            if action == "systemRestart":
                                needsReboot = True
                        except Exception:
                            pass
                return (update_message, update_date, needsReboot)
            pkg = pkg.nextTag("Package")
        return("Log not found", "", False)

    def get_update_detail(self, package_ids):
        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]
            the_package = self.installdb.get_package(package)
            updates = [package_id]
            obsoletes = ""
            # TODO: Add regex matching for #FIXES:ID or something similar
            bugzilla_url = ""
            cve_url = ""
            package_url = the_package.source.homepage
            vendor_url = package_url if package_url is not None else ""
            issued = ""
            repo = self.packagedb.get_package_repo(package, None)[1]
            pindex = "/var/lib/pisi/index/%s/pisi-index.xml" % repo

            changelog = ""
            # TODO: Set to security_issued if security update
            issued = updated = ""
            update_message, security_issued, needsReboot = \
                self._extract_update_details(pindex, package)
            # TODO: Add tagging to repo's, or a mapping file
            state = UPDATE_STATE_STABLE
            reboot = "system" if needsReboot else "none"

            self.update_detail(package_id, updates, obsoletes, vendor_url,
                               bugzilla_url, cve_url, reboot, update_message,
                               changelog, state, issued, updated)

    def download_packages(self, directory, package_ids):
        """ Download the given packages to a directory """
        self.allow_cancel(False)
        self.percentage(None)
        self.status(STATUS_DOWNLOAD)

        packages = list()

        def progress_cb(**kw):
            self.percentage(int(kw['percent']))

        ui = SimplePisiHandler()
        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]
            packages.append(package)
            try:
                pkg = self.packagedb.get_package(package)
            except:
                self.error(ERROR_PACKAGE_NOT_FOUND, "Package was not found")
        try:
            pisi.api.set_userinterface(ui)
            ui.the_callback = progress_cb
            if directory is None:
                directory = os.path.curdir
            pisi.api.fetch(packages, directory)
            # Scan for package
            for package in packages:
                package_obj = self.packagedb.get_package(package)
                uri = package_obj.packageURI.split("/")[-1]
                location = os.path.join(directory, uri)
                self.files(package_id, location)
            pisi.api.set_userinterface(self.saved_ui)
        except Exception, e:
            self.error(ERROR_PACKAGE_DOWNLOAD_FAILED,
                       "Could not download package: %s" % e)
        self.percentage(None)

    def install_files(self, only_trusted, files):
        """ Installs given package into system"""

        # FIXME: use only_trusted

        # FIXME: install progress
        self.allow_cancel(False)
        self.percentage(None)

        def progress_cb(**kw):
            self.percentage(int(kw['percent']))

        ui = SimplePisiHandler()

        self.status(STATUS_INSTALL)
        pisi.api.set_userinterface(ui)
        ui.the_callback = progress_cb

        try:
            self.status(STATUS_INSTALL)
            pisi.api.install(files)
        except pisi.Error, e:
            # FIXME: Error: internal-error : Package re-install declined
            # Force needed?
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED, e)
        pisi.api.set_userinterface(self.saved_ui)

    def _report_all_for_package(self, package, remove=False):
        """ Report all deps for the given package """
        if not remove:
            deps = self.packagedb.get_package(package).runtimeDependencies()
            # TODO: Add support to report conflicting packages requiring removal
            #conflicts = self.packagedb.get_package (package).conflicts
            for dep in deps:
                if not self.installdb.has_package(dep.name()):
                    dep_pkg = self.packagedb.get_package(dep.name())
                    repo = self.packagedb.get_package_repo(dep_pkg.name, None)
                    version = self.__get_package_version(dep_pkg)
                    pkg_id = self.get_package_id(dep_pkg.name, version,
                                                 dep_pkg.architecture, repo[1])
                    self.package(pkg_id, INFO_INSTALLING, dep_pkg.summary)
        else:
            rev_deps = self.installdb.get_rev_deps(package)
            for rev_dep, depinfo in rev_deps:
                if self.installdb.has_package(rev_dep):
                    dep_pkg = self.packagedb.get_package(rev_dep)
                    repo = self.packagedb.get_package_repo(dep_pkg.name, None)
                    version = self.__get_package_version(dep_pkg)
                    pkg_id = self.get_package_id(dep_pkg.name, version,
                                                 dep_pkg.architecture, repo[1])
                    self.package(pkg_id, INFO_REMOVING, dep_pkg.summary)

    def install_packages(self, transaction_flags, package_ids):
        """ Installs given package into system"""
        # FIXME: fetch/install progress
        self.allow_cancel(False)
        self.percentage(None)

        packages = list()

        # FIXME: use only_trusted
        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]
            if self.installdb.has_package(package):
                self.error(ERROR_PACKAGE_NOT_INSTALLED,
                           "Package is already installed")
            packages.append(package)

        def progress_cb(**kw):
            self.percentage(int(kw['percent']))

        ui = SimplePisiHandler()

        self.status(STATUS_INSTALL)
        pisi.api.set_userinterface(ui)
        ui.the_callback = progress_cb

        if TRANSACTION_FLAG_SIMULATE in transaction_flags:
            # Simulated, not real.
            for package in packages:
                self._report_all_for_package(package)
            return
        try:
            pisi.api.install(packages)
        except pisi.Error, e:
            self.error(ERROR_UNKNOWN, e)
        pisi.api.set_userinterface(self.saved_ui)

    def refresh_cache(self, force):
        """ Updates repository indexes """
        # TODO: use force ?
        self.allow_cancel(False)
        self.percentage(0)
        self.status(STATUS_REFRESH_CACHE)

        slice = (100 / len(pisi.api.list_repos())) / 2

        percentage = 0
        for repo in pisi.api.list_repos():
            pisi.api.update_repo(repo)
            percentage += slice
            self.percentage(percentage)

        self.percentage(100)

    def remove_packages(self, transaction_flags, package_ids,
                        allowdeps, autoremove):
        """ Removes given package from system"""
        self.allow_cancel(False)
        self.percentage(None)
        # TODO: use autoremove
        packages = list()

        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]
            if not self.installdb.has_package(package):
                self.error(ERROR_PACKAGE_NOT_INSTALLED,
                           "Package is not installed")
            packages.append(package)

        def progress_cb(**kw):
            self.percentage(int(kw['percent']))

        ui = SimplePisiHandler()

        package = self.get_package_from_id(package_ids[0])[0]
        self.status(STATUS_REMOVE)

        if TRANSACTION_FLAG_SIMULATE in transaction_flags:
            # Simulated, not real.
            for package in packages:
                self._report_all_for_package(package, remove=True)
            return
        try:
            pisi.api.remove(packages)
        except pisi.Error, e:
            self.error(ERROR_CANNOT_REMOVE_SYSTEM_PACKAGE, e)
        pisi.api.set_userinterface(self.saved_ui)

    def repo_set_data(self, repo_id, parameter, value):
        """ Sets a parameter for the repository specified """
        self.allow_cancel(False)
        self.percentage(None)

        if parameter == "add-repo":
            try:
                pisi.api.add_repo(repo_id, value, parameter)
            except pisi.Error, e:
                self.error(ERROR_UNKNOWN, e)

            try:
                pisi.api.update_repo(repo_id)
            except pisi.fetcher.FetchError:
                pisi.api.remove_repo(repo_id)
                err = "Could not reach the repository, removing from system"
                self.error(ERROR_REPO_NOT_FOUND, err)
        elif parameter == "remove-repo":
            try:
                pisi.api.remove_repo(repo_id)
            except pisi.Error:
                self.error(ERROR_REPO_NOT_FOUND, "Repository does not exist")
        else:
            self.error(ERROR_NOT_SUPPORTED, "Parameter not supported")

    def resolve(self, filters, package):
        """ Turns a single package name into a package_id
        suitable for the other methods """
        self.allow_cancel(True)
        self.percentage(None)

        self.__get_package(package[0], filters)

    def search_details(self, filters, values):
        """ Prints a detailed list of packages contains search term """
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        # Internal FIXME: Use search_details instead of _package when API
        # gains that ability :)
        for pkg in pisi.api.search_package(values):
            self.__get_package(pkg, filters)

    def search_file(self, filters, values):
        """ Prints the installed package which contains the specified file """
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for value in values:
            # Internal FIXME: Why it is needed?
            value = value.lstrip("/")

            for pkg, files in pisi.api.search_file(value):
                self.__get_package(pkg)

    def search_group(self, filters, values):
        """ Prints a list of packages belongs to searched group """
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for value in values:
            packages = list()
            for item in self.groups:
                if self.groups[item] == value:
                    try:
                        pkgs = self.componentdb.get_packages(item, walk=False)
                        packages.extend(pkgs)
                    except:
                        self.error(ERROR_GROUP_NOT_FOUND,
                                   "Component %s was not found" % value)
            for pkg in packages:
                self.__get_package(pkg, filters)

    def search_name(self, filters, values):
        """ Prints a list of packages contains search term in its name """
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for value in values:
            for pkg in pisi.api.search_package([value]):
                self.__get_package(pkg, filters)

    def update_packages(self, transaction_flags, package_ids):
        """ Updates given package to its latest version """

        # FIXME: use only_trusted

        # FIXME: fetch/install progress
        self.allow_cancel(False)
        self.percentage(None)

        packages = list()
        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]
            if not self.installdb.has_package(package):
                self.error(ERROR_PACKAGE_NOT_INSTALLED,
                           "Cannot update a package that is not installed")
            packages.append(package)

        def progress_cb(**kw):
            self.percentage(int(kw['percent']))

        ui = SimplePisiHandler()
        pisi.api.set_userinterface(ui)
        ui.the_callback = progress_cb

        if TRANSACTION_FLAG_SIMULATE in transaction_flags:
            for package in packages:
                self._report_all_for_package(package)
            return
        try:
            pisi.api.upgrade(packages)
        except pisi.Error, e:
            self.error(ERROR_UNKNOWN, e)
        pisi.api.set_userinterface(self.saved_ui)

    def update_system(self, only_trusted):
        """ Updates all available packages """
        # FIXME: use only_trusted
        # FIXME: fetch/install progress
        self.allow_cancel(False)
        self.percentage(None)

        if not len(pisi.api.list_upgradable()) > 0:
            self.error(ERROR_NO_PACKAGES_TO_UPDATE, "System is already up2date")

        try:
            pisi.api.upgrade(pisi.api.list_upgradable())
        except pisi.Error, e:
            self.error(ERROR_UNKNOWN, e)


def main():
    backend = PackageKitPisiBackend('')
    backend.dispatcher(sys.argv[1:])

if __name__ == "__main__":
    main()
