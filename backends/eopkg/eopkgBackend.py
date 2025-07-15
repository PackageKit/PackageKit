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
# Copyright (C) 2013 Ikey Doherty <ikey@solusos.com>
# Copyright (C) 2024 Solus Developers <releng@getsol.us>

# Notes to PiSi based distribution maintainers
# /etc/PackageKit/pisi.conf must contain a mapping of PiSi component to
# PackageKit groups for correct operation, i.e.
#   system.utils       = system
#   desktop.gnome      = desktop-gnome
# If you have a BTS you must also provide Bug-Regex and Bug-URI fields, i.e:
#   Bug-Regex = Bug-SolusOS: T(\d+)
#   Bug-URI = http://inf.solusos.com/T%s
# We use simple python string formatting to replace the %s with the first
# matched group in the regular expression. So in the example above, we expect
# to see "Bug-SolusOS: T9" for example, on its own line in a package update
# comment.

import pisi
import pisi.ui
from packagekit.backend import *
from packagekit.package import PackagekitPackage
from packagekit import enums
import os.path
import piksemel
import re
from collections import Counter
from operator import attrgetter


TransactionsStateMap = {
    "download_packages" : STATUS_DOWNLOAD,
    "update_packages"   : STATUS_UPDATE,
    "remove_packages"   : STATUS_REMOVE,
    "install_packages"  : STATUS_INSTALL,
    "install_files"     : STATUS_INSTALL,
    "refresh_cache"     : STATUS_REFRESH_CACHE
}

TransactionsInfoMap = {
    "update_packages"   : INFO_UPDATING,
    "remove_packages"   : INFO_REMOVING,
    "install_packages"  : INFO_INSTALLING,
    "install_files"     : INFO_INSTALLING
}


def _format_str(text):
    """
    Convert a multi line string to a list separated by ';'
    """
    if text:
        lines = text.split('\n')
        return ";".join(lines)
    else:
        return ""


# Override PiSi UI so we can get callbacks for progress and events
class SimplePisiHandler(pisi.ui.UI):

    def __init__(self, base):
        pisi.ui.UI.__init__(self, False, False)
        self.errors = 0
        self.warnings = 0

        # PackageKitPisiBackend
        self.base = base

        # Progress bar helpers
        self.packagestogo = 0
        self.currentpackage = 0

    @staticmethod
    def get_percentage(count, max_count):
        """
        Prepare percentage value used to feed self.percentage()
        """
        if count == 0 or max_count == 0:
            return 0
        percent = int((float(count) / max_count) * 100)
        if percent > 100:
            return 100
        return percent

    def update_percentage(self, downloading=False):
        # Reset counter when switching state i.e. from downloading to installing
        # FIXME: If we get forced upgrades i.e. partial upgrades as part of a pkg
        #        installation, it breaks the progress bar as we switch from INFO_INSTALLING
        #        to INFO_UPDATING and doesn't match up with packagestogo.
        if self.currentpackage == self.packagestogo:
            self.currentpackage = 0
        self.currentpackage += 1
        percent = self.get_percentage(self.currentpackage, self.packagestogo)
        if downloading is False:
            # Reserve 10% of progress to account for usysconf to run triggers
            usysconf_offset = 0.1 * self.packagestogo
            total_percent = percent + usysconf_offset
            self.base._set_percent(total_percent)
        else:
            self.base._set_percent(percent)

    def display_progress(self, **kw):
        percent = self.get_percentage(self.currentpackage, self.packagestogo)
        file_name = kw["filename"]
        if not file_name.startswith("eopkg-index.xml"):
            pkg_name = pisi.util.parse_package_name(file_name)[0]
            self.notify("progress", pkg_name=pkg_name, percent=int(kw['percent']))
        if self.packagestogo > 0:
            # Increase the step offset by 50%
            # e.g. 5 pkgs to install, currentpackage == 3 so update percentage range within 60% to 80%.
            sliced = (100 / self.packagestogo)
            slicedpercent = sliced / 100 * int(kw['percent']) + percent - sliced
            self.base._set_percent(slicedpercent)
        else:
            self.base._set_percent(int(kw['percent']))

    def notify(self, event, **keywords):

        if event == pisi.ui.packagestogo:
            self.packagestogo = len(keywords["order"])
        if event == pisi.ui.downloading:
            self.base._set_status(keywords["package"], INFO_DOWNLOADING)
            self.update_percentage(downloading=True)
        if event == "progress":
            if pisi.db.packagedb.PackageDB().has_package(keywords["pkg_name"]):
                pkg = pisi.db.packagedb.PackageDB().get_package(keywords["pkg_name"])
                self.base.item_progress(self.base._pkg_to_id(pkg), STATUS_DOWNLOAD, keywords['percent'])
        #if event == pisi.ui.cached:
        #   self.base._set_status(keywords["package"], INFO_FINISHED)
        if event == pisi.ui.installing:
            # Pisi doesn't tell us whether it's installing or upgrading in the callback until after it's done it, thanks!
            # This is bad for progress bars, obviously. Set the status based on the called operation.
            self.base.status(TransactionsStateMap[self.base.operation])

            self.base._set_status(keywords["package"], TransactionsInfoMap[self.base.operation])
            self.base.item_progress(self.base._pkg_to_id(keywords["package"]), TransactionsStateMap[self.base.operation], 0)
        if event == pisi.ui.removing:
            self.base._set_status(keywords["package"], TransactionsInfoMap[self.base.operation])
            self.base.item_progress(self.base._pkg_to_id(keywords["package"]), TransactionsStateMap[self.base.operation], 0)
        if event == pisi.ui.extracting:
            # Increase the step offset by 50% and account for usysconf offset
            # e.g. 5 pkgs to install, currentpackage == 3 so update percentage 50% within 60% to 80%.
            subpercentage = (100 / (self.packagestogo + (0.1 * self.packagestogo))) / 100 * 50 + self.get_percentage(self.currentpackage, self.packagestogo)
            self.base._set_percent(subpercentage)
            self.base.item_progress(self.base._pkg_to_id(keywords["package"]), TransactionsStateMap[self.base.operation], 50)
        if event == pisi.ui.installed:
            self.base.item_progress(self.base._pkg_to_id(keywords["package"]), STATUS_INSTALL, 100)
            self.update_percentage()
        if event == pisi.ui.removed:
            self.base.item_progress(self.base._pkg_to_id(keywords["package"]), STATUS_REMOVE, 100)
            self.update_percentage()
        if event == pisi.ui.upgraded:
            self.base.item_progress(self.base._pkg_to_id(keywords["package"]), STATUS_UPDATE, 100)
            self.update_percentage()
        if event == pisi.ui.systemconf:
            self.base._set_percent(90)


