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
# Copyright (C) 2008 Anders F Bjorklund <afb@users.sourceforge.net>

import smart
from smart.interface import Interface
from smart.progress import Progress
from smart.fetcher import Fetcher
from packagekit.backend import PackageKitBaseBackend, INFO_INSTALLED, \
        INFO_AVAILABLE, INFO_NORMAL, FILTER_NOT_INSTALLED, FILTER_INSTALLED, \
        INFO_SECURITY, INFO_BUGFIX, INFO_ENHANCEMENT, \
        ERROR_REPO_NOT_FOUND, ERROR_PACKAGE_ALREADY_INSTALLED, \
        ERROR_PACKAGE_DOWNLOAD_FAILED
from packagekit.package import PackagekitPackage
from packagekit.enums import *
import re

# Global vars
pkpackage = PackagekitPackage()

def needs_cache(func):
    """ Load smart's channels, and save the cache when done. """
    def cache_wrap(obj, *args, **kwargs):
        # Smart's usual output is: (delocalized)
        # Loading cache...
        # Updating cache...    ########## [100%]
        #
        obj.status(STATUS_REQUEST) # ???
        obj.ctrl.reloadChannels()
        result = func(obj, *args, **kwargs)
        obj.ctrl.saveSysConf()
        return result
    return cache_wrap


class PackageKitSmartInterface(Interface):

    def __init__(self, ctrl, backend):
        Interface.__init__(self, ctrl)
        self._progress = PackageKitSmartProgress(True, backend)

    def getProgress(self, obj, hassub=False):
        self._progress.setHasSub(hassub)
        self._progress.setFetcherMode(isinstance(obj, Fetcher))
        return self._progress

class PackageKitSmartProgress(Progress):

    def __init__(self, hassub, backend):
        Progress.__init__(self)
        self._hassub = hassub
        self._backend = backend
        self._oldstatus = None
            
    def setFetcherMode(self, flag):
        if flag:
            self._oldstatus = self._backend._status
            self._backend.status(STATUS_DOWNLOAD)
            self._backend.percentage(0)

    def stop(self):
        Progress.stop(self)
        if self._oldstatus:
            self._backend.percentage(100)
            self._backend.status(self._oldstatus)
            self._oldstatus = None

    def expose(self, topic, percent, subkey, subtopic, subpercent, data, done):
        self._backend.percentage(percent)

