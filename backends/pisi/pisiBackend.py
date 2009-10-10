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
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
# Copyright (C) 2007 S.Çağlar Onur <caglar@pardus.org.tr>

import pisi
from packagekit.backend import *
from packagekit.package import PackagekitPackage

class PackageKitPisiBackend(PackageKitBaseBackend, PackagekitPackage):

    def __init__(self, args):
        PackageKitBaseBackend.__init__(self, args)

        # init required db
        self.__init_db()

        # Do not ask any question to users
        self.options = pisi.config.Options()
        self.options.yes_all = True

    def __init_db(self):
        self.componentdb = pisi.db.componentdb.ComponentDB()
        self.filesdb = pisi.db.filesdb.FilesDB()
        self.installdb = pisi.db.installdb.InstallDB()
        self.packagedb = pisi.db.packagedb.PackageDB()
        self.repodb = pisi.db.repodb.RepoDB()
        self.groupdb = pisi.db.groupdb.GroupDB()

    # refresh all db
    def __invalidate_db_caches(self):
        pisi.db.invalidate_caches()
        self.__init_db()

    def __get_group(self, package):
        try:
            pkg_component = self.componentdb.get_component(package.partOf)
            return pkg_component.group
        except:
            return "unknown"

    def __get_package_version(self, package):
        """ Returns version string of given package """
        # Internal FIXME: PiSi may provide this
        if package.build is not None:
            version = "%s-%s-%s" % (package.version, package.release, package.build)
        else:
            version = "%s-%s" % (package.version, package.release)
        return version

    def __get_package_id(self, package):
        """ Returns package id string of given package """
        return self.get_package_id(package.name, self.__get_package_version(package), package.architecture, "")

    def __get_package(self, package, filters = None):
        """ Returns package object suitable for other methods """
        if self.installdb.has_package(package):
            status = INFO_INSTALLED
            pkg = self.installdb.get_package(package)
            pkg2 = self.packagedb.get_package(package)
            if pkg2.history[0].release > pkg.release:
                status = INFO_UPDATING
        elif self.packagedb.has_package(package):
            status = INFO_AVAILABLE
            pkg = self.packagedb.get_package(package)
        else:
            self.error(ERROR_PACKAGE_NOT_FOUND, "Package was not found")

        if filters:
            if "none" not in filters:
                filterlist = filters.split(';')

                if FILTER_INSTALLED in filterlist and not (status == INFO_INSTALLED or status == INFO_UPDATING):
                    return
                if FILTER_NOT_INSTALLED in filterlist and status == INFO_INSTALLED:
                    return
                if FILTER_GUI in filterlist and "app:gui" not in pkg.isA:
                    return
                if FILTER_NOT_GUI in filterlist and "app:gui" in pkg.isA:
                    return

        if status == INFO_UPDATING:
            self.package(self.__get_package_id(pkg), INFO_INSTALLED, pkg.summary)
            self.package(self.__get_package_id(pkg2), INFO_AVAILABLE, pkg2.summary)
        else:
            self.package(self.__get_package_id(pkg), status, pkg.summary)

    def get_depends(self, filters, package_ids, recursive):
        """ Prints a list of depends for a given package """
        self.allow_cancel(True)
        self.percentage(None)

        packages = []
        for package_id in package_ids:
            packages.append(self.get_package_from_id(package_id)[0])

        for package in packages:
            for pkg in self.packagedb.get_package(package).runtimeDependencies():
                # Internal FIXME: PiSi API has really inconsistent for return types and arguments!
                self.__get_package(pkg.package, filters)

    def get_details(self, package_ids):
        """ Prints a detailed description for a given package """
        self.allow_cancel(True)
        self.percentage(None)

        package = self.get_package_from_id(package_ids[0])[0]

        if self.packagedb.has_package(package):
            pkg_status = "available"
            pkg = self.packagedb.get_package(package)
            if self.installdb.has_package(package):
                pkg_status = "installed"
                if "%s-%s-%s" % (pkg.version, pkg.release, pkg.build) != package_ids[0].split(";")[1]:
                    pkg = self.installdb.get_package(package)

            my_package_id = "%s;%s;%s;%s" % (pkg.name,
                                             self.__get_package_version(pkg),
                                             pkg.architecture,
                                             pkg_status)

            #FIXME: pisi.installdb must be provide pkg.packageSize
            if pkg.packageSize == None:
                packageSize = 0
            else:
                packageSize = pkg.packageSize

            self.details(my_package_id,
                         " ".join(pkg.license),
                         self.__get_group(pkg),
                         pkg.description,
                         pkg.packageURI,
                         packageSize)
        else:
            self.error(ERROR_PACKAGE_NOT_FOUND, "Package was not found")

    def get_update_detail(self, package_ids):
        ''' Implement the {backend}-get-update_detail functionality '''
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        package = self.get_package_from_id(package_ids[0])[0]

        if self.installdb.has_package(package):
            installed_package = self.installdb.get_package(package)
            update_package    = self.packagedb.get_package(package)
            if update_package.release > installed_package.release:
                desc   = update_package.history[0].comment
                issued = update_package.history[0].date
                desc   = desc.replace("\n", "")
                desc   = desc.split()

                self.update_detail(self.__get_package_id(installed_package),
                                   #FIXME: format to 'git-1.6.3.4-81-5-i686' from git;1.6.3.4-81-5;i686;
                                   "'%s'" % ("-".join(self.__get_package_id(update_package).split(";")))[0:-1],
                                   '', 'http://www.pardus.org.tr', 'http://bugs.pardus.org.tr',
                                   '', 'none', " ".join(desc), '', '', issued, '')

    def get_files(self, package_ids):
        """ Prints a file list for a given package """
        self.allow_cancel(True)
        self.percentage(None)

        package = self.get_package_from_id(package_ids[0])[0]

        if self.installdb.has_package(package):
            pkg = self.installdb.get_files(package)

            files = map(lambda y: y.path, pkg.list)

            # Reformat for PackageKit
            # And add "/" for every file.
            file_list = ";/".join(files)
            file_list = "/%s" % file_list

            self.files(package, file_list)

    def get_packages(self, filters):
        """
        List all instaled package
        It is used with list-{create-diff-install}
        """
        for repo in self.repodb.list_repos():
            for pkg in self.packagedb.list_packages(repo):
                self.__get_package(pkg, filters)

    def get_repo_list(self, filters):
        """ Prints available repositories """
        self.allow_cancel(True)
        self.percentage(None)

        for repo in pisi.api.list_repos(False):
            if self.repodb.repo_active(repo):
                self.repo_detail(repo, self.repodb.get_repo_url(repo), "true")
            else:
                self.repo_detail(repo, self.repodb.get_repo_url(repo), "false")

    def get_requires(self, filters, package_ids, recursive):
        """ Prints a list of requires for a given package """
        # Shows package's reverse dependencies.
        # If packege will be removed, shows packages will be removed (rev deps).

        self.allow_cancel(True)
        self.percentage(None)

        package = self.get_package_from_id(package_ids[0])[0]

        if filters == 'installed':
            # show removed packages
            rev_dep_list = pisi.api.get_remove_order([package])
            rev_dep_list.pop()
            for pkg in rev_dep_list:
                self.__get_package(pkg)
        else:
            # show reverse dependencies
            if self.installdb.has_package(package):
                for pkg in self.installdb.get_rev_deps(package):
                    self.__get_package(pkg[0], filters)
            else:
                for pkg in self.packagedb.get_rev_deps(package):
                    self.__get_package(pkg[0], filters)



    def get_updates(self, filter):
        """ Prints available updates and types """
        self.allow_cancel(True)
        self.percentage(None)

        for package in pisi.api.list_upgradable():

            pkg = self.packagedb.get_package(package)

            version = self.__get_package_version(pkg)
            id = self.get_package_id(pkg.name, version, pkg.architecture, "")

            # Internal FIXME: PiSi must provide this information as a single API call :(
            updates = [i for i in self.packagedb.get_package(package).history
                    if pisi.version.Version(i.release) > pisi.version.Version(self.installdb.get_package(package).release)]
            if pisi.util.any(lambda i:i.type == "security", updates):
                self.package(id, INFO_SECURITY, pkg.summary)
            else:
                self.package(id, INFO_NORMAL, pkg.summary)

    def install_files(self, trusted, files):
        """ Installs given package into system"""
        # FIXME: install progress
        self.allow_cancel(False)
        self.percentage(0)

        try:
            self.percentage(5)
            self.status(STATUS_INSTALL)

            pisi.api.install(files)
            self.percentage(100)
            self.__invalidate_db_caches()
        except pisi.Error,e:
            # FIXME: Error: internal-error : Package re-install declined
            # Force needed?
            self.error(ERROR_PACKAGE_ALREADY_INSTALLED, e)

    def install_packages(self, package_ids):
        """ Installs given package into system"""
        # FIXME: fetch/install progress
        self.allow_cancel(False)
        self.percentage(0)
        percentage = 5

        packages = []
        for package_id in package_ids:
            packages.append(self.get_package_from_id(package_id)[0])

        self.percentage(percentage)
        self.status(STATUS_INSTALL)
        try:
            install_order = pisi.api.get_install_order(packages)
            slice = 90 / len(install_order)
            for pkg in install_order:
                pisi.api.install([pkg])
                percentage += slice
                self.percentage(percentage)
            self.__invalidate_db_caches()
            self.percentage(100)
        except pisi.Error,e:
            self.error(ERROR_UNKNOWN, e)

    def download_packages(self, directory, package_ids):
        '''
        Implement the {backend}-download-packages functionality
        '''

        self.status(STATUS_DOWNLOAD)
        fileList = []

        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]
            if self.packagedb.has_package(package):
                try:
                    self.percentage(0)
                    pisi.api.fetch([package], directory)
                    self.percentage(100)
                    pkg = self.packagedb.get_package(package)
                    filePath = directory + '/' + pkg.packageURI
                    fileList.append(filePath)
                    self.package(self.__get_package_id(pkg), INFO_DOWNLOADING, '')

                except pisi.Error, e:
                    self.error(ERROR_PACKAGE_DOWNLOAD_FAILED, e)
            else:
                self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, "Could not find a match for package %s" % package)

            self.files(package_ids[0], ";".join(fileList))


    def refresh_cache(self):
        """ Updates repository indexes """
        self.allow_cancel(False)
        self.percentage(0)
        self.status(STATUS_REFRESH_CACHE)

        slice = (100/len(pisi.api.list_repos()))/2

        percentage = 0
        for repo in pisi.api.list_repos():
            pisi.api.update_repo(repo)
            percentage += slice
            self.percentage(percentage)

        self.percentage(100)

    def remove_packages(self, deps, package_ids):
        """ Removes given package from system"""
        self.allow_cancel(False)
        self.status(STATUS_REMOVE)
        percentage = 0;
        self.percentage(percentage)

        packages = []
        for package_id in package_ids:
            packages.append(self.get_package_from_id(package_id)[0])

        percentage = 5
        self.percentage(percentage)

        try:
            remove_order = pisi.api.get_remove_order(packages)
            slice = 90 / len(remove_order)
            for pkg in remove_order:
                pisi.api.remove([pkg])
                percentage += slice
                self.percentage(percentage)
            self.__invalidate_db_caches()
            self.percentage(100)
        except pisi.Error,e:
            # system.base packages cannot be removed from system
            self.error(ERROR_CANNOT_REMOVE_SYSTEM_PACKAGE, e)

    def repo_enable(self, repoid, enable):
        '''
        Implement the {backend}-repo-enable functionality
        '''
        try:
            if enable == 'false':
                pisi.api.set_repo_activity(repoid, False)
            else:
                pisi.api.set_repo_activity(repoid, True)
        except Exception, e:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))


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
                self.error(ERROR_REPO_NOT_FOUND, "Could not be reached to repository, removing from system")
        elif parameter == "remove-repo":
            try:
                pisi.api.remove_repo(repo_id)
            except pisi.Error:
                self.error(ERROR_REPO_NOT_FOUND, "Repository is not exists")
        else:
            self.error(ERROR_NOT_SUPPORTED, "Parameter not supported")

    def resolve(self, filters, package):
        """ Turns a single package name into a package_id suitable for the other methods """
        self.allow_cancel(True)
        self.percentage(None)

        self.__get_package(package[0], filters)

    def search_details(self, filters, key):
        """ Prints a detailed list of packages contains search term """
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        # Internal FIXME: Use search_details instead of _package when API gains that ability :)
        for pkg in pisi.api.search_package([key]):
            self.__get_package(pkg, filters)

    def search_file(self, filters, key):
        """ Prints the installed package which contains the specified file """
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        # Internal FIXME: Why it is needed?
        key = key.lstrip("/")

        for pkg, files in pisi.api.search_file(key):
            self.__get_package(pkg, filters)

    def search_group(self, filters, group):
        """ Prints a list of packages belongs to searched group """
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)
        # FIXME: pisi must provide
        if group == "desktop-gnome":
            group = "gnome.desktop"
        elif group == "desktop-kde":
            group = "kde.desktop"
        elif group == "desktop-xfce":
            group = "xfce.desktop"
        elif group == "desktop-other":
            group = "other.desktop"
        elif group == "admin-tools":
            group = "admin.tools"
        elif group == "power-management":
            group = "power.management"

        package_list = []

        try:
            for key in self.groupdb.get_group_components(group):
                for pkg in self.componentdb.get_packages(key, walk = False):
                    package_list.append(pkg)
            package_list = list(set(package_list))
            for pkg in package_list:
                self.__get_package(pkg, filters)
        except:
            self.error(ERROR_GROUP_NOT_FOUND, "Component %s was not found" % group)

    def search_name(self, filters, package):
        """ Prints a list of packages contains search term in its name """
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for pkg in pisi.api.search_package([package]):
            self.__get_package(pkg, filters)

    def update_packages(self, package_ids):
        """ Updates given package to its latest version """
        # FIXME: fetch/install progress
        self.allow_cancel(False)
        self.percentage(None)

        package = self.get_package_from_id(package_ids[0])[0]

        if self.installdb.has_package(package):
            try:
                pisi.api.upgrade([package])
            except pisi.Error,e:
                self.error(ERROR_UNKNOWN, e)
        else:
            self.error(ERROR_PACKAGE_NOT_INSTALLED, "Package is already installed")

    def update_system(self):
        """ Updates all available packages """
        # FIXME: fetch/install progress
        self.allow_cancel(False)
        self.percentage(None)

        if not len(pisi.api.list_upgradable()) > 0:
            self.error(ERROR_NO_PACKAGES_TO_UPDATE, "System is already up2date")

        try:
            pisi.api.upgrade(pisi.api.list_upgradable())
        except pisi.Error,e:
            self.error(ERROR_UNKNOWN, e)

def main():
    backend = PackageKitPisiBackend('')
    backend.dispatcher(sys.argv[1:])

if __name__ == "__main__":
    main()