class PackageKitEopkgBackend(PackageKitBaseBackend, PackagekitPackage):

    SETTINGS_FILE = "/etc/PackageKit/eopkg.d/groups.list"

    def __init__(self, args):
        self.bug_regex = None
        self.bug_uri = None
        self._load_settings()
        self.operation = None
        PackageKitBaseBackend.__init__(self, args)

        self.get_db()

        # Do not ask any question to users
        self.options = pisi.config.Options()
        self.options.yes_all = True

        self.saved_ui = pisi.context.ui

    def get_db(self):
        self.componentdb = pisi.db.componentdb.ComponentDB()
        # self.filesdb = pisi.db.filesdb.FilesDB()
        self.installdb = pisi.db.installdb.InstallDB()
        self.packagedb = pisi.db.packagedb.PackageDB()
        self.historydb = pisi.db.historydb.HistoryDB()
        self.repodb = pisi.db.repodb.RepoDB()

    def _load_settings(self):
        """ Load the PK Group-> PiSi component mapping """
        if os.path.exists(self.SETTINGS_FILE):
            with open(self.SETTINGS_FILE, "r") as mapping:
                self.groups = {}
                for line in mapping.readlines():
                    line = line.replace("\r", "").replace("\n", "").strip()
                    if line.strip() == "" or "#" in line:
                        continue

                    splits = line.split("=")
                    key = splits[0].strip()
                    value = splits[1].strip()

                    # Check if this contains our bug keys
                    if key == "Bug-Regex":
                        self.bug_regex = re.compile(value)
                        continue
                    if key == "Bug-URI":
                        self.bug_uri = value
                        continue
                    self.groups[key] = value
        else:
            self.groups = {}

    def _set_percent(self, percent):
        if percent <= 100:
            self.percentage(percent)

    def privileged(func):
        """
        Decorator for synchronizing privileged functions
        """

        def wrapper(self, *__args, **__kw):
            ui = SimplePisiHandler(self)
            self.operation = func.__name__
            pisi.api.set_userinterface(ui)
            try:
                func(self, *__args, **__kw)
            except Exception as e:
                raise e
            self.percentage(100)
            self.finished()
            self.get_db()
            pisi.api.set_userinterface(self.saved_ui)
        return wrapper

    def _pkg_to_id(self, pkg):
        """ PiSi pkg to pk id """
        # FIXME: when getting repo, we can't use self.packagedb/installdb here
        # as they get invalidated by the pisi.api call.
        if pisi.db.packagedb.PackageDB().has_package(pkg.name):
            repo = pisi.db.packagedb.PackageDB().which_repo(pkg.name)
            #if pisi.db.installdb.InstallDB().has_package(pkg.name):
            #    repo = "installed:{}".format(repo)
        else:
            if pisi.db.installdb.InstallDB().has_package(pkg.name):
                repo = "installed"
            else:
                repo = "local"
        pkg_id = self.get_package_id(pkg.name, pkg.version, pkg.architecture, repo)
        return pkg_id

    def _set_status(self, pkg, status):
        package_id = self._pkg_to_id(pkg)
        self.package(package_id, status, pkg.summary)

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

        status = INFO_AVAILABLE
        data = "installed"
        pkg = ""

        installed = self.installdb.get_package(package) if self.installdb.has_package(package) else None
        available, repo = self.packagedb.get_package_repo(package) if self.packagedb.has_package(package) else (None, None)

        # Not found
        if installed is None and available is None:
            raise PkError(ERROR_PACKAGE_NOT_FOUND, "Package %s not found" % package)

        # Unholy matrimony of irish priests who got a deal with the catholic church
        fltred = None
        fltr_status = None
        fltr_data = None
        if filters is not None:
            if FILTER_NOT_INSTALLED in filters:
                fltred = available if available is not None else None
                if fltred is None:
                    return
                fltr_status = INFO_AVAILABLE if fltred is not None else None
                fltr_data = repo if fltred is not None else None
            if FILTER_INSTALLED in filters:
                fltred = installed if installed is not None else None
                if fltred is None:
                    return
                fltr_status = INFO_INSTALLED if fltred is not None else None
                fltr_data = "installed:{}".format(repo) if repo is not None else data
            # FIXME: Newest should be able to show the newest local version as well as remote version
            if FILTER_NEWEST in filters:
                fltred = available if available is not None else installed
                fltr_status = INFO_AVAILABLE if fltred is not None else None
                fltr_data = repo if fltred is not None else None
            if FILTER_NEWEST in filters and FILTER_INSTALLED in filters:
                fltred = installed if installed is not None else None
                fltr_status = INFO_INSTALLED if fltred is not None else None
                fltr_data = "installed:{}".format(repo) if repo is not None else data

        # Installed and has repo origin
        if available is not None and installed is not None:
            pkg = fltred if fltred is not None else installed
            status = fltr_status if fltr_status is not None else INFO_INSTALLED
            data = fltr_data if fltr_data is not None else "installed:{}".format(repo)

        # Available but not installed
        if available is not None and installed is None:
            pkg = fltred if fltred is not None else available
            status = fltr_status if fltr_status is not None else INFO_AVAILABLE
            data = fltr_data if fltr_data is not None else repo

        # Installed but has no repo origin
        if installed is not None and available is None:
            pkg = fltred if fltred is not None else installed
            status = fltr_status if fltr_status is not None else INFO_INSTALLED
            data = fltr_data if fltr_data is not None else "installed"

        if filters is not None:
            if FILTER_GUI in filters and "app:gui" not in pkg.isA:
                return
            if FILTER_NOT_GUI in filters and "app:gui" in pkg.isA:
                return
            # FIXME: To lower
            nonfree = ['EULA', 'Distributable']
            if FILTER_FREE in filters:
                if any(l in pkg.license for l in nonfree):
                    return
            if FILTER_NOT_FREE in filters:
                if not any(l in pkg.license for l in nonfree):
                    return
            if FILTER_DEVELOPMENT in filters and not "-devel" in pkg.name:
                return
            if FILTER_NOT_DEVELOPMENT in filters and "-devel" in pkg.name:
                return
            pkg_subtypes = ["-devel", "-dbginfo", "-32bit", "-docs"]
            if FILTER_BASENAME in filters:
                if any(suffix in pkg.name for suffix in pkg_subtypes):
                    return
            if FILTER_NOT_BASENAME in filters:
                if not any(suffix in pkg.name for suffix in pkg_subtypes):
                    return

        version = self.__get_package_version(pkg)
        id = self.get_package_id(pkg.name, version, pkg.architecture, data)
        return self.package(id, status, pkg.summary)

    def depends_on(self, filters, package_ids, recursive):
        """ Prints a list of depends for a given package """
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)

        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]

            # FIXME: PiSi API has really inconsistent for return types and arguments!
            if self.packagedb.has_package(package):
                for pkg in self.packagedb.get_package(package).runtimeDependencies():
                    self.__get_package(pkg.package)
            elif self.installdb.has_package(package):
                for pkg in self.installdb.get_package(package).runtimeDependencies():
                    self.__get_package(pkg.package)
            else:
                self.error(ERROR_PACKAGE_NOT_FOUND, "Package %s was not found" % package)

    def get_categories(self):
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        categories = self.componentdb.list_components()
        categories.sort()

        for p in categories:
            component = self.componentdb.get_component(p)
            cat_id = component.name
            self.category(component.group, cat_id, component.name, str(component.summary), "image-missing")

    def repair_system(self, transaction_flags):
        """ Deletes caches, rebuilds filesdb and reinits pisi caches """

        if TRANSACTION_FLAG_SIMULATE in transaction_flags:
            return

        self.status(STATUS_CLEANUP)
        pisi.api.delete_cache()
        self.status(STATUS_REFRESH_CACHE)
        pisi.api.rebuild_db(files=True)
        pisi.db.update_caches()

    def get_details(self, package_ids):
        """ Prints a detailed description for a given packages """
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)

        for package in package_ids:
            package = self.get_package_from_id(package)[0]

            pkg = ""
            size = 0
            dl_size = 0
            data = "installed"

            # FIXME: There is duplication here from __get_package
            if self.packagedb.has_package(package):
                pkg, repo = self.packagedb.get_package_repo(package, None)
                size = int(pkg.installedSize)
                dl_size = int(pkg.packageSize)
                if self.installdb.has_package(package):
                    data = "installed:{}".format(repo)
                else:
                    data = repo
            elif self.installdb.has_package(package):
                pkg = self.installdb.get_package(package)
                data = "local"
                size = int(pkg.installedSize)
                dl_size = int(pkg.packageSize)
            else:
                self.error(ERROR_PACKAGE_NOT_FOUND, "Package %s was not found" % package)

            pkg_id = self.get_package_id(pkg.name, self.__get_package_version(pkg),
                                         pkg.architecture, data)

            if pkg.partOf in self.groups:
                group = self.groups[pkg.partOf]
            else:
                group = GROUP_UNKNOWN
            homepage = pkg.source.homepage if pkg.source.homepage is not None else ''

            description = str(pkg.description).replace('\n', " ")

            self.details(pkg_id, pkg.summary, ",".join(pkg.license), group, description,
                            homepage, size)

    def get_details_local(self, files):

        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for f in files:
            if not f.endswith(".eopkg"):
                self.error(ERROR_PACKAGE_NOT_FOUND, "Eopkg %s was not found" % f)
            try:
                metadata, files = pisi.api.info_file(f)
            except PkError as e:
                if e.code == ERROR_PACKAGE_NOT_FOUND:
                    self.message('COULD_NOT_FIND_PACKAGE', e.details)
                    continue
                self.error(e.code, e.details, exit=True)
                return
            if metadata:
                pkg = metadata.package

            data = "local"

            pkg_id = self.get_package_id(pkg.name, self.__get_package_version(pkg),
                                         pkg.architecture, data)

            if pkg.partOf in self.groups:
                group = self.groups[pkg.partOf]
            else:
                group = GROUP_UNKNOWN
            homepage = pkg.source.homepage if pkg.source.homepage is not None\
                else ''

            size = pkg.installedSize

            self.details(pkg_id, pkg.summary, ",".join(pkg.license), group,
                         pkg.description, homepage, size)

    def get_files(self, package_ids):
        """ Prints a file list for a given packages """
        self.allow_cancel(True)
        self.percentage(None)

        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]

            if self.installdb.has_package(package):
                pkg = self.packagedb.get_package(package)
                repo = self.packagedb.get_package_repo(pkg.name, None)
                pkg_id = self.get_package_id(pkg.name,
                                             self.__get_package_version(pkg),
                                             pkg.architecture, repo[1])

                pkg = self.installdb.get_files(package)

                files = ["/%s" % y.path for y in pkg.list]

                file_list = ";".join(files)
                self.files(pkg_id, file_list)
            else:
                self.error(ERROR_PACKAGE_NOT_FOUND,
                           "Package %s must be installed to get file list" % package_id.split(";"))

    def get_packages(self, filters):
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)

        packages = list()
        all_pkgs = False
        installed = self.installdb.list_installed()
        available = self.packagedb.list_packages(None)

        if FILTER_INSTALLED in filters:
            packages = installed
        elif FILTER_NOT_INSTALLED in filters:
            cntInstalled = Counter(installed)
            cntAvailable = Counter(available)

            diff = cntAvailable - cntInstalled
            packages = diff.elements()
        else:
            packages = available

        for package in packages:
            self.__get_package(package)

    def get_repo_list(self, filters):
        """ Prints available repositories """
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for repo in self.repodb.list_repos(only_active=False):
            uri = self.repodb.get_repo_url(repo)
            enabled = False
            if self.repodb.repo_active(repo):
                enabled = True
            self.repo_detail(repo, uri, enabled)

    def required_by(self, filters, package_ids, recursive):
        """ Prints a list of requires for a given package """
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)

        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]

            if self.packagedb.has_package(package):
                for pkg in self.packagedb.get_rev_deps(package):
                    self.__get_package(pkg[0])
            elif self.installdb.has_package(package):
                for pkg in self.installdb.get_rev_deps(package):
                    self.__get_package(pkg[0])
            else:
                self.error(ERROR_PACKAGE_NOT_FOUND, "Package %s was not found" % package.name)

    def get_updates(self, filters):
        """ Prints available updates and types """
        self.allow_cancel(True)
        self.percentage(None)

        for package in pisi.api.list_upgradable():
            # FIXME: we need to handle replaces here more effectively
            if self.packagedb.has_package(package):
                pkg, repo = self.packagedb.get_package_repo(package, None)

            version = self.__get_package_version(pkg)
            id = self.get_package_id(pkg.name, version, pkg.architecture, repo)
            installed_package = self.installdb.get_package(package)

            oldRelease = int(installed_package.release)
            histories = self._get_history_between(oldRelease, pkg)

            securities = [x for x in histories if x.type == "security"]
            # FIXME: INFO_BUGFIX Support? We would have to match against #123 Github issues
            if len(securities) > 0:
                self.package(id, INFO_SECURITY, pkg.summary)
            else:
                self.package(id, INFO_NORMAL, pkg.summary)

    def _get_history_between(self, old_release, new):
        """ Get the history items between the old release and new pkg """
        ret = list()

        for i in new.history:
            if int(i.release) <= int(old_release):
                continue
            ret.append(i)
        return sorted(ret, key=attrgetter('release'), reverse=True)

    def get_update_detail(self, package_ids):
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]
            pkg, repo = self.packagedb.get_package_repo(package, None)
            version = self.__get_package_version(pkg)
            id = self.get_package_id(pkg.name, version, pkg.architecture, repo)

            updates = [package_id]
            obsoletes = ""

            package_url = pkg.source.homepage
            vendor_url = package_url if package_url is not None else ""

            update_message = pkg.history[0].comment
            update_message = update_message.replace("\n", ";")

            updated_date = pkg.history[0].date

            bugURI = ""

            changelog = ""
            # FIXME: Works but output is fugly
            #for i in pkg.history:
            #    comment = i.comment
            #    comment = comment.replace("\n", ";")
            #    changelog.append(comment)

            cves = re.findall(r" (CVE\-[0-9]+\-[0-9]+)", str(update_message))
            cve_url = ""
            if cves is not None:
                #cve_url = "https://cve.mitre.org/cgi-bin/cvename.cgi?name={}".format(cves[0])
                cve_url = cves

            # TODO: If repo is unstable and package.release not in shannon then UNSTABLE
            state = UPDATE_STATE_STABLE
            reboot = "none"

            # TODO: Eopkg doesn't provide any time
            split_date = updated_date.split("-")
            updated = "{}-{}-{}T00:00:00".format(split_date[0], split_date[1], split_date[2])
            # TODO: The index only stores the last 10 history entries.
            #       What is the difference between issued and updated?
            issued = ""

            self.update_detail(package_id, updates, obsoletes, vendor_url,
                               bugURI, cve_url, reboot, update_message,
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


def main():
    backend = PackageKitEopkgBackend('')
    backend.dispatcher(sys.argv[1:])

if __name__ == "__main__":
    main()