class PackageKitSmartBackend(PackageKitBaseBackend):

    _status = STATUS_UNKNOWN

    def __init__(self, args):
        PackageKitBaseBackend.__init__(self, args)

        self.ctrl = smart.init()
        smart.iface.object = PackageKitSmartInterface(self.ctrl, self)
        smart.initPlugins()
        smart.initPsyco()

    def status(self, state):
        PackageKitBaseBackend.status(self, state)
        self._status = state

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
        self.status(STATUS_DEP_RESOLVE)
        trans.run()
        self.status(STATUS_INSTALL)
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

        self.status(STATUS_DEP_RESOLVE)
        trans.run()
        self.status(STATUS_INSTALL)
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
        self.status(STATUS_DEP_RESOLVE)
        trans.run()
        self.status(STATUS_REMOVE)
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
        self.status(STATUS_DEP_RESOLVE)
        trans.run()
        self.status(STATUS_UPDATE)
        self.ctrl.commitTransaction(trans, confirm=False)

    @needs_cache
    def download_packages(self, directory, packageids):
        packages = []
        for packageid in packageids:
            ratio, results, suggestions = self._search_packageid(packageid)
            packages.extend(self._process_search_results(results))

        if len(packages) < 1:
            return
        self.status(PK_STATUS_ENUM_DOWNLOAD)
        self.ctrl.downloadPackages(packages, targetdir=directory)

    @needs_cache
    def update_system(self):
        cache = self.ctrl.getCache()

        trans = smart.transaction.Transaction(cache,
                smart.transaction.PolicyUpgrade)

        for package in cache.getPackages():
            if package.installed:
                trans.enqueue(package, smart.transaction.UPGRADE)

        self.status(STATUS_DEP_RESOLVE)
        trans.run()
        self.status(STATUS_UPDATE)
        self.ctrl.commitTransaction(trans, confirm=False)

    @needs_cache
    def get_updates(self, filter):
        cache = self.ctrl.getCache()

        trans = smart.transaction.Transaction(cache,
                smart.transaction.PolicyUpgrade)

        for package in cache.getPackages():
            if package.installed:
                trans.enqueue(package, smart.transaction.UPGRADE)

        self.status(STATUS_DEP_RESOLVE)
        trans.run()
        self.status(STATUS_INFO)
        for (package, op) in trans.getChangeSet().items():
            if op == smart.transaction.INSTALL:
                status = self._get_status(package)
                self._show_package(package, status)

    @needs_cache
    def resolve(self, filters, packagename):
        self.status(STATUS_QUERY)
        ratio, results, suggestions = self.ctrl.search(packagename)
        for result in results:
            if self._package_passes_filters(result, filters):
                self._show_package(result)

    @needs_cache
    def search_name(self, filters, packagename):
        globbed = "*%s*" % packagename
        self.status(STATUS_QUERY)
        ratio, results, suggestions = self.ctrl.search(globbed)

        packages = self._process_search_results(results)

        for package in packages:
            if self._package_passes_filters(package, filters):
                self._show_package(package)

    @needs_cache
    def search_file(self, filters, searchstring):
        self.status(STATUS_QUERY)
        packages = self.ctrl.getCache().getPackages()
        for package in packages:
            if self._package_passes_filters(package, filters):
                # FIXME: Only installed packages have path lists.
                paths = []
                for loader in package.loaders:
                    info = loader.getInfo(package)
                    paths = info.getPathList()
                    if len(paths) > 0:
                        break
                if searchstring in paths:
                    self._show_package(package)

    @needs_cache
    def search_group(self, filters, searchstring):
        self.status(STATUS_QUERY)
        packages = self.ctrl.getCache().getPackages()
        for package in packages:
            if self._package_passes_filters(package, filters):
                info = package.loaders.keys()[0].getInfo(package)
                group = info.getGroup()
                if group in self.GROUPS:
                    group = self.GROUPS[group]
                    if searchstring in group:
                        self._show_package(package)

    @needs_cache
    def search_details(self, filters, searchstring):
        self.status(STATUS_QUERY)
        packages = self.ctrl.getCache().getPackages()
        for package in packages:
            if self._package_passes_filters(package, filters):
                info = package.loaders.keys()[0].getInfo(package)
                if searchstring in info.getDescription():
                    self._show_package(package)

    @needs_cache
    def get_packages(self, filters):
        self.status(STATUS_QUERY)
        packages = self.ctrl.getCache().getPackages()
        for package in packages:
            if self._package_passes_filters(package, filters):
                self._show_package(package)

    @needs_cache
    def what_provides(self, filters, provides_type, search):
        self.status(STATUS_QUERY)
        # FIXME: provides_type is not used (== PROVIDES_ANY)
        providers = self.ctrl.getCache().getProvides(search)
        for provider in providers:
            for package in provider.packages:
                if self._package_passes_filters(package, filters):
                    self._show_package(package)

    def refresh_cache(self):
        self.status(STATUS_REFRESH_CACHE)
        self.ctrl.rebuildSysConfChannels()
        self.ctrl.reloadChannels(None, caching=smart.const.NEVER)
        self.ctrl.saveSysConf()

    GROUPS = {
    # RPM
    'Amusement/Games'                         : GROUP_GAMES,
    'Amusement/Graphics'                      : GROUP_GRAPHICS,
    'Applications/Archiving'                  : GROUP_OTHER,
    'Applications/Communications'             : GROUP_COMMUNICATION,
    'Applications/Databases'                  : GROUP_OTHER,
    'Applications/Editors'                    : GROUP_PUBLISHING,
    'Applications/Emulators'                  : GROUP_OTHER,
    'Applications/Engineering'                : GROUP_OTHER,
    'Applications/File'                       : GROUP_OTHER,
    'Applications/Internet'                   : GROUP_INTERNET,
    'Applications/Multimedia'                 : GROUP_MULTIMEDIA,
    'Applications/Productivity'               : GROUP_OTHER,
    'Applications/Publishing'                 : GROUP_PUBLISHING,
    'Applications/System'                     : GROUP_SYSTEM,
    'Applications/Text'                       : GROUP_PUBLISHING,
    'Development/Debuggers'                   : GROUP_PROGRAMMING,
    'Development/Languages'                   : GROUP_PROGRAMMING,
    'Development/Libraries'                   : GROUP_PROGRAMMING,
    'Development/System'                      : GROUP_PROGRAMMING,
    'Development/Tools'                       : GROUP_PROGRAMMING,
    'Documentation'                           : GROUP_DOCUMENTATION,
    'System Environment/Base'                 : GROUP_SYSTEM,
    'System Environment/Daemons'              : GROUP_SYSTEM,
    'System Environment/Kernel'               : GROUP_SYSTEM,
    'System Environment/Libraries'            : GROUP_SYSTEM,
    'System Environment/Shells'               : GROUP_SYSTEM,
    'User Interface/Desktops'                 : GROUP_DESKTOP_OTHER,
    'User Interface/X'                        : GROUP_DESKTOP_OTHER,
    'User Interface/X Hardware Support'       : GROUP_DESKTOP_OTHER,
    # DEB
    "admin"                                   : GROUP_ADMIN_TOOLS,
    "base"                                    : GROUP_SYSTEM,
    "comm"                                    : GROUP_COMMUNICATION,
    "devel"                                   : GROUP_PROGRAMMING,
    "doc"                                     : GROUP_DOCUMENTATION,
    "editors"                                 : GROUP_PUBLISHING,
    "electronics"                             : GROUP_ELECTRONICS,
    "embedded"                                : GROUP_SYSTEM,
    "games"                                   : GROUP_GAMES,
    "GNOME"                                   : GROUP_DESKTOP_GNOME,
    "graphics"                                : GROUP_GRAPHICS,
    "hamradio"                                : GROUP_COMMUNICATION,
    "interpreters"                            : GROUP_PROGRAMMING,
    "kde"                                     : GROUP_DESKTOP_KDE,
    "libdevel"                                : GROUP_PROGRAMMING,
    "lib"                                     : GROUP_SYSTEM,
    "mail"                                    : GROUP_INTERNET,
    "math"                                    : GROUP_SCIENCE,
    "misc"                                    : GROUP_OTHER,
    "net"                                     : GROUP_NETWORK,
    "news"                                    : GROUP_INTERNET,
    "oldlibs"                                 : GROUP_LEGACY,
    "otherosfs"                               : GROUP_SYSTEM,
    "perl"                                    : GROUP_PROGRAMMING,
    "python"                                  : GROUP_PROGRAMMING,
    "science"                                 : GROUP_SCIENCE,
    "shells"                                  : GROUP_SYSTEM,
    "sound"                                   : GROUP_MULTIMEDIA,
    "tex"                                     : GROUP_PUBLISHING,
    "text"                                    : GROUP_PUBLISHING,
    "utils"                                   : GROUP_ACCESSORIES,
    "web"                                     : GROUP_INTERNET,
    "x11"                                     : GROUP_DESKTOP_OTHER,
    "unknown"                                 : GROUP_UNKNOWN,
    "alien"                                   : GROUP_UNKNOWN,
    "translations"                            : GROUP_LOCALIZATION,
    # Slack
    "Slackware"                               : GROUP_UNKNOWN
    }
    
    @needs_cache
    def get_details(self, packageids):
        self.status(STATUS_INFO)
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

            if hasattr(info, 'getLicense'):
                license = info.getLicense()
            else:
                license = "unknown"

            group = info.getGroup()
            if group in self.GROUPS:
                group = self.GROUPS[group]
            else:
                group = "unknown"

            self.details(packageid, license, group, description, url,
                    pkgsize)

    @needs_cache
    def get_files(self, packageids):
        self.status(STATUS_INFO)
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

    def _text_to_boolean(self,text):
        if text == 'true' or text == 'TRUE':
            return True
        elif text == 'yes' or text == 'YES':
            return True
        return False

    @needs_cache
    def get_depends(self, filters, packageids, recursive_text):
        recursive = self._text_to_boolean(recursive_text)
        self.status(STATUS_INFO)
        for packageid in packageids:
            ratio, results, suggestions = self._search_packageid(packageid)

            packages = self._process_search_results(results)

            if len(packages) != 1:
                return

            package = packages[0]

            providers = {}
            for required in package.requires:
                for provider in required.providedby:
                    for package in provider.packages:
                        if not providers.has_key(package):
                            providers[package] = True

            for package in providers.keys():
                if self._package_passes_filters(package, filters):
                    self._show_package(package)

    @needs_cache
    def get_requires(self, filters, packageids, recursive_text):
        recursive = self._text_to_boolean(recursive_text)
        self.status(STATUS_INFO)
        for packageid in packageids:
            ratio, results, suggestions = self._search_packageid(packageid)

            packages = self._process_search_results(results)

            if len(packages) != 1:
                return

            package = packages[0]

            requirers = {}
            for provided in package.provides:
                for requirer in provided.requiredby:
                    for package in requirer.packages:
                        if not requirers.has_key(package):
                            requirers[package] = True

            for package in requirers.keys():
                if self._package_passes_filters(package, filters):
                    self._show_package(package)

    def get_repo_list(self, filters):
        self.status(STATUS_INFO)
        channels = smart.sysconf.get("channels", ())
        for alias in channels:
            channel = smart.sysconf.get(("channels", alias))
            name = channel.get("name", alias)
            parsed = smart.channel.parseChannelData(channel)
            enabled = 'true'
            if channel.has_key('disabled') and channel['disabled'] == 'yes':
                enabled = 'false'
            channel['alias'] = alias
            if self._channel_passes_filters(channel, filters):
                self.repo_detail(alias, name, enabled)

    def repo_enable(self, repoid, enable):
        self.status(STATUS_INFO)
        if smart.sysconf.has(("channels", repoid)):
            if enable == "true":
                smart.sysconf.remove(("channels", repoid, "disabled"))
            else:
                smart.sysconf.set(("channels", repoid, "disabled"), "yes")
            self.ctrl.saveSysConf()
        else:
            self.error(ERROR_REPO_NOT_FOUND, "repo %s was not found" % repoid)

    def repo_set_data(self, repoid, param, value):
        self.status(STATUS_INFO)
        if smart.sysconf.has(("channels", repoid)):
            smart.sysconf.set(("channels", repoid, param), value)
            self.ctrl.saveSysConf()
        else:
            self.error(ERROR_REPO_NOT_FOUND, "repo %s was not found" % repoid)

    systemchannel = None # unfortunately package strings depend on system

    def _splitpackage(self, package):
        from smart.backends.rpm.base import RPMPackage
        from smart.backends.deb.base import DebPackage
        from smart.backends.slack.base import SlackPackage
        if isinstance(package, RPMPackage):
            version, arch = package.version.split('@')
        elif isinstance(package, DebPackage):
            version, arch = package.version, smart.backends.deb.base.DEBARCH
        elif isinstance(package, SlackPackage):
            ver, arch, rel = package.version.rsplit('-')
            version = "%s-%s" % (ver, rel)
        else:
            import os
            machine = os.uname()[-1]
            if machine == "Power Macintosh": #<sigh>
                machine = "ppc"
            version, arch = package.version, machine
        return package.name, version, arch

    def _joinpackage(self, name, version, arch):
        if not self.systemchannel:
            channels = smart.sysconf.get("channels", ())
            # FIXME: should look by type, not by alias
            if "rpm-sys" in channels:
                self.systemchannel = "rpm-sys"
            elif "deb-sys" in channels:
                self.systemchannel = "deb-sys"
            elif "slack-sys" in channels:
                self.systemchannel = "slack-sys"
        if self.systemchannel == "rpm-sys":
            pkg = "%s-%s@%s" % (name, version, arch)
        elif self.systemchannel == "deb-sys":
            pkg = "%s_%s" % (name, version)
        elif self.systemchannel == "slack-sys":
            ver, rel = version.rsplit("-")
            pkg = "%s-%s-%s-%s" % (name, ver, arch, rel)
        else:
            pkg = "%s-%s" % (name, version)
        return pkg

    def _search_packageid(self, packageid):
        idparts = packageid.split(';')
        # note: currently you can only search in channels native to system
        packagestring = self._joinpackage(idparts[0], idparts[1], idparts[2])
        ratio, results, suggestions = self.ctrl.search(packagestring)

        return (ratio, results, suggestions)

    def _show_package(self, package, status=None):
        if not status:
            if package.installed:
                status = INFO_INSTALLED
            else:
                status = INFO_AVAILABLE
        name, version, arch = self._splitpackage(package)
        for loader in package.loaders:
            channel = loader.getChannel()
            if package.installed and not channel.getType().endswith('-sys'):
                continue
            info = loader.getInfo(package)
            self.package(pkpackage.get_package_id(name, version, arch,
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

    def _channel_passes_filters(self, channel, filters):
        filterlist = filters.split(';')
        if FILTER_NOT_DEVELOPMENT in filterlist:
            if channel['type'] == 'rpm-md':
                repo = channel['alias']
                if repo.endswith('-debuginfo'):
                    return False
                if repo.endswith('-testing'):
                    return False
                if repo.endswith('-debug'):
                    return False
                if repo.endswith('-development'):
                    return False
                if repo.endswith('-source'):
                    return False
        return True   

    def _package_is_graphical(self, package):
        from smart.backends.rpm.base import RPMPackage
        from smart.backends.deb.base import DebPackage
        if isinstance(package, RPMPackage):
            regex = re.compile(r'(qt)|(gtk)')
            for required in package.requires:
                for provider in required.providedby:
                    for package in provider.packages:
                        if regex.search(package.name):
                            return True
            return False
        elif isinstance(package, DebPackage):
            group = package.getGroup().split('/')[-1].lower()
            return group in ['x11', 'gnome', 'kde']
        else:
            return None

    def _package_is_development(self, package):
        from smart.backends.rpm.base import RPMPackage
        from smart.backends.deb.base import DebPackage
        if isinstance(package, RPMPackage):
            regex = re.compile(r'(-devel)|(-dgb)|(-static)')
            return bool(regex.search(package.name))
        elif isinstance(package, DebPackage):
            group = package.getGroup().split('/')[-1].lower()
            return package.name.endswith("-dev") or \
                   package.name.endswith("-dbg") or \
                   group in ['devel', 'libdevel']
        else:
            return None

    def _package_passes_filters(self, package, filters):
        filterlist = filters.split(';')
        if FILTER_NOT_INSTALLED in filterlist and package.installed:
            return False
        elif FILTER_INSTALLED in filterlist and not package.installed:
            return False
        else:
            loader = package.loaders.keys()[0]
            info = loader.getInfo(package)
            for filter in filterlist:
                if filter in (FILTER_GUI, FILTER_NOT_GUI):
                    graphical = self._package_is_graphical(package)
                    if graphical is None: # tristate boolean
                        return None
                    if filter == FILTER_GUI and not graphical:
                        return False
                    if filter == FILTER_NOT_GUI and graphical:
                        return False
                if filter in (FILTER_DEVELOPMENT, FILTER_NOT_DEVELOPMENT):
                    development = self._package_is_development(package)
                    if development is None: # tristate boolean
                        return None
                    if filter == FILTER_DEVELOPMENT and not development:
                        return False
                    if filter == FILTER_NOT_DEVELOPMENT and development:
                        return False
                if filter in (FILTER_FREE, FILTER_NOT_FREE):
                    if hasattr(info, 'getLicense'):
                        license = info.getLicense()
                        free = pkpackage.check_license_field(license)
                        if free is None: # tristate boolean
                            return None
                        if filter == FILTER_FREE and not free:
                            return False
                        if filter == FILTER_NOT_FREE and free:
                            return False
                    else:
                        self.error(ERROR_FILTER_INVALID, \
                                   "filter %s not supported" % filter)
                        return False
        return True
