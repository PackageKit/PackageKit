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

import os.path
import re
from collections import Counter
from operator import attrgetter

import pisi
import pisi.api
import pisi.config
import pisi.context
import pisi.db
import pisi.fetcher
import pisi.ui
import pisi.util
from packagekit.backend import *
from packagekit.enums import *
from packagekit.package import PackagekitPackage
from packagekit.progress import *


def _format_str(text):
    """
    Convert a multi line string to a list separated by ';'
    """
    if text:
        lines = text.split("\n")
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
        self.cur_pkg = None
        self.cur_status = None
        self.is_downloading = False

    def _update_global_progress(self, item_percent):
        if self.packagestogo <= 0:
            self.base._set_percent(item_percent)
            return

        # Weight global progress: 90% for packages (allowing for usysconf), 100% if just downloading
        weight = 100.0 if self.is_downloading else 90.0
        slice_size = weight / self.packagestogo

        # currentpackage is 1-indexed (1..N)
        base_percent = (self.currentpackage - 1) * slice_size
        progress = base_percent + (item_percent / 100.0) * slice_size
        self.base._set_percent(progress)

    def display_progress(self, **kw):
        operation = kw.get("operation")
        percent = int(kw.get("percent", 0))

        if operation == "fetching":
            filename = kw.get("filename")
            if filename and not filename.startswith("eopkg-index.xml"):
                pkg_name = pisi.util.parse_package_name(filename)[0]
                if pisi.db.packagedb.PackageDB().has_package(pkg_name):
                    pkg = pisi.db.packagedb.PackageDB().get_package(pkg_name)
                    self.base.item_progress(
                        self.base._pkg_to_id(pkg), STATUS_DOWNLOAD, percent
                    )
            self._update_global_progress(percent)

        elif operation == "extracting":
            if self.cur_pkg and self.cur_status:
                self.base.item_progress(
                    self.base._pkg_to_id(self.cur_pkg), self.cur_status, percent
                )
            self._update_global_progress(percent)

        elif operation == "indexing":
            self.base._set_percent(percent)

        else:
            # Fallback for other operations
            if self.packagestogo > 0:
                self._update_global_progress(percent)
            else:
                self.base._set_percent(percent)

    def notify(self, event, **keywords):
        if event == pisi.ui.packagestogo:
            self.packagestogo = len(keywords["order"])
            self.currentpackage = 0

        elif event == pisi.ui.downloading:
            if not self.is_downloading:
                self.is_downloading = True
                self.currentpackage = 0
            self.currentpackage += 1
            self.cur_pkg = keywords["package"]
            self.base.item_progress(
                self.base._pkg_to_id(self.cur_pkg), STATUS_DOWNLOAD, 0
            )

        elif event in (pisi.ui.installing, pisi.ui.upgrading):
            if self.is_downloading:
                self.is_downloading = False
                self.currentpackage = 0

            self.currentpackage += 1
            self.cur_pkg = keywords["package"]
            self.cur_status = (
                STATUS_INSTALL if event == pisi.ui.installing else STATUS_UPDATE
            )
            info = INFO_INSTALLING if event == pisi.ui.installing else INFO_UPDATING

            self.base.status(self.cur_status)
            self.base._set_status(self.cur_pkg, info)
            self.base.item_progress(
                self.base._pkg_to_id(self.cur_pkg), self.cur_status, 0
            )

        elif event == pisi.ui.removing:
            self.is_downloading = False
            if self.currentpackage >= self.packagestogo:
                self.currentpackage = 0
            self.currentpackage += 1
            self.cur_pkg = keywords["package"]
            self.cur_status = STATUS_REMOVE
            self.base.status(self.cur_status)
            self.base._set_status(self.cur_pkg, INFO_REMOVING)
            self.base.item_progress(
                self.base._pkg_to_id(self.cur_pkg), self.cur_status, 0
            )

        elif event == pisi.ui.extracting:
            self.cur_pkg = keywords.get("package", self.cur_pkg)
            # Item progress and global percentage are now handled live in display_progress

        elif event in (pisi.ui.installed, pisi.ui.upgraded, pisi.ui.removed):
            status = STATUS_INSTALL
            if event == pisi.ui.upgraded:
                status = STATUS_UPDATE
            if event == pisi.ui.removed:
                status = STATUS_REMOVE

            if self.cur_pkg:
                self.base.item_progress(self.base._pkg_to_id(self.cur_pkg), status, 100)
            self._update_global_progress(100)

        elif event == pisi.ui.systemconf:
            self.base._set_percent(90)


