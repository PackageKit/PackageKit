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

    def get_package_version(self, pkg):
        if pkg.build is not None:
            version = "%s-%s-%s" % (pkg.version, pkg.release, pkg.build)
        else:
            version = "%s-%s" % (pkg.version, pkg.release)
        return version

    def resolve(self, filter, package_id):
        """ turns a single package name into a package_id suitable for the other methods. """

        self.allow_interrupt(True);
        self.percentage(None)

        if self.installdb.has_package(package_id):
            status = INFO_INSTALLED
            pkg = self.installdb.get_package(package_id)
        elif self.packagedb.has_package(package_id):
            status = INFO_AVAILABLE
            pkg = self.packagedb.get_package(package_id)
        else:
            self.error(ERROR_INTERNAL_ERROR, "Package was not found")

        version = self.get_package_version(pkg)

        id = self.get_package_id(pkg.name, version, self.arch, "")
        self.package(id, status, pkg.summary)

    def remove(self, deps, package_id):
        """ removes given package """
        package = self.get_package_from_id(package_id)[0]

        self.allow_interrupt(False);
        self.percentage(None)

        if self.installdb.has_package(package):
            self.status(STATE_REMOVE)
            try:
                pisi.api.remove([package])
            except pisi.Error,e:
                self.error(ERROR_CANNOT_REMOVE_SYSTEM_PACKAGE, e)
        else:
            self.error(ERROR_PACKAGE_NOT_INSTALLED, "Package is not installed")

    def install(self, package_id):
        """ installs given package """
        package = self.get_package_from_id(package_id)[0]

        self.allow_interrupt(False);
        self.percentage(None)

        if self.packagedb.has_package(package):
            self.status(STATE_INSTALL)
            try:
                pisi.api.install([package])
            except pisi.Error,e:
                self.error(ERROR_INTERNAL_ERROR, e)
        else:
            self.error(ERROR_PACKAGE_NOT_INSTALLED, "Package is already installed")

    def update(self, package_id):
        # FIXME: fetch/install progress
        self.allow_interrupt(False);
        self.percentage(0)

        package = self.get_package_from_id(package_id)[0]

        if self.installdb.has_package(package):
            try:
                pisi.api.upgrade([package])
            except pisi.Error,e:
                self.error(ERROR_INTERNAL_ERROR, e)
        else:
            self.error(ERROR_PACKAGE_NOT_INSTALLED, "Package is already installed")

    def get_repo_list(self):
        self.allow_interrupt(True);
        self.percentage(None)

        for repo in pisi.api.list_repos():
            self.repo_detail(repo, self.repodb.get_repo(repo).indexuri.get_uri(), "true")

    def get_updates(self):
        for package in pisi.api.list_upgradable():

            pkg = self.installdb.get_package(package)

            version = self.get_package_version(pkg)
            id = self.get_package_id(pkg.name, version, self.arch, "")
            
            # Internal FIXME: PiSi must provide this information as a single API call :(
            updates = [i for i in self.packagedb.get_package(package).history if pisi.version.Version(i.release) > pisi.version.Version(pkg.release)]
            if pisi.util.any(lambda i:i.type == "security", updates):
                self.package(id, INFO_SECURITY, pkg.summary)
            else:
                self.package(id, INFO_NORMAL, pkg.summary)

    def refresh_cache(self):
        self.allow_interrupt(False);

        self.percentage(0)

        slice = (100/len(pisi.api.list_repos()))/2

        percentage = 0
        for repo in pisi.api.list_repos():
            pisi.api.update_repo(repo)
            percentage += slice
            self.percentage(percentage)

        self.percentage(100)
