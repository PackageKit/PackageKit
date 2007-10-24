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

class PackageKitPisiBackend(PackageKitBaseBackend):

    # Currently we only support i686
    arch = "i686"

    def __init__(self, args):
        PackageKitBaseBackend.__init__(self, args)

        self.installdb = pisi.db.installdb.InstallDB()
        self.componentdb = pisi.db.componentdb.ComponentDB()
        self.packagedb = pisi.db.packagedb.PackageDB()
        self.repodb = pisi.db.repodb.RepoDB()

    def __get_package_version(self, package):
        """ Returns version string of given package """
        # Internal FIXME: PiSi may provide this
        if package.build is not None:
            version = "%s-%s-%s" % (package.version, package.release, package.build)
        else:
            version = "%s-%s" % (package.version, package.release)
        return version

    def __get_package(self, package):
        """ Returns package object suitable for other methods """
        if self.installdb.has_package(package):
            status = INFO_INSTALLED
            pkg = self.installdb.get_package(package)
        elif self.packagedb.has_package(package):
            status = INFO_AVAILABLE
            pkg = self.packagedb.get_package(package)
        else:
            self.error(ERROR_INTERNAL_ERROR, "Package was not found")

        version = self.__get_package_version(pkg)

        id = self.get_package_id(pkg.name, version, self.arch, "")
        return self.package(id, status, pkg.summary)

    def resolve(self, filter, package):
        """ Turns a single package name into a package_id suitable for the other methods """
        self.allow_interrupt(True);
        self.percentage(None)
        self.__get_package(package)

    def remove(self, deps, package_id):
        """ Removes given package from system"""
        self.allow_interrupt(False);
        self.percentage(None)

        package = self.get_package_from_id(package_id)[0]

        if self.installdb.has_package(package):
            self.status(STATE_REMOVE)
            try:
                pisi.api.remove([package])
            except pisi.Error,e:
                # system.base packages cannot be removed from system
                self.error(ERROR_CANNOT_REMOVE_SYSTEM_PACKAGE, e)
        else:
            self.error(ERROR_PACKAGE_NOT_INSTALLED, "Package is not installed")

    def install(self, package_id):
        """ Installs given package into system"""
        # FIXME: fetch/install progress
        self.allow_interrupt(False);
        self.percentage(None)

        package = self.get_package_from_id(package_id)[0]

        if self.packagedb.has_package(package):
            self.status(STATE_INSTALL)
            try:
                pisi.api.install([package])
            except pisi.Error,e:
                self.error(ERROR_INTERNAL_ERROR, e)
        else:
            self.error(ERROR_PACKAGE_NOT_INSTALLED, "Package is already installed")

    def update(self, package_id):
        """ Updates given package to its latest version """
        # FIXME: fetch/install progress
        self.allow_interrupt(False);
        self.percentage(None)

        package = self.get_package_from_id(package_id)[0]

        if self.installdb.has_package(package):
            try:
                pisi.api.upgrade([package])
            except pisi.Error,e:
                self.error(ERROR_INTERNAL_ERROR, e)
        else:
            self.error(ERROR_PACKAGE_NOT_INSTALLED, "Package is already installed")

    def update_system(self):
        """ Updates all available packages """
        # FIXME: fetch/install progress
        self.allow_interrupt(False);
        self.percentage(None)

        if not len(pisi.api.list_upgradable()) > 0:
            self.error(ERROR_INTERNAL_ERROR, "System is already up2date")

        try:
            pisi.api.upgrade(pisi.api.list_upgradable())
        except pisi.Error,e:
            self.error(ERROR_INTERNAL_ERROR, e)

    def get_repo_list(self):
        """ Prints available repositories """
        self.allow_interrupt(True);
        self.percentage(None)

        for repo in pisi.api.list_repos():
            # Internal FIXME: What an ugly way to get repo uri
            self.repo_detail(repo, self.repodb.get_repo(repo).indexuri.get_uri(), "true")

    def get_description(self, package_id):
        """ Prints a detailed description for a given package """
        self.allow_interrupt(True)
        self.percentage(None)

        package = self.get_package_from_id(package_id)[0]
        pkg = self.packagedb.get_package(package)

        if pkg:
            self.description("%s-%s" % (pkg.name, self.__get_package_version(pkg)), 
                            pkg.license, 
                            pkg.partOf, 
                            pkg.description, 
                            pkg.packageURI, 
                            pkg.packageSize, "")
        else:
            self.error(ERROR_INTERNAL_ERROR, "Package was not found")

    def get_updates(self):
        """ Prints available updates and types """
        self.allow_interrupt(True);
        self.percentage(None)

        for package in pisi.api.list_upgradable():

            pkg = self.packagedb.get_package(package)

            version = self.__get_package_version(pkg)
            id = self.get_package_id(pkg.name, version, self.arch, "")

            # Internal FIXME: PiSi must provide this information as a single API call :(
            updates = [i for i in self.packagedb.get_package(package).history if pisi.version.Version(i.release) > pisi.version.Version(pkg.release)]
            if pisi.util.any(lambda i:i.type == "security", updates):
                self.package(id, INFO_SECURITY, pkg.summary)
            else:
                self.package(id, INFO_NORMAL, pkg.summary)

    def refresh_cache(self):
        """ Updates repository indexes """
        self.allow_interrupt(False);
        self.percentage(0)

        slice = (100/len(pisi.api.list_repos()))/2

        percentage = 0
        for repo in pisi.api.list_repos():
            pisi.api.update_repo(repo)
            percentage += slice
            self.percentage(percentage)

        self.percentage(100)

    def get_requires(self, package_id):
        """ Prints a list of requires for a given package """
        self.allow_interrupt(True)
        self.percentage(None)

        package = self.get_package_from_id(package_id)[0]

        # FIXME: Handle packages which is not installed from repository
        for pkg in self.packagedb.get_rev_deps(package):
            self.__get_package(pkg[0])

    def get_depends(self, package_id):
        """ Prints a list of depends for a given package """
        self.allow_interrupt(True)
        self.percentage(None)

        package = self.get_package_from_id(package_id)[0]

        for pkg in self.packagedb.get_package(package).runtimeDependencies():
            # Internal FIXME: PiSi API has really inconsistent for return types and arguments!
            self.__get_package(pkg.package)
