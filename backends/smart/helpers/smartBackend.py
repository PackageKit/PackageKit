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
        INFO_AVAILABLE, INFO_NORMAL, FILTER_NOT_INSTALLED, FILTER_INSTALLED, \
        INFO_SECURITY, INFO_BUGFIX, INFO_ENHANCEMENT, \
        ERROR_REPO_NOT_FOUND, ERROR_PACKAGE_ALREADY_INSTALLED, \
        ERROR_PACKAGE_DOWNLOAD_FAILED
from packagekit.package import PackagekitPackage

# Global vars
pkpackage = PackagekitPackage()

def needs_cache(func):
    """ Load smart's channels, and save the cache when done. """
    def cache_wrap(obj, *args, **kwargs):
        obj.ctrl.reloadChannels()
        result = func(obj, *args, **kwargs)
        obj.ctrl.saveSysConf()
        return result
    return cache_wrap


class PackageKitSmartBackend(PackageKitBaseBackend):

    def __init__(self, args):
        PackageKitBaseBackend.__init__(self, args)

        # FIXME: Only pulsing progress for now.
        self.percentage(None)

        self.ctrl = smart.init()
        # Use the dummy interface to quiet output.
        smart.iface.object = smart.interface.Interface(self.ctrl)
        smart.initPlugins()
        smart.initPsyco()

    @needs_cache
    def install_packages(self, packageids):
        packages = []
        for packageid in packageids:
            ratio, results, suggestions = self._search_packageid(packageid)
            packages.extend(self._process_search_results(results))

        available = [package for package in packages if not package.installed]
        if len(available) < 1:
            return
        trans = smart.transaction.Transaction(self.ctrl.getCache(),
                smart.transaction.PolicyInstall)
        for package in available:
            trans.enqueue(package, smart.transaction.INSTALL)
        trans.run()
        self.ctrl.commitTransaction(trans, confirm=False)

    @needs_cache
    def install_files(self, trusted, paths):
        for path in paths:
            self.ctrl.addFileChannel(path)
        self.ctrl.reloadChannels()
        trans = smart.transaction.Transaction(self.ctrl.getCache(),
                smart.transaction.PolicyInstall)

        for channel in self.ctrl.getFileChannels():
            for loader in channel.getLoaders():
                for package in loader.getPackages():
                    if package.installed:
                        self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                                'Package %s is already installed' % package)
                    trans.enqueue(package, smart.transaction.INSTALL)

        trans.run()
        self.ctrl.commitTransaction(trans, confirm=False)

    @needs_cache
    def remove_packages(self, packageids):
        packages = []
        for packageid in packageids:
            ratio, results, suggestions = self._search_packageid(packageid)
            packages.extend(self._process_search_results(results))

        installed = [package for package in packages if package.installed]
        if len(installed) < 1:
            return
        trans = smart.transaction.Transaction(self.ctrl.getCache(),
                smart.transaction.PolicyRemove)
        for package in installed:
            trans.enqueue(package, smart.transaction.REMOVE)
        trans.run()
        self.ctrl.commitTransaction(trans, confirm=False)

    @needs_cache
    def update_packages(self, packageids):
        packages = []
        for packageid in packageids:
            ratio, results, suggestions = self._search_packageid(packageid)
            packages.extend(self._process_search_results(results))

        installed = [package for package in packages if package.installed]
        if len(installed) < 1:
            return
        trans = smart.transaction.Transaction(self.ctrl.getCache(),
                smart.transaction.PolicyUpgrade)
        for package in installed:
            trans.enqueue(package, smart.transaction.UPGRADE)
        trans.run()
        self.ctrl.commitTransaction(trans, confirm=False)

    @needs_cache
    def download_packages(self, directory, packageids):
        packages = []
        for packageid in packageids:
            ratio, results, suggestions = self._search_packageid(packageid)
            packages.extend(self._process_search_results(results))

        if len(packages) < 1:
            return
        self.ctrl.downloadPackages(packages, targetdir=directory)

    @needs_cache
    def update_system(self):
        cache = self.ctrl.getCache()

        trans = smart.transaction.Transaction(cache,
                smart.transaction.PolicyUpgrade)

        for package in cache.getPackages():
            if package.installed:
                trans.enqueue(package, smart.transaction.UPGRADE)

        trans.run()
        self.ctrl.commitTransaction(trans, confirm=False)

    @needs_cache
    def get_updates(self, filter):
        cache = self.ctrl.getCache()

        trans = smart.transaction.Transaction(cache,
                smart.transaction.PolicyUpgrade)

        for package in cache.getPackages():
            if package.installed:
                trans.enqueue(package, smart.transaction.UPGRADE)

        trans.run()
        for (package, op) in trans.getChangeSet().items():
            if op == smart.transaction.INSTALL:
                status = self._get_status(package)
                self._show_package(package, status)

    @needs_cache
    def resolve(self, filters, packagename):
        ratio, results, suggestions = self.ctrl.search(packagename)
        for result in results:
            if self._passes_filters(result, filters):
                self._show_package(result)

    @needs_cache
    def search_name(self, filters, packagename):
        globbed = "*%s*" % packagename
        ratio, results, suggestions = self.ctrl.search(globbed)

        packages = self._process_search_results(results)

        for package in packages:
            if self._passes_filters(package, filters):
                self._show_package(package)

    @needs_cache
    def search_details(self, filters, searchstring):
        packages = self.ctrl.getCache().getPackages()
        for package in packages:
            if self._passes_filters(package, filters):
                info = package.loaders.keys()[0].getInfo(package)
                if searchstring in info.getDescription():
                    self._show_package(package)

    @needs_cache
    def get_packages(self, filters):
        packages = self.ctrl.getCache().getPackages()
        for package in packages:
            if self._passes_filters(package, filters):
                self._show_package(package)

    def refresh_cache(self):
        self.ctrl.rebuildSysConfChannels()
        self.ctrl.reloadChannels(None, caching=smart.const.NEVER)
        self.ctrl.saveSysConf()

    @needs_cache
    def get_details(self, packageids):
        for packageid in packageids:
            ratio, results, suggestions = self._search_packageid(packageid)

            packages = self._process_search_results(results)

            if len(packages) != 1:
                return

            package = packages[0]
            infos = []
            for loader in package.loaders:
                info = loader.getInfo(package)
                infos.append(info)

            infos.sort()
            info = infos[0]

            version, arch = package.version.split('@')
            description = info.getDescription()
            description = description.replace("\n\n", ";")
            description = description.replace("\n", " ")
            urls = info.getReferenceURLs()
            if urls:
                url = urls[0]
            else:
                url = "unknown"

            pkgsize = None
            seen = {}
            for loader in package.loaders:
                info = loader.getInfo(package)
                for pkgurl in info.getURLs():
                    size = info.getSize(pkgurl)
                    if size:
                        pkgsize = size
                        break
                if pkgsize:
                    break
            if not pkgsize:
                pkgsize = "unknown"

            self.details(packageid, "unknown", "unknown", description, url,
                    pkgsize)

    @needs_cache
    def get_files(self, packageids):
        for packageid in packageids:
            ratio, results, suggestions = self._search_packageid(packageid)

            packages = self._process_search_results(results)

            if len(packages) != 1:
                return

            package = packages[0]
            # FIXME: Only installed packages have path lists.
            paths = []
            for loader in package.loaders:
                info = loader.getInfo(package)
                paths = info.getPathList()
                if len(paths) > 0:
                    break

            self.files(packageid, ";".join(paths))

    @needs_cache
    def get_depends(self, packageids):
        for packageid in packageids:
            ratio, results, suggestions = self._search_packageid(packageid)

            packages = self._process_search_results(results)

            if len(packages) != 1:
                return

            package = packages[0]

            providers = {}
            for required in package.requires:
                for provider in self.ctrl.getCache().getProvides(str(required)):
                    for package in provider.packages:
                        if not providers.has_key(package):
                            providers[package] = True

            for package in providers.keys():
                self._show_package(package)

    def get_repo_list(self, filters):
        channels = smart.sysconf.get("channels", ())
        for alias in channels:
            channel = smart.sysconf.get(("channels", alias))
            name = channel.get("name", alias)
            parsed = smart.channel.parseChannelData(channel)
            enabled = 'true'
            if channel.has_key('disabled') and channel['disabled'] == 'yes':
                enabled = 'false'
            self.repo_detail(alias, name, enabled)

    def repo_enable(self, repoid, enable):
        if smart.sysconf.has(("channels", repoid)):
            if enable == "true":
                smart.sysconf.remove(("channels", repoid, "disabled"))
            else:
                smart.sysconf.set(("channels", repoid, "disabled"), "yes")
            self.ctrl.saveSysConf()
        else:
            self.error(ERROR_REPO_NOT_FOUND, "repo %s was not found" % repoid)

    def _search_packageid(self, packageid):
        idparts = packageid.split(';')
        packagestring = "%s-%s@%s" % (idparts[0], idparts[1], idparts[2])
        ratio, results, suggestions = self.ctrl.search(packagestring)

        return (ratio, results, suggestions)

    def _show_package(self, package, status=None):
        if not status:
            if package.installed:
                status = INFO_INSTALLED
            else:
                status = INFO_AVAILABLE
        version, arch = package.version.split('@')
        for loader in package.loaders:
            channel = loader.getChannel()
            if package.installed and not channel.getType().endswith('-sys'):
                continue
            info = loader.getInfo(package)
            self.package(pkpackage.get_package_id(package.name, version, arch,
                channel.getAlias()), status, info.getSummary())

    def _get_status(self, package):
        flags = smart.pkgconf.testAllFlags(package)
        for flag in flags:
            if flag == 'security':
                return INFO_SECURITY
            elif flag == 'bugfix':
                return INFO_BUGFIX
            elif flag == 'enhancement':
                return INFO_ENHANCEMENT
        return INFO_NORMAL

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

    @staticmethod
    def _passes_filters(package, filters):
        filterlist = filters.split(';')

        return (FILTER_NOT_INSTALLED not in filterlist and package.installed
                or FILTER_INSTALLED not in filterlist and not package.installed)
