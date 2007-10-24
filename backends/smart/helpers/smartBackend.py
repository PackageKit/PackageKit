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

# Copyright (C) 2007 James Bowes <jbowes@dangerouslyinc.com>

import smart
from packagekit.backend import PackageKitBaseBackend, INFO_INSTALLED, \
        INFO_AVAILABLE

class PackageKitSmartBackend(PackageKitBaseBackend):

    def __init__(self, args):
        PackageKitBaseBackend.__init__(self, args)

        self.ctrl = smart.init()
        self.ctrl.reloadChannels()
        self.ctrl.getCache()

        # FIXME: Only pulsing progress for now.
        self.percentage(None)

    def install(self, packageid):
        idparts = packageid.split(';')
        packagestring = "%s-%s@%s" % (idparts[0], idparts[1], idparts[2])
        ratio, results, suggestions = self.ctrl.search(packagestring)

        packages = self._process_search_results(results)

        available = [package for package in packages if not package.installed]
        if len(available) != 1:
            return
        package = available[0]
        trans = smart.transaction.Transaction(self.ctrl.getCache(),
                smart.transaction.PolicyInstall)
        trans.getPolicy()
        trans.enqueue(package, smart.transaction.INSTALL)
        trans.run()
        self.ctrl.commitTransaction(trans, confirm=False)

    def remove(self, allowdeps, packageid):

        idparts = packageid.split(';')
        packagestring = "%s-%s@%s" % (idparts[0], idparts[1], idparts[2])
        ratio, results, suggestions = self.ctrl.search(packagestring)

        packages = self._process_search_results(results)

        installed = [package for package in packages if package.installed]
        if len(installed) != 1:
            return
        package = installed[0]
        trans = smart.transaction.Transaction(self.ctrl.getCache(),
                smart.transaction.PolicyRemove)
        trans.getPolicy()
        trans.enqueue(package, smart.transaction.REMOVE)
        trans.run()
        self.ctrl.commitTransaction(trans, confirm=False)

    def resolve(self, filters, packagename):
        ratio, results, suggestions = self.ctrl.search(packagename)
        for result in results:
            self._show_package(result)

    def search_name(self, filters, packagename):
        globbed = "*%s*" % packagename
        ratio, results, suggestions = self.ctrl.search(globbed)

        packages = self._process_search_results(results)

        for package in packages:
            self._show_package(package)

    def _show_package(self, package):
        if package.installed:
            status = INFO_INSTALLED
        else:
            status = INFO_AVAILABLE
        version, arch = package.version.split('@')
        for loader in package.loaders:
            channel = loader.getChannel()
            self.package(self.get_package_id(package.name, version, arch,
                channel.getAlias()), status, None)

    def _process_search_results(self, results):
        packages = []
        for obj in results:
            if isinstance(obj, smart.cache.Package):
                packages.append(obj)

        if not packages:
            for obj in results:
                for pkg in obj.packages:
                    packages.append(pkg)

        return packages