class PackageKitEopkgBackend(PackageKitBaseBackend, PackagekitPackage):
    SETTINGS_FILE = "/etc/PackageKit/eopkg.d/groups.list"

    def __init__(self, args):
        self.bug_regex = None
        self.bug_uri = None
        self._load_settings()
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
        """Load the PK Group-> PiSi component mapping"""
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
        """PiSi pkg to pk id"""
        # FIXME: when getting repo, we can't use self.packagedb/installdb here
        # as they get invalidated by the pisi.api call.
        if pisi.db.packagedb.PackageDB().has_package(pkg.name):
            repo = pisi.db.packagedb.PackageDB().which_repo(pkg.name)
            # if pisi.db.installdb.InstallDB().has_package(pkg.name):
            #    repo = "installed:{}".format(repo)
        else:
            if pisi.db.installdb.InstallDB().has_package(pkg.name):
                repo = "installed"
            else:
                repo = "local"
        pkg_id = self.get_package_id(pkg.name, pkg.version, pkg.architecture, repo)
        return pkg_id

    def _get_package_obj_from_id(self, package_id):
        """Centralized helper to get package object and data from id"""
        name, version, arch, data = self.get_package_from_id(package_id)
        pkg = None

        if data == "local" or data.startswith("installed"):
            if self.installdb.has_package(name):
                pkg = self.installdb.get_package(name)

        if pkg is None and self.packagedb.has_package(name):
            pkg, repo = self.packagedb.get_package_repo(name, None)
            data = repo

        if pkg is None and self.installdb.has_package(name):
            pkg = self.installdb.get_package(name)
            data = "local"

        if pkg is None:
            raise PkError(ERROR_PACKAGE_NOT_FOUND, "Package %s was not found" % name)

        return pkg, data

    def _set_status(self, pkg, status):
        package_id = self._pkg_to_id(pkg)
        self.package(package_id, status, pkg.summary)

    def __get_package_version(self, package):
        """Returns version string of given package"""
        # Internal FIXME: PiSi may provide this
        if package.build is not None:
            version = "%s-%s-%s" % (package.version, package.release, package.build)
        else:
            version = "%s-%s" % (package.version, package.release)
        return version

    def __get_package(self, package, filters=None):
        """Returns package object suitable for other methods"""

        installed = (
            self.installdb.get_package(package)
            if self.installdb.has_package(package)
            else None
        )
        available, repo = (
            self.packagedb.get_package_repo(package)
            if self.packagedb.has_package(package)
            else (None, None)
        )

        # Not found
        if installed is None and available is None:
            raise PkError(ERROR_PACKAGE_NOT_FOUND, "Package %s not found" % package)

        pkg = None
        status = None
        data = None

        if filters is not None:
            if FILTER_INSTALLED in filters:
                if installed:
                    pkg = installed
                    status = INFO_INSTALLED
                    data = "installed:{}".format(repo) if repo else "installed"
                else:
                    return
            elif FILTER_NOT_INSTALLED in filters:
                if available:
                    pkg = available
                    status = INFO_AVAILABLE
                    data = repo
                else:
                    return
            elif FILTER_NEWEST in filters:
                if available:
                    pkg = available
                    status = INFO_AVAILABLE
                    data = repo
                elif installed:
                    pkg = installed
                    status = INFO_INSTALLED
                    data = "installed"
                else:
                    return

        # Fallback if no filter matched or no filters provided
        if pkg is None:
            if installed:
                pkg = installed
                status = INFO_INSTALLED
                data = "installed:{}".format(repo) if repo else "installed"
            else:
                pkg = available
                status = INFO_AVAILABLE
                data = repo

        if filters is not None:
            if FILTER_GUI in filters and "app:gui" not in pkg.isA:
                return
            if FILTER_NOT_GUI in filters and "app:gui" in pkg.isA:
                return
            # FIXME: To lower
            nonfree = ["EULA", "Distributable"]
            if FILTER_FREE in filters:
                if any(l in pkg.license for l in nonfree):
                    return
            if FILTER_NOT_FREE in filters:
                if not any(l in pkg.license for l in nonfree):
                    return
            if FILTER_DEVELOPMENT in filters and "-devel" not in pkg.name:
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
        """Prints a list of depends for a given package"""
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)

        for package_id in package_ids:
            try:
                pkg, data = self._get_package_obj_from_id(package_id)
            except PkError as e:
                self.error(e.code, e.details)
                continue

            for dep in pkg.runtimeDependencies():
                self.__get_package(dep.package)

    def get_categories(self):
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        categories = self.componentdb.list_components()
        categories.sort()

        for p in categories:
            component = self.componentdb.get_component(p)
            cat_id = component.name
            self.category(
                component.group,
                cat_id,
                component.name,
                str(component.summary),
                "image-missing",
            )

    def repair_system(self, transaction_flags):
        """Deletes caches, rebuilds filesdb and reinits pisi caches"""

        if TRANSACTION_FLAG_SIMULATE in transaction_flags:
            return

        self.status(STATUS_CLEANUP)
        pisi.api.delete_cache()
        self.status(STATUS_REFRESH_CACHE)
        pisi.api.rebuild_db(files=True)
        pisi.db.update_caches()

    def get_details(self, package_ids):
        """Prints a detailed description for a given packages"""
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)

        for package_id in package_ids:
            try:
                pkg, data = self._get_package_obj_from_id(package_id)
            except PkError as e:
                self.error(e.code, e.details)
                continue

            size = pkg.installedSize
            dl_size = pkg.packageSize

            pkg_id = self.get_package_id(
                pkg.name, self.__get_package_version(pkg), pkg.architecture, data
            )

            if pkg.partOf in self.groups:
                group = self.groups[pkg.partOf]
            else:
                group = GROUP_UNKNOWN
            homepage = pkg.source.homepage if pkg.source.homepage is not None else ""

            description = str(pkg.description).replace("\n", " ")

            self.details(
                pkg_id,
                pkg.summary,
                ",".join(pkg.license),
                group,
                description,
                homepage,
                size,
                dl_size,
            )

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
                    self.message("COULD_NOT_FIND_PACKAGE", e.details)
                    continue
                self.error(e.code, e.details, exit=True)
                return
            if metadata:
                pkg = metadata.package

            data = "local"

            pkg_id = self.get_package_id(
                pkg.name, self.__get_package_version(pkg), pkg.architecture, data
            )

            if pkg.partOf in self.groups:
                group = self.groups[pkg.partOf]
            else:
                group = GROUP_UNKNOWN
            homepage = pkg.source.homepage if pkg.source.homepage is not None else ""

            size = pkg.installedSize
            dl_size = os.path.getsize(f)

            self.details(
                pkg_id,
                pkg.summary,
                ",".join(pkg.license),
                group,
                pkg.description,
                homepage,
                size,
                dl_size,
            )

    def get_files(self, package_ids):
        """Prints a file list for a given packages"""
        self.allow_cancel(True)
        self.percentage(None)

        for package_id in package_ids:
            try:
                pkg, data = self._get_package_obj_from_id(package_id)
            except PkError as e:
                self.error(e.code, e.details)
                continue

            if self.installdb.has_package(pkg.name):
                pkg_id = self.get_package_id(
                    pkg.name, self.__get_package_version(pkg), pkg.architecture, data
                )

                pkg_files = self.installdb.get_files(pkg.name)

                files = ["/%s" % y.path for y in pkg_files.list]

                file_list = ";".join(files)
                self.files(pkg_id, file_list)
            else:
                self.error(
                    ERROR_PACKAGE_NOT_FOUND,
                    "Package %s must be installed to get file list" % pkg.name,
                )

    def get_packages(self, filters):
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)

        packages = list()
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
        """Prints available repositories"""
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
        """Prints a list of requires for a given package"""
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)

        for package_id in package_ids:
            try:
                pkg, data = self._get_package_obj_from_id(package_id)
            except PkError as e:
                self.error(e.code, e.details)
                continue

            for dep in self.packagedb.get_rev_deps(pkg.name):
                self.__get_package(dep[0])

    def get_updates(self, filters):
        """Prints available updates and types"""
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
        """Get the history items between the old release and new pkg"""
        ret = list()

        for i in new.history:
            if int(i.release) <= int(old_release):
                continue
            ret.append(i)
        return sorted(ret, key=attrgetter("release"), reverse=True)

    def get_update_detail(self, package_ids):
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        upgradables = pisi.api.list_upgradable()

        for package_id in package_ids:
            try:
                pkg, data = self._get_package_obj_from_id(package_id)
            except PkError:
                continue

            if pkg.name not in upgradables:
                continue

            version = self.__get_package_version(pkg)
            id = self.get_package_id(pkg.name, version, pkg.architecture, data)

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
            # for i in pkg.history:
            #    comment = i.comment
            #    comment = comment.replace("\n", ";")
            #    changelog.append(comment)

            cves = re.findall(r" (CVE\-[0-9]+\-[0-9]+)", str(update_message))
            cve_url = ""
            if cves is not None:
                # cve_url = "https://cve.mitre.org/cgi-bin/cvename.cgi?name={}".format(cves[0])
                cve_url = cves

            # TODO: If repo is unstable and package.release not in shannon then UNSTABLE
            state = UPDATE_STATE_STABLE
            reboot = "none"

            # TODO: Eopkg doesn't provide any time
            split_date = updated_date.split("-")
            updated = "{}-{}-{}T00:00:00Z".format(
                split_date[0], split_date[1], split_date[2]
            )
            # TODO: The index only stores the last 10 history entries.
            #       What is the difference between issued and updated?
            issued = ""

            self.update_detail(
                package_id,
                updates,
                obsoletes,
                vendor_url,
                bugURI,
                cve_url,
                reboot,
                update_message,
                changelog,
                state,
                issued,
                updated,
            )

    @privileged
    def download_packages(self, directory, package_ids):
        """Download the given packages to a directory"""
        self.allow_cancel(True)
        self.percentage(0)
        self.status(STATUS_DOWNLOAD)

        packages = list()

        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]
            packages.append(package)
            try:
                pkg = self.packagedb.get_package(package)
            except:
                self.error(ERROR_PACKAGE_NOT_FOUND, "Package was not found")
        try:
            if directory is None:
                directory = os.path.curdir
            pisi.api.fetch(packages, directory)
            # Scan for package
            for package in packages:
                package_obj = self.packagedb.get_package(package)
                uri = package_obj.packageURI.split("/")[-1]
                location = os.path.join(directory, uri)
                self.files(package_id, location)
        except pisi.fetcher.FetchError as e:
            self.error(
                ERROR_PACKAGE_DOWNLOAD_FAILED,
                "Could not download package: %s" % e,
                exit=False,
            )
        except IOError as e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % e)
        except pisi.Error as e:
            self.error(
                ERROR_PACKAGE_DOWNLOAD_FAILED,
                "Could not download package: %s" % e,
                exit=False,
            )
        except Exception:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

    @privileged
    def install_files(self, transaction_flags, inst_files):
        """Installs given package into system"""

        # FIXME: use only_trusted

        if TRANSACTION_FLAG_SIMULATE in transaction_flags:
            for f in inst_files:
                metadata, _ = pisi.api.info_file(f)
                pkg_id = self.get_package_id(
                    metadata.package.name,
                    metadata.package.version,
                    metadata.package.architecture,
                    "local",
                )
                self.package(pkg_id, INFO_INSTALL, metadata.package.summary)
                for dep in metadata.package.runtimeDependencies():
                    if not dep.satisfied_by_installed():
                        if not self.packagedb.has_package(dep.package):
                            self.error(
                                ERROR_DEP_RESOLUTION_FAILED,
                                "Cannot install: %s. Can't resolve dependency %s"
                                % (f, dep.package),
                            )
                        dep_pkg = self.packagedb.get_package(dep.package)
                        repo = self.packagedb.get_package_repo(dep_pkg.name, None)
                        dep_id = self.get_package_id(
                            dep_pkg.name, dep_pkg.version, dep_pkg.architecture, repo
                        )
                        self.package(dep_id, INFO_INSTALL, dep_pkg.summary)
            return

        self.allow_cancel(False)
        try:
            # Actually install
            pisi.api.install(inst_files)
        except pisi.fetcher.FetchError as e:
            self.error(
                ERROR_PACKAGE_DOWNLOAD_FAILED,
                "Could not download package: %s" % e,
                exit=False,
            )
        except IOError as e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % e)
        except pisi.Error as e:
            self.error(
                ERROR_LOCAL_INSTALL_FAILED, "Could not install: %s" % e, exit=False
            )
        except Exception:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

    @privileged
    def install_packages(self, transaction_flags, package_ids):
        """Installs given package into system"""
        self.allow_cancel(False)
        self.percentage(0)
        packages = list()

        # FIXME: use only_trusted
        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]
            if self.installdb.has_package(package):
                self.error(ERROR_PACKAGE_NOT_INSTALLED, "Package is already installed")
            packages.append(package)

        if TRANSACTION_FLAG_SIMULATE in transaction_flags:
            pkgSet = set(packages)
            order = pisi.api.get_install_order(pkgSet)
            # Merge any forced system.base upgrades to the order as well
            base_order = pisi.api.get_base_upgrade_order(pkgSet)
            order = base_order + order
            for dep in order:
                dep_pkg = self.packagedb.get_package(dep)
                repo = self.packagedb.get_package_repo(dep_pkg.name, None)
                version = self.__get_package_version(dep_pkg)
                pkg_id = self.get_package_id(
                    dep_pkg.name, version, dep_pkg.architecture, repo[1]
                )
                self.package(pkg_id, INFO_INSTALL, dep_pkg.summary)
            return

        if TRANSACTION_FLAG_ONLY_DOWNLOAD in transaction_flags:
            pisi.context.set_option("fetch_only", True)

        try:
            pisi.api.install(packages)
        except pisi.fetcher.FetchError as e:
            self.error(
                ERROR_PACKAGE_DOWNLOAD_FAILED,
                "Could not download package: %s" % e,
                exit=False,
            )
        except IOError as e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % e)
        except pisi.Error as e:
            self.error(
                ERROR_PACKAGE_FAILED_TO_INSTALL, "Could not install: %s" % e, exit=False
            )
        except Exception:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

    @privileged
    def refresh_cache(self, force):
        """Updates repository indexes"""
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

    @privileged
    def remove_packages(self, transaction_flags, package_ids, allowdep, autoremove):
        """Removes given package from system"""
        self.allow_cancel(False)
        self.percentage(0)
        packages = list()

        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]
            if not self.installdb.has_package(package):
                self.error(ERROR_PACKAGE_NOT_INSTALLED, "Package is not installed")
            packages.append(package)

        if TRANSACTION_FLAG_SIMULATE in transaction_flags:
            pkgSet = set(packages)
            order = pisi.api.get_remove_order(pkgSet, autoremove)
            for dep in order:
                dep_pkg = self.packagedb.get_package(dep)
                if dep_pkg.partOf == "system.base":
                    self.error(
                        ERROR_CANNOT_REMOVE_SYSTEM_PACKAGE,
                        "Cannot remove system.base package: %s" % dep_pkg.name,
                    )
                repo = self.packagedb.get_package_repo(dep_pkg.name, None)
                version = self.__get_package_version(dep_pkg)
                pkg_id = self.get_package_id(
                    dep_pkg.name, version, dep_pkg.architecture, repo[1]
                )
                self.package(pkg_id, INFO_REMOVE, dep_pkg.summary)
            return

        try:
            if autoremove:
                pisi.api.autoremove(packages)
            else:
                pisi.api.remove(packages)
        except pisi.fetcher.FetchError as e:
            self.error(
                ERROR_PACKAGE_DOWNLOAD_FAILED,
                "Could not download package: %s" % e,
                exit=False,
            )
        except IOError as e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % e)
        except pisi.Error as e:
            self.error(
                ERROR_PACKAGE_FAILED_TO_REMOVE, "Could not remove: %s" % e, exit=False
            )
        except Exception:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

    @privileged
    def repo_enable(self, repoid, enable):
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)
        if self.repodb.has_repo(repoid):
            pisi.api.set_repo_activity(repoid, enable)
            return
        else:
            self.error(ERROR_REPO_NOT_FOUND, "Repository %s was not found" % repoid)

    @privileged
    def repo_set_data(self, repoid, parameter, value):
        """Sets a parameter for the repository specified"""
        self.allow_cancel(False)
        self.percentage(None)

        if parameter == "add-repo":
            try:
                pisi.api.add_repo(repoid, value)
            except pisi.Error as e:
                self.error(ERROR_UNKNOWN, e)

            try:
                pisi.api.update_repo(repoid)
            except pisi.fetcher.FetchError:
                pisi.api.remove_repo(repoid)
                err = "Could not reach the repository, removing from system"
                self.error(ERROR_REPO_NOT_FOUND, err)
        elif parameter == "remove-repo":
            try:
                pisi.api.remove_repo(repoid)
            except pisi.Error:
                self.error(ERROR_REPO_NOT_FOUND, "Repository does not exist")
        else:
            self.error(
                ERROR_NOT_SUPPORTED, "Valid parameters are add-repo and remove-repo"
            )

    def resolve(self, filters, values):
        """Turns a single package name into a package_id
        suitable for the other methods"""
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_QUERY)

        for package in values:
            name = self.get_package_from_id(package)[0]
            try:
                if filters is not None:
                    self.__get_package(name, filters)
                else:
                    # If no filters, show both installed and available if they exist
                    # we should really fix __get_package to allow emitting multiple results
                    self.__get_package(name, [FILTER_INSTALLED])
                    self.__get_package(name, [FILTER_NOT_INSTALLED])
            except PkError as e:
                if e.code == ERROR_PACKAGE_NOT_FOUND:
                    continue
                self.error(e.code, e.details, exit=True)
                return

    def search_details(self, filters, values):
        """Prints a detailed list of packages contains search term"""
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        # Internal FIXME: Use search_details instead of _package when API
        # gains that ability :)
        for pkg in pisi.api.search_package(values):
            self.__get_package(pkg, filters)

    def search_file(self, filters, values):
        """Prints the installed package which contains the specified file"""
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for value in values:
            # Internal FIXME: Why it is needed?
            value = value.lstrip("/")

            for pkg, files in pisi.api.search_file(value):
                self.__get_package(pkg)

    def search_group(self, filters, values):
        """Prints a list of packages belongs to searched group"""
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
                        self.error(
                            ERROR_GROUP_NOT_FOUND, "Component %s was not found" % value
                        )
            for pkg in packages:
                self.__get_package(pkg, filters)

    def search_name(self, filters, values):
        """Prints a list of packages contains search term in its name"""
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_INFO)

        for value in values:
            for pkg in pisi.api.search_package([value]):
                self.__get_package(pkg, filters)

    @privileged
    def update_packages(self, transaction_flags, package_ids):
        """Updates given package to its latest version"""

        # FIXME: use only_trusted
        # FIXME: install progress
        self.allow_cancel(True)
        self.percentage(None)
        self.status(STATUS_RUNNING)

        packages = list()
        for package_id in package_ids:
            package = self.get_package_from_id(package_id)[0]
            if not self.installdb.has_package(package):
                self.error(
                    ERROR_PACKAGE_NOT_INSTALLED,
                    "Cannot update a package that is not installed",
                )
            packages.append(package)

        if TRANSACTION_FLAG_SIMULATE in transaction_flags:
            pkgSet = set(packages)
            order = pisi.api.get_upgrade_order(pkgSet)
            # Merge any forced system.base upgrades to the order as well
            base_order = pisi.api.get_base_upgrade_order(pkgSet)
            order = base_order + order
            for dep in order:
                dep_pkg = self.packagedb.get_package(dep)
                repo = self.packagedb.get_package_repo(dep_pkg.name, None)
                version = self.__get_package_version(dep_pkg)
                pkg_id = self.get_package_id(
                    dep_pkg.name, version, dep_pkg.architecture, repo[1]
                )
                self.package(pkg_id, INFO_INSTALL, dep_pkg.summary)
            return

        if TRANSACTION_FLAG_ONLY_DOWNLOAD in transaction_flags:
            pisi.context.set_option("fetch_only", True)

        try:
            # Actually upgrade
            pisi.api.upgrade(packages)
        except pisi.fetcher.FetchError as e:
            self.error(
                ERROR_PACKAGE_DOWNLOAD_FAILED,
                "Could not download package: %s" % e,
                exit=False,
            )
        except IOError as e:
            self.error(ERROR_NO_SPACE_ON_DEVICE, "Disk error: %s" % e)
        except pisi.Error as e:
            self.error(
                ERROR_PACKAGE_FAILED_TO_INSTALL, "Could not update: %s" % e, exit=False
            )
        except Exception:
            self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))


def main():
    backend = PackageKitEopkgBackend("")
    backend.dispatcher(sys.argv[1:])


if __name__ == "__main__":
    main()
