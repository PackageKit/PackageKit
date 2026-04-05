#!/usr/bin/python3
# -*- coding: utf-8 -*-
# vim:set shiftwidth=4 tabstop=4 expandtab:
#
# Copyright (C) 2009 Mounir Lamouri (volkmar) <mounir.lamouri@gmail.com>
# Copyright (C) 2010-2013 Fabio Erculiani (lxnay) <lxnay@gentoo.org>
# Copyright (C) 2025-2026 Mihai Morovan <hithack9@gmail.com>
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

import os
import re
import signal
import sys
import traceback
from collections import defaultdict

from itertools import zip_longest

import subprocess
# packagekit imports
from packagekit.backend import (
    PackageKitBaseBackend,
    get_package_id,
    split_package_id,
)
from packagekit.enums import *
from packagekit.progress import PackagekitProgress
# portage imports
import _emerge.AtomArg

from _emerge.actions import load_emerge_config
from _emerge.actions import action_sync
from _emerge.create_depgraph_params import create_depgraph_params
from _emerge.depgraph import depgraph
from _emerge.Scheduler import Scheduler
from _emerge.stdout_spinner import stdout_spinner
from _emerge.unmerge import unmerge

import portage
import portage.dep
import portage.versions
from portage._sets.base import InternalPackageSet
from portage.exception import InvalidAtom

from _emerge.main import parse_opts
import traceback
import shlex
import portage.elog

# NOTES:
#
# Package IDs description:
# CAT/PN;PV;KEYWORD;[REPOSITORY|installed]
# Last field must be "installed" if installed. Otherwise it's the repo name
#
# Naming convention:
# cpv: category package version, the standard representation of what packagekit
#   names a package (an ebuild for portage)

# TODO:
# remove percentage(None) if percentage is used
# protection against signal when installing/removing

# Map Gentoo categories to the PackageKit group name space


def compute_equal_steps(iterable):
    return [idx * (100.0 / len(iterable))
            for idx, _ in enumerate(iterable, start=1)]


class PortagePackageGroups(dict):

    """
    Portage Package categories group representation
    """

    def __init__(self):
        dict.__init__(self)

        data = {
            'accessibility': {
                'name': "Accessibility",
                'description': "Accessibility applications",
                'categories': ['app-accessibility'],
            },
            'office': {
                'name': "Office",
                'description': "Applications used in office environments",
                'categories': ['app-office', 'app-pda', 'app-mobilephone',
                               'app-cdr', 'app-antivirus', 'app-laptop',
                               'mail-'],
            },
            'development': {
                'name': "Development",
                'description': "Applications or system libraries",
                'categories': ['dev-', 'sys-devel'],
            },
            'system': {
                'name': "System",
                'description': "System applications or libraries",
                'categories': ['sys-'],
            },
            'games': {
                'name': "Games",
                'description': "Games, enjoy your spare time",
                'categories': ['games-'],
            },
            'gnome': {
                'name': "GNOME Desktop",
                'description': "Applications and libraries for the GNOME Desktop",
                'categories': ['gnome-'],
            },
            'kde': {
                'name': "KDE Desktop",
                'description': "Applications and libraries for the KDE Desktop",
                'categories': ['kde-'],
            },
            'xfce': {
                'name': "XFCE Desktop",
                'description': "Applications and libraries for the XFCE Desktop",
                'categories': ['xfce-'],
            },
            'lxde': {
                'name': "LXDE Desktop",
                'description': "Applications and libraries for the LXDE Desktop",
                'categories': ['lxde-'],
            },
            'multimedia': {
                'name': "Multimedia",
                'description': "Applications and libraries for Multimedia",
                'categories': ['media-'],
            },
            'networking': {
                'name': "Networking",
                'description': "Applications and libraries for Networking",
                'categories': ['net-', 'www-'],
            },
            'science': {
                'name': "Science",
                'description': "Scientific applications and libraries",
                'categories': ['sci-'],
            },
            'security': {
                'name': "Security",
                'description': "Security orientend applications",
                'categories': ['app-antivirus', 'net-analyzer', 'net-firewall'],
            },
            'x11': {
                'name': "X11",
                'description': "Applications and libraries for X11",
                'categories': ['x11-'],
            },
        }

        self.update(data)


class PortageBridge():

    '''
    Bridge to portage/emerge settings and variabales to help using them
    and be sure they are always up-to-date.
    '''

    def __init__(self):
        self.settings = None
        self.trees = None
        self.mtimedb = None
        self.vardb = None
        self.portdb = None
        self.bindb = None
        self.root_config = None
        self.myopts = None
        self._allow_binpkgs = False

        self.update()
        os.environ.setdefault("PATH", "/usr/local/sbin:/usr/local/bin:/usr/bin:/opt/bin")
        os.environ.setdefault("HOME", "/var/lib/portage")
        os.environ.setdefault("USER", "portage")
        os.environ.setdefault("LOGNAME", "portage")

    def handle_binpkg(self):
        # TODO: return whole metadata correctly for remote binpkgs 
        try:
            emerge_config = load_emerge_config()
            tmpcmdline = []
            tmpcmdline.extend(
                shlex.split(
                    emerge_config.target_config.settings.get("EMERGE_DEFAULT_OPTS", "")
                )
            )
            emerge_config.action, emerge_config.opts, emerge_config.args = parse_opts(tmpcmdline)
            self.settings = emerge_config.target_config.settings
            self.myopts = emerge_config.opts
        except BaseException as e:
            self.error(ERROR_PACKAGE_FAILED_TO_INSTALL, f"parse_opts exploded: {type(e).__name__}: {e}")

        getbin = bool(self.myopts.get("--getbinpkg"))
        getbinonly = bool(self.myopts.get("--getbinpkgonly"))

        self._allow_binpkgs = getbin or getbinonly

        if getbin and getbinonly:
            self.error(ERROR_DEP_RESOLUTION_FAILED, 
                "Conflicting binary package options: both '--getbinpkg' and '--getbinpkgonly' are enabled.\n"
                "The system is configured to both prefer binary packages and require them exclusively.\n"
                "Please disable one of these options in EMERGE_DEFAULT_OPTS")
            return

        root = self.settings['ROOT']
        bintree = self.trees[root].get('bintree')

        if getbin:
            self.myopts["--usepkg"] = True
            self.myopts.pop("--getbinpkgonly", None)
            if bintree:
                try:
                    bintree.populate(getbinpkgs=True)
                except Exception as e:
                    self.message(MESSAGE_INFO, f"bintree.populate(getbinpkg) failed: {e}")

        elif getbinonly:
            self.myopts["--usepkgonly"] = True
            self.myopts.pop("--getbinpkg", None)
            if bintree:
                try:
                    bintree.populate(getbinpkgs=True)
                except Exception as e:
                    self.error(ERROR_PACKAGE_FAILED_TO_INSTALL, f"No binary packages available: {e}")
                    return
        else:
            self.myopts.pop("--getbinpkg", None)
            self.myopts.pop("--getbinpkgonly", None)
            self.myopts.pop("--usepkg", None)
            self.myopts.pop("--usepkgonly", None)

    def update(self):
        self.settings, self.trees, self.mtimedb = \
            load_emerge_config()
        self.vardb = self.trees[self.settings['ROOT']]['vartree'].dbapi
        self.portdb = self.trees[self.settings['ROOT']]['porttree'].dbapi
        self.bindb = self.trees[self.settings['ROOT']]['bintree'].dbapi
        self.root_config = self.trees[self.settings['ROOT']]['root_config']

        self.handle_binpkg()

        self.apply_settings({
            # we don't want interactive ebuilds
            'ACCEPT_PROPERTIES': '-interactive',
            # do not log with mod_echo (cleanly prevent some outputs)
            'PORTAGE_ELOG_SYSTEM': ' '.join([
                elog for elog in self.settings["PORTAGE_ELOG_SYSTEM"].split()
                if elog != 'echo'
            ]),
        })

    def apply_settings(self, mapping):
        """Set portage settings."""
        self.settings.unlock()

        for key, value in mapping.items():
            self.settings[key] = value
            self.settings.backup_changes(key)

        self.settings.regenerate()
        self.settings.lock()


class PackageKitPortageMixin(object):

    def __init__(self):
        object.__init__(self)

        self.pvar = PortageBridge()
        # TODO: should be removed when using non-verbose function API
        # FIXME: avoid using /dev/null, dangerous (ro fs)
        self._dev_null = open('/dev/null', 'w')
        # TODO: atm, this stack keep tracks of elog messages
        self._elog_messages = []
        self._error_message = ""
        self._error_phase = ""
        self._buildid_cache = {}

    # TODO: should be removed when using non-verbose function API
    def _block_output(self):
        sys.stdout = self._dev_null
        sys.stderr = self._dev_null

    # TODO: should be removed when using non-verbose function API
    def _unblock_output(self):
        sys.stdout = sys.__stdout__
        sys.stderr = sys.__stderr__

    def _has_flag(self, flags, flag):
        try:
            return (flags & flag) == flag
        except TypeError:
            return flag in flags
    
    def _is_allow_downgrade(self, transaction_flags):
        return self._has_flag(transaction_flags, TRANSACTION_FLAG_ALLOW_DOWNGRADE)

    def _is_allow_reinstall(self, transaction_flags):
        return self._has_flag(transaction_flags, TRANSACTION_FLAG_ALLOW_REINSTALL)

    def _is_only_trusted(self, transaction_flags):
        return self._has_flag(transaction_flags, TRANSACTION_FLAG_ONLY_TRUSTED)

    def _is_simulate(self, transaction_flags):
        return self._has_flag(transaction_flags, TRANSACTION_FLAG_SIMULATE)

    def _is_only_download(self, transaction_flags):
        return self._has_flag(transaction_flags, TRANSACTION_FLAG_ONLY_DOWNLOAD)

    def _eselect_enabled_repos(self):
        """Return a set of enabled repository names via eselect-repository."""
        try:
            out = subprocess.run(
                ["eselect", "repository", "list", "-i"],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                check=False,
            )
            repos = set()
            for line in out.stdout.splitlines():
                # Typical line:  [1]  gentoo (enabled)
                parts = line.split()
                if len(parts) >= 2:
                    repos.add(parts[1])
            return repos
        except Exception:
            return set()

    def _eselect_all_repos(self):
        """Return an ordered list of (name, enabled_bool) from eselect."""
        try:
            enabled = self._eselect_enabled_repos()
            out = subprocess.run(
                ["eselect", "repository", "list"],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                check=False,
            )
            repos = []
            for line in out.stdout.splitlines():
                parts = line.split()
                if len(parts) >= 2:
                    name = parts[1]
                    repos.append((name, name in enabled))
            return repos
        except Exception:
            return []

    def _is_repo_enabled(self, repo_name):
        return repo_name in self._eselect_enabled_repos()

    def _get_search_list(self, keys_list):
        '''
        Get a string composed of keys (separated with spaces).
        Returns a list of compiled regular expressions.
        '''
        search_list = []

        for k in keys_list:
            # not done entirely by pk-transaction
            k = re.escape(k)
            search_list.append(re.compile(k, re.IGNORECASE))

        return search_list

    def _get_portage_categories(self):
        """
        Return a list of available Portage categories
        """
        return self.pvar.settings.categories

    def _get_portage_category_description(self, category):

        from xml.dom import minidom
        data = {}
        portdir = self.pvar.settings['PORTDIR']
        myfile = os.path.join(portdir, category, "metadata.xml")
        if os.access(myfile, os.R_OK) and os.path.isfile(myfile):
            doc = minidom.parse(myfile)
            longdescs = doc.getElementsByTagName("longdescription")
            for longdesc in longdescs:
                data[longdesc.getAttribute("lang").strip()] = \
                    ' '.join([x.strip() for x in
                              longdesc.firstChild.data.strip().split("\n")])

        # Only return in plain English since Portage doesn't support i18n/l10n
        return data.get('en', "No description")

    def _get_portage_groups(self):
        """
        Return an expanded version of PortagePackageGroups
        """
        groups = PortagePackageGroups()
        categories = self._get_portage_categories()

        # expand categories
        for data in list(groups.values()):

            exp_cats = set()
            for g_cat in data['categories']:
                exp_cats.update([x for x in categories if x.startswith(g_cat)])
            data['categories'] = sorted(exp_cats)

        return groups

    def _get_pk_group(self, cp):
        """
        Return PackageKit group belonging to given Portage package.
        """
        category = portage.versions.catsplit(cp)[0]
        group_data = [key for key, data in self._get_portage_groups().items()
                      if category in data['categories']]
        try:
            generic_group_name = group_data.pop(0)
        except IndexError:
            return GROUP_UNKNOWN

        return PackageKitPortageBackend.GROUP_MAP[generic_group_name]

    def _get_portage_group(self, pk_group):
        """
        Given a PackageKit group identifier, return Portage packages group.
        """
        group_map = PackageKitPortageBackend.GROUP_MAP
        # reverse dict
        group_map_reverse = dict((y, x) for x, y in group_map.items())
        return group_map_reverse.get(pk_group, 'unknown')

    def _get_ebuild_settings(self, cpv, metadata):
        """
        Return values of given metadata keys for given Portage CPV.
        """
        settings = portage.config(clone=self.pvar.settings)
        settings.setcpv(cpv, mydb=metadata)
        return settings

    def _is_installed(self, cpv):
        base = self._strip_buildid_for_db(cpv)
        return self.pvar.vardb.cpv_exists(base)

    def _is_cpv_valid(self, cpv):
        base = self._strip_buildid_for_db(cpv)
        if self._is_installed(base):
            return True
        try:
            if getattr(self.pvar, "_allow_binpkgs", False):
                if self.pvar.bindb.cpv_exists(base):
                    return True
        except Exception:
            pass
        return self.pvar.portdb.cpv_exists(base)

    def _is_binpkg(self, cpv):
        base = self._strip_buildid_for_db(cpv)
        try:
            if getattr(self.pvar, "_allow_binpkgs", False):
                return self.pvar.bindb.cpv_exists(base)
            return False
        except Exception:
            return False

    def _is_strictly_newer_than_installed(self, cpv):
        """
        Return True if the given cpv is strictly newer than
        the currently installed version(s) of the same package.
        """     
        try:
            tcpv = self._strip_buildid_for_db(cpv)
            cp, ver, rev = portage.versions.pkgsplit(tcpv)
        except Exception:
            self.error(
                ERROR_PACKAGE_ID_INVALID,
                f"Failed to parse package version from CPV: {cpv}"
            )
            return False
            
        installed_cpvs = self.pvar.vardb.match(cp)
        if not installed_cpvs:
            # nothing installed, so it's "newer" by definition
            return True
        
        for inst_cpv in installed_cpvs: 
            if self._cmp_cpv(tcpv, inst_cpv) <= 0:
                return False
        
        return True


    def _get_real_license_str(self, cpv, metadata):
        # use conditionals info (w/ USE) in LICENSE and remove ||
        ebuild_settings = self._get_ebuild_settings(cpv, metadata)
        license_ = set(portage.flatten(
            portage.dep.use_reduce(
                portage.dep.paren_reduce(metadata["LICENSE"]),
                uselist=ebuild_settings.get("USE", "").split()
            )
        ))
        license_.discard('||')
        return ' '.join(license_)

    def _signal_config_update(self):
        result = list(portage.util.find_updated_config_files(
            self.pvar.settings['ROOT'],
            self.pvar.settings.get('CONFIG_PROTECT', '').split()))

        # if result:
        #    self.message(
        #        MESSAGE_CONFIG_FILES_CHANGED,
        #        "Some configuration files need updating."
        #        ";You should use Gentoo's tools to update them (dispatch-conf)"
        #        ";If you can't do that, ask your system administrator."
        #    )

    def _get_restricted_fetch_files(self, cpv, metadata):
        '''
        This function checks files in SRC_URI and look if they are in DESTDIR.
        Missing files are returned. If there is no issue, None is returned.
        We don't care about digest but only about existence of files.

        NOTES:
        - we are assuming the package has RESTRICT='fetch'
          be sure to call this function only in this case.
        - we are not using fetch_check because it's not returning missing files
          so this function is a simplist fetch_check
        '''
        missing_files = []
        ebuild_settings = self._get_ebuild_settings(cpv, metadata)

        files = self.pvar.portdb.getFetchMap(cpv,
                                             ebuild_settings['USE'].split())

        for f in files:
            file_path = os.path.join(ebuild_settings["DISTDIR"], f)
            if not os.access(file_path, os.F_OK):
                missing_files.append([file_path, files[f]])

        return missing_files if missing_files else None

    def _check_fetch_restrict(self, packages_list):
        for pkg in packages_list:
            if 'fetch' not in pkg.metadata['RESTRICT']:
                continue

            files = self._get_restricted_fetch_files(pkg.cpv, pkg.metadata)
            if files:
                message = (
                    "Package {0} can't download some files."
                    ";Please, download manually the following file(s): "
                ).format(pkg.cpv)
                message += ''.join([
                    ";- {0} then copy it to {1}"
                    .format(' '.join(file_info[1]), file_info[0])
                    for file_info in files
                ])
                self.error(ERROR_RESTRICTED_DOWNLOAD, message)

    def _elog_listener(self, settings, key, logentries, fulltext):
        '''
        This is a listener for elog.
        It's called each time elog is emitting log messages (at end of process).
        We are not using settings and fulltext but they are used by other
        listeners so we have to keep them as arguments.
        '''
        message = "Messages for package %s:;" % str(key)
        error_message = ""

        # building the message
        for phase in logentries:
            for entries in logentries[phase]:
                type = entries[0]
                messages = entries[1]

                # TODO: portage.elog.filtering is using upper() should we ?
                if type == 'LOG':
                    message += ";Information messages:"
                elif type == 'WARN':
                    message += ";Warning messages:"
                elif type == 'QA':
                    message += ";QA messages:"
                elif type == 'ERROR':
                    message += ";Error messages:"
                    self._error_phase = phase
                else:
                    continue

                for msg in messages:
                    msg = msg.replace('\n', '')
                    if type == 'ERROR':
                        error_message += msg + ";"
                    message += "; " + msg

        # add the message to the stack
        self._elog_messages.append(message)
        self._error_message = message

    def _extract_buildid(self, cpv):
        """
        If cpv ends with an integer build-id suffix (after optional -rN),
        return (base_cpv, build_id_int). Otherwise return (cpv, None).
        """
        if not cpv or "-" not in cpv:
            return (cpv, None)

        m = re.match(r"^(?P<base>.+?)-(?P<last>\d+)$", cpv)
        if not m:
            return (cpv, None)

        base_candidate = m.group("base")
        bid_candidate = int(m.group("last"))

        try:
            if (self.pvar.vardb.cpv_exists(base_candidate)
                    or self.pvar.portdb.cpv_exists(base_candidate)
                    or self.pvar.bindb.cpv_exists(base_candidate)):
                return (base_candidate, bid_candidate)
        except Exception:
            return (base_candidate, bid_candidate)

        return (cpv, None)

    def _strip_buildid_for_db(self, cpv):
        """
        Return the base CPV suitable for DB lookups.
        """
        base, _ = self._extract_buildid(cpv)
        return base

    def _list_binpkg_buildids(self, base_cpv):
        """
        Return sorted list of build IDs available for base_cpv.
        Combines local PKGDIR scan and bindb (remote binhost metadata).
        """
        if base_cpv in self._buildid_cache:
            return self._buildid_cache[base_cpv]

        bids = set()

        if getattr(self.pvar, "_allow_binpkgs", False):
            try:
                package, version, rev = portage.versions.pkgsplit(base_cpv)
                pn = package.split("/", 1)[1]
                pf = pn + "-" + version
                if rev != "r0":
                    pf = pf + "-" + rev
            except Exception:
                self._buildid_cache[base_cpv] = []
                return []

            pkgdirs_raw = self.pvar.settings.get("PKGDIR", "") or ""
            pkgdirs = pkgdirs_raw.split() if isinstance(pkgdirs_raw, str) else pkgdirs_raw

            for pkgdir in pkgdirs:
                cat, pn = package.split("/", 1)
                candidate_dir = os.path.join(pkgdir, cat, pn)
                if not os.path.isdir(candidate_dir):
                    continue
                for fname in os.listdir(candidate_dir):
                    if not (fname.endswith((".tbz2", ".tbz", ".tb2", ".xpak", ".gpkg.tar"))):
                        continue
                    if fname.startswith(pf + "-"):
                        bid_str = fname[len(pf) + 1:].split(".")[0]
                        if bid_str.isdigit():
                            bids.add(int(bid_str))

        try:
            if getattr(self.pvar, "_allow_binpkgs", False):
                for cpv in self.pvar.bindb.match(base_cpv):
                    try:
                        build_id_str = self.pvar.bindb.aux_get(cpv, ["BUILD_ID"])[0]
                        if build_id_str and build_id_str.isdigit():
                            bids.add(int(build_id_str))
                    except Exception:
                        continue
        except Exception:
            # fall back: no bindb data
            pass

        bids_list = sorted(bids)
        self._buildid_cache[base_cpv] = bids_list
        return bids_list


    def _send_merge_error(self, default):
        if self._error_phase in ("setup", "unpack", "prepare", "configure",
                                 "nofetch", "config", "info"):
            error_type = ERROR_PACKAGE_FAILED_TO_CONFIGURE
        elif self._error_phase in ("compile", "test"):
            error_type = ERROR_PACKAGE_FAILED_TO_BUILD
        elif self._error_phase in ("install", "preinst", "postinst",
                                   "package"):
            error_type = ERROR_PACKAGE_FAILED_TO_INSTALL
        elif self._error_phase in ("prerm", "postrm"):
            error_type = ERROR_PACKAGE_FAILED_TO_REMOVE
        else:
            error_type = default
        self.error(error_type, self._error_message)


    def _get_file_list(self, cpv):
        cat, pv = portage.versions.catsplit(cpv)
        db = portage.dblink(cat, pv, self.pvar.settings['ROOT'],
                            self.pvar.settings, treetype="vartree",
                            vartree=self.pvar.vardb)

        contents = db.getcontents()
        return contents.keys() if contents else []

    def _cmp_cpv(self, cpv1, cpv2):
        '''
        returns 1 if cpv1 > cpv2
        returns 0 if cpv1 = cpv2
        returns -1 if cpv1 < cpv2
        '''
        return portage.versions.pkgcmp(portage.versions.pkgsplit(cpv1),
                                       portage.versions.pkgsplit(cpv2))

    def _get_newest_cpv(self, cpv_list, installed):
        newer = ""

        # get the first cpv following the installed rule
        for cpv in cpv_list:
            if self._is_installed(cpv) == installed:
                newer = cpv
                break

        if newer == "":
            return ""

        for cpv in cpv_list:
            if self._is_installed(cpv) == installed:
                if self._cmp_cpv(cpv, newer) == 1:
                    newer = cpv

        return newer

    def _get_metadata(self, cpv, keys, in_dict=False, add_cache_keys=False):
        '''
        This function returns required metadata.
        If in_dict is True, metadata is returned in a dict object.
        If add_cache_keys is True, cached keys are added to keys in parameter.
        '''
        base_cpv, build_id = self._extract_buildid(cpv)

        if self._is_installed(base_cpv):
            db = self.pvar.vardb
        elif self._is_binpkg(base_cpv):
            db = self.pvar.bindb
        else:
            db = self.pvar.portdb

        try:
            res = db.aux_get(base_cpv, keys)
        except Exception:
            if in_dict:
                return dict(zip(keys, ("",) * len(keys)))
            else:
                return tuple("" for _ in keys)

        if "BUILD_ID" in keys and build_id is not None:
            if in_dict:
                res = dict(zip(keys, res)) if not isinstance(res, dict) else dict(res)
                res["BUILD_ID"] = str(build_id)
            else:
                tmp = list(res)
                idx = keys.index("BUILD_ID")
                tmp[idx] = str(build_id)
                res = tuple(tmp)

        if in_dict:
            if not isinstance(res, dict):
                return dict(zip(keys, res))
            return res
        else:
            if isinstance(res, dict):
                return tuple(res.get(k, "") for k in keys)
            return res


    def _get_size(self, cpv):
        '''
        Returns the installed size if the package is installed.
        Otherwise, the size of files needed to be downloaded.
        If some required files have been downloaded,
        only the remaining size will be considered.
        '''
        size = 0
        if self._is_installed(cpv):
            size = self._get_metadata(cpv, ["SIZE"])[0]
            size = int(size) if size else 0
        else:
            keys = ["EAPI", "IUSE", "SLOT", "repository", "KEYWORDS", "PROPERTIES", "RESTRICT"]
            metadata = self._get_metadata(cpv, keys, in_dict=True)
            for k in keys:
                if k not in metadata or metadata[k] is None:
                    metadata[k] = ""

            package = _emerge.Package.Package(
                type_name="ebuild",
                built=False,
                installed=False,
                root_config=self.pvar.root_config,
                cpv=cpv,
                metadata=metadata
            )
            fetch_file = self.pvar.portdb.getfetchsizes(package[2],
                                                        package.use.enabled)
            #size = sum(fetch_file)
            size = sum(f[0] for f in fetch_file if isinstance(f[0], int))

        return size

    def _get_cpv_slotted(self, cpv_list):
        cpv_dict = defaultdict(list)

        for cpv in cpv_list:
            slot = self._get_metadata(cpv, ["SLOT"])[0]
            cpv_dict[slot].append(cpv)

        return cpv_dict

    def _filter_free(self, cpv_list, filters):
        if not cpv_list:
            return cpv_list

        def _has_validLicense(cpv):
            metadata = self._get_metadata(
                cpv, ["LICENSE", "USE", "SLOT"], True)
            return not self.pvar.settings._getMissingLicenses(cpv, metadata)

        if FILTER_FREE in filters or FILTER_NOT_FREE in filters:
            licenses = ""
            free_licenses = "@FSF-APPROVED"
            if FILTER_FREE in filters:
                licenses = "-* " + free_licenses
            elif FILTER_NOT_FREE in filters:
                licenses = "* -" + free_licenses
            backup_licenses = self.pvar.settings["ACCEPT_LICENSE"]

            self.pvar.apply_settings({'ACCEPT_LICENSE': licenses})
            cpv_list = list(filter(_has_validLicense, cpv_list))
            self.pvar.apply_settings({'ACCEPT_LICENSE': backup_licenses})

        return cpv_list

    def _filter_newest(self, cpv_list, filters):
        if len(cpv_list) == 0:
            return cpv_list

        if FILTER_NEWEST not in filters:
            return cpv_list

        if FILTER_INSTALLED in filters:
            # we have one package per slot, so it's the newest
            return cpv_list

        cpv_dict = self._get_cpv_slotted(cpv_list)

        # slots are sorted (dict), revert them to have newest slots first
        slots = sorted(cpv_dict.keys(), reverse=True)

        # empty cpv_list, cpv are now in cpv_dict and cpv_list gonna be repop
        cpv_list = []

        for k in slots:
            # if not_intalled on, no need to check for newest installed
            if FILTER_NOT_INSTALLED not in filters:
                newest_installed = self._get_newest_cpv(cpv_dict[k], True)
                if newest_installed != "":
                    cpv_list.append(newest_installed)
            newest_available = self._get_newest_cpv(cpv_dict[k], False)
            if newest_available != "":
                cpv_list.append(newest_available)

        return cpv_list

    def _get_all_cp(self, filters):
        # NOTES:
        # returns a list of cp
        #
        # FILTERS:
        # - installed: ok
        # - free: ok (should be done with cpv)
        # - newest: ok (should be finished with cpv)
        cp_list = []

        if FILTER_INSTALLED in filters:
            cp_list = self.pvar.vardb.cp_all()
        elif FILTER_NOT_INSTALLED in filters:
            cp_list = self.pvar.portdb.cp_all()
        else:
            # need installed packages first
            cp_list = self.pvar.vardb.cp_all()
            for cp in self.pvar.portdb.cp_all():
                if cp not in cp_list:
                    cp_list.append(cp)
            if getattr(self.pvar, "_allow_binpkgs", False):
                for cp in self.pvar.bindb.cp_all():
                    if cp not in cp_list:
                        cp_list.append(cp)

        return cp_list

    def _get_all_cpv(self, cp, filters, filter_newest=True):
        # NOTES:
        # returns a list of cpv
        #
        # FILTERS:
        # - installed: ok
        # - free: ok
        # - newest: ok
        installed_cpvs = list(self.pvar.vardb.match(cp))
        available_cpvs = [x for x in self.pvar.portdb.match(cp)
                          if not self._is_installed(x)]
        bin_cpvs = []
        if getattr(self.pvar, "_allow_binpkgs", False):
            bin_cpvs = [x for x in self.pvar.bindb.match(cp)
                        if not self._is_installed(x)]

        if FILTER_INSTALLED in filters:
            cpv_list = installed_cpvs
        elif FILTER_NOT_INSTALLED in filters:
            cpv_list = available_cpvs + bin_cpvs
        else:
            cpv_list = []
            cpv_list.extend(installed_cpvs)
            for x in available_cpvs + bin_cpvs:
                if x not in cpv_list:
                    cpv_list.append(x)

        cpv_list = self._filter_free(cpv_list, filters)

        if filter_newest:
            cpv_list = self._filter_newest(cpv_list, filters)

        return cpv_list


    def _id_to_cpv(self, pkgid):
        '''
        Transform the package id (packagekit) to a cpv (portage)
        '''
        ret = split_package_id(pkgid)

        if len(ret) < 4:
            self.error(ERROR_PACKAGE_ID_INVALID,
                       "The package id %s does not contain 4 fields" % pkgid)
        if '/' not in ret[0]:
            self.error(ERROR_PACKAGE_ID_INVALID,
                       "The first field of the package id must contain"
                       " a category")
        cp = ret[0]
        version = ret[1].split(':')[0]

        m_rev_build = re.match(r"(.+)-r(\d+)-(\d+)$", version)
        if m_rev_build:
            basever, rev, bid = m_rev_build.groups()
            return f"{cp}-{basever}-r{rev}-{bid}"

        m_build = re.match(r"(.+)-(\d+)$", version)
        if m_build:
            basever, bid = m_build.groups()
            return f"{cp}-{basever}-{bid}"

        m = re.match(r"(.+)-r(\d+)$", version)
        if m:
            basever, rev = m.groups()
            return f"{cp}-{basever}-r{rev}"

        return f"{cp}-{version}"


    def _cpv_to_id(self, cpv):
        '''
        Transform the cpv (portage) to a package id (packagekit)
        '''

        package, version, rev = portage.versions.pkgsplit(cpv)
        pkg_keywords, repo_meta, slot = self._get_metadata(cpv, ["KEYWORDS", "repository", "SLOT"])

        # filter accepted keywords
        keywords = list(set(pkg_keywords.split()).intersection(
            set(self.pvar.settings["ACCEPT_KEYWORDS"].split())
        ))

        # if no keywords, check in package.keywords
        if not keywords:
            key_dict = self.pvar.settings.pkeywordsdict.get(
                portage.versions.cpv_getkey(cpv)
            )
            if key_dict:
                for keys in key_dict.values():
                    keywords.extend(keys)

        if not keywords:
            keywords.append("no keywords")

        if rev != "r0":
            version = version + "-" + rev

        build_id = getattr(self, "_selected_build_id", None)
        if build_id is not None:
            version = version + "-" + str(build_id)

        if slot != '0':
            version = version + ':' + slot

        if self._is_installed(cpv):
            repo = "installed"
        elif self._is_binpkg(cpv):
            repo = "binpkg"
        else:
            repo = repo_meta

        return get_package_id(package, version, ' '.join(keywords), repo)



    def _get_required_packages(self, cpv_input, recursive):
        '''
        Get a list of cpv and recursive parameter.
        Returns the list of packages required for cpv list.
        '''
        packages_list = []

        myopts = {}
        myopts["--selective"] = True
        myopts["--deep"] = True

        myparams = _emerge.create_depgraph_params \
            .create_depgraph_params(myopts, "remove")
        depgraph = _emerge.depgraph.depgraph(self.pvar.settings,
                                             self.pvar.trees, myopts,
                                             myparams, None)

        # TODO: atm, using FILTER_INSTALLED because it's quicker
        # and we don't want to manage non-installed packages
        for cp in self._get_all_cp([FILTER_INSTALLED]):
            for cpv in self._get_all_cpv(cp, [FILTER_INSTALLED]):
                depgraph._dynamic_config._dep_stack.append(
                    _emerge.Dependency.Dependency(
                        atom=portage.dep.Atom('=' + cpv),
                        root=self.pvar.settings["ROOT"],
                        parent=None
                    )
                )

        if not depgraph._complete_graph():
            self.error(ERROR_INTERNAL_ERROR, "Error when generating depgraph")
            return

        def _add_children_to_list(packages_list, node):
            for n in depgraph._dynamic_config.digraph.parent_nodes(node):
                if n not in packages_list \
                        and not isinstance(n, _emerge.SetArg.SetArg):
                    packages_list.append(n)
                    _add_children_to_list(packages_list, n)

        for node in depgraph._dynamic_config.digraph.__iter__():
            if isinstance(node, _emerge.SetArg.SetArg):
                continue
            if node.cpv in cpv_input:
                if recursive:
                    _add_children_to_list(packages_list, node)
                else:
                    for n in \
                            depgraph._dynamic_config.digraph.parent_nodes(node):
                        if not isinstance(n, _emerge.SetArg.SetArg):
                            packages_list.append(n)

        # remove cpv_input that may be added to the list
        def filter_cpv_input(x):
            return x.cpv not in cpv_input
        return list(filter(filter_cpv_input, packages_list))


class PackageKitPortageBackend(PackageKitPortageMixin, PackageKitBaseBackend):

    # Portage <-> PackageKit groups map
    GROUP_MAP = {
        'accessibility': GROUP_ACCESSIBILITY,
        'development': GROUP_PROGRAMMING,
        'games': GROUP_GAMES,
        'gnome': GROUP_DESKTOP_GNOME,
        'kde': GROUP_DESKTOP_KDE,
        'lxde': GROUP_DESKTOP_OTHER,
        'multimedia': GROUP_MULTIMEDIA,
        'networking': GROUP_NETWORK,
        'office': GROUP_OFFICE,
        'science': GROUP_SCIENCE,
        'system': GROUP_SYSTEM,
        'security': GROUP_SECURITY,
        'x11': GROUP_OTHER,
        'xfce': GROUP_DESKTOP_XFCE,
        'unknown': GROUP_UNKNOWN,
    }

    def __sigquit(self, signum, frame):
        raise SystemExit(1)

    def __init__(self, args):
        signal.signal(signal.SIGQUIT, self.__sigquit)
        PackageKitPortageMixin.__init__(self)
        PackageKitBaseBackend.__init__(self, args)

    def _package(self, cpv, info=None):
        desc = self._get_metadata(cpv, ["DESCRIPTION"])[0]
        if not info:
            if self._is_installed(cpv):
                info = INFO_INSTALLED
            else:
                info = INFO_AVAILABLE

        prev_bid = getattr(self, "_selected_build_id", None)
        limit_buildids = getattr(self, "_limit_buildids_to_latest", False)

        self._selected_build_id = None

        # If in "resolve/install" mode (limit_buildids true) and this package
        # has binpkg buildids available, emit only the latest build id entry.
        # If note keep the old behavior (base entry + per-build-id entries).
        if limit_buildids and self._is_binpkg(cpv) and not self._is_installed(cpv):
            bids = self._list_binpkg_buildids(cpv)
            if bids:
                self._selected_build_id = max(bids)
                self.package(self._cpv_to_id(cpv), info, desc)
            else:
                # fallback to normal single entry if no build IDs present
                self.package(self._cpv_to_id(cpv), info, desc)
        else:
            self.package(self._cpv_to_id(cpv), info, desc)

            if self._is_binpkg(cpv) and not self._is_installed(cpv):
                bids = self._list_binpkg_buildids(cpv)
                for bid in bids:
                    self._selected_build_id = bid
                    self.package(self._cpv_to_id(cpv), info, desc)

        self._selected_build_id = prev_bid


    def get_categories(self):

        self.status(STATUS_QUERY)
        self.allow_cancel(True)

        categories = self._get_portage_categories()
        if not categories:
            self.error(ERROR_GROUP_LIST_INVALID, "no package categories")
            return

        for name in categories:

            summary = self._get_portage_category_description(name)

            f_name = "/usr/share/pixmaps/portage/%s.png" % (name,)
            if os.path.isfile(f_name) and os.access(f_name, os.R_OK):
                icon = name
            else:
                icon = "image-missing"

            cat_id = name  # same thing
            self.category("", cat_id, name, summary, icon)

    def depends_on(self, filters, pkgs, recursive):
        # TODO: use only myparams ?
        # TODO: improve error management / info

        # FILTERS:
        # - installed: ok
        # - free: ok
        # - newest: ignored because only one version of a package is installed

        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        cpv_input = []
        cpv_list = []

        for pkg in pkgs:
            cpv = self._id_to_cpv(pkg)
            if not self._is_cpv_valid(cpv):
                self.error(ERROR_PACKAGE_NOT_FOUND,
                           "Package %s was not found" % pkg)
                continue
            cpv_input.append('=' + cpv)

        myopts = {}
        myopts["--selective"] = True
        myopts["--deep"] = True
        myparams = _emerge.create_depgraph_params \
            .create_depgraph_params(myopts, "")

        depgraph = _emerge.depgraph.depgraph(self.pvar.settings,
                                             self.pvar.trees, myopts,
                                             myparams, None)
        retval, fav = depgraph.select_files(cpv_input)

        if not retval:
            self.error(ERROR_DEP_RESOLUTION_FAILED,
                       "Wasn't able to get dependency graph")
            return

        def _add_children_to_list(cpv_list, node):
            for n in depgraph._dynamic_config.digraph.child_nodes(node):
                if n not in cpv_list:
                    cpv_list.append(n)
                    _add_children_to_list(cpv_list, n)

        for cpv in cpv_input:
            for r in depgraph._dynamic_config.digraph.root_nodes():
                # TODO: remove things with @ as first char
                # TODO: or refuse SetArgs
                if not isinstance(r, _emerge.AtomArg.AtomArg):
                    continue
                if r.atom == cpv:
                    if recursive:
                        _add_children_to_list(cpv_list, r)
                    else:
                        for n in \
                                depgraph._dynamic_config.digraph.child_nodes(r):
                            for c in \
                                    depgraph._dynamic_config.digraph.child_nodes(n):
                                cpv_list.append(c)

        def _filter_uninstall(cpv):
            return cpv[3] != 'uninstall'

        def _filter_installed(cpv):
            return cpv[0] == 'installed'

        def _filter_not_installed(cpv):
            return cpv[0] != 'installed'

        # removing packages going to be uninstalled
        cpv_list = list(filter(_filter_uninstall, cpv_list))

        # install filter
        if FILTER_INSTALLED in filters:
            cpv_list = list(filter(_filter_installed, cpv_list))
        if FILTER_NOT_INSTALLED in filters:
            cpv_list = list(filter(_filter_not_installed, cpv_list))

        # now we can change cpv_list to a real cpv list
        tmp_list = cpv_list[:]
        cpv_list = [x[2] for x in tmp_list]
        del tmp_list

        # free filter
        cpv_list = self._filter_free(cpv_list, filters)

        for cpv in cpv_list:
            # prevent showing input packages
            if '=' + cpv not in cpv_input:
                try:
                    self._package(cpv)
                except InvalidAtom:
                    continue

    def get_details(self, pkgs):
        self.status(STATUS_INFO)
        self.allow_cancel(True)

        progress = PackagekitProgress(compute_equal_steps(pkgs))
        self.percentage(progress.percent)

        for percentage, pkg in zip(progress, pkgs):
            cpv = self._id_to_cpv(pkg)

            if not self._is_cpv_valid(cpv):
                self.error(ERROR_PACKAGE_NOT_FOUND,
                           "Package %s was not found" % pkg)
                continue

            metadata = self._get_metadata(
                cpv, ["DESCRIPTION", "HOMEPAGE", "IUSE", "LICENSE", "SLOT",
                      "EAPI", "KEYWORDS"],
                in_dict=True
            )

            self.details(
                self._cpv_to_id(cpv),
                '',
                self._get_real_license_str(cpv, metadata),
                self._get_pk_group(cpv),
                metadata["DESCRIPTION"],
                metadata["HOMEPAGE"],
                self._get_size(cpv)
            )

            self.percentage(percentage)

        self.percentage(100)

    def get_files(self, pkgs):
        self.status(STATUS_INFO)
        self.allow_cancel(True)

        progress = PackagekitProgress(compute_equal_steps(pkgs))
        self.percentage(progress.percent)

        for percentage, pkg in zip(progress, pkgs):
            cpv = self._id_to_cpv(pkg)

            if not self._is_cpv_valid(cpv):
                self.error(ERROR_PACKAGE_NOT_FOUND,
                           "Package %s was not found" % pkg)
                continue

            if not self._is_installed(cpv):
                self.error(ERROR_CANNOT_GET_FILELIST,
                           "get-files is only available for installed"
                           " packages")
                continue

            self.files(pkg, ';'.join(sorted(self._get_file_list(cpv))))

            self.percentage(percentage)

        self.percentage(100)

    def get_packages(self, filters):
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(0)

        if FILTER_INSTALLED in filters:
            cp_list = self.pvar.vardb.cp_all()
        else:
            cp_list = self._get_all_cp(filters)
        progress = PackagekitProgress(compute_equal_steps(cp_list))

        for percentage, cp in zip(progress, cp_list):
            for cpv in self._get_all_cpv(cp, filters):
                try:
                    self._package(cpv)
                except InvalidAtom:
                    continue

            self.percentage(percentage)

        self.percentage(100)

    def get_repo_list(self, filters):
        """Get list of repositories (via eselect-repository)."""
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        # Always include gentoo (dummy entry like original)
        self.repo_detail('gentoo', 'Gentoo Portage tree', True)

        if FILTER_NOT_DEVELOPMENT not in filters:
            for repo_name, enabled in self._eselect_all_repos():
                if repo_name == "gentoo":
                    continue
                self.repo_detail(repo_name, repo_name, enabled)

    def required_by(self, filters, pkgs, recursive):
        # TODO: manage non-installed package

        # FILTERS:
        # - installed: error atm, see previous TODO
        # - free: ok
        # - newest: ignored because only one version of a package is installed

        self.status(STATUS_RUNNING)
        self.allow_cancel(True)
        self.percentage(None)

        cpv_input = []
        cpv_list = []

        if FILTER_NOT_INSTALLED in filters:
            self.error(ERROR_CANNOT_GET_REQUIRES,
                       "required-by returns only installed packages"
                       " at the moment")
            return

        for pkg in pkgs:
            cpv = self._id_to_cpv(pkg)

            if not self._is_cpv_valid(cpv):
                self.error(ERROR_PACKAGE_NOT_FOUND,
                           "Package %s was not found" % pkg)
                continue
            if not self._is_installed(cpv):
                self.error(ERROR_CANNOT_GET_REQUIRES,
                           "required-by is only available for installed"
                           " packages at the moment")
                continue

            cpv_input.append(cpv)

        # populate cpv list
        packages_list = self._get_required_packages(cpv_input, recursive)
        cpv_list = [package.cpv for package in packages_list]
        del packages_list

        # free filter
        cpv_list = self._filter_free(cpv_list, filters)

        for cpv in cpv_list:
            # prevent showing input packages
            if '=' + cpv not in cpv_input:
                try:
                    self._package(cpv)
                except InvalidAtom:
                    continue

    def get_update_detail(self, pkgs):
        # TODO: a lot of informations are missing

        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        for pkg in pkgs:
            updates = []
            obsoletes = ""
            vendor_url = ""
            bugzilla_url = ""
            cve_url = ""

            cpv = self._id_to_cpv(pkg)

            # if not self.pvar.portdb.cpv_exists(cpv):
            #    self.message(MESSAGE_COULD_NOT_FIND_PACKAGE,
            #                 "could not find %s" % pkg)

            for cpv in self.pvar.vardb.match(portage.versions.pkgsplit(cpv)[0]):
                updates.append(cpv)
            updates = "&".join(updates)

            # temporarily set vendor_url = homepage
            homepage = self._get_metadata(cpv, ["HOMEPAGE"])[0]
            vendor_url = homepage
            issued = ""
            updated = ""

            self.update_detail(
                pkg, updates, obsoletes, vendor_url, bugzilla_url, cve_url,
                "none", "No update text", "No ChangeLog", UPDATE_STATE_STABLE,
                issued, updated
            )

    def get_updates(self, filters):
        # NOTES:
        # because of a lot of things related to Gentoo,
        # only world and system packages are can be listed as updates
        # _except_ for security updates

        # UPDATE TYPES:
        # - blocked: wait for feedbacks
        # - low: TODO: --newuse
        # - normal: default
        # - important: none atm
        # - security: from @security

        # FILTERS:
        # - installed: try to update non-installed packages and call me ;)
        # - free: ok
        # - newest: ok

        # Need to include for the moment, might need to change behaviour later, but it seems nobody sets obvious flags anymore
        if FILTER_NEWEST not in filters:
            filters.append(FILTER_NEWEST)

        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        update_candidates = []
        cpv_updates = {}
        cpv_downgra = {}

        # get system and world packages
        for pkg_set in ["system", "world"]:
            sets = InternalPackageSet(initial_atoms=self.pvar.root_config
                                      .setconfig.getSetAtoms(pkg_set))
            for atom in sets:
                update_candidates.append(atom.cp)

        self.pvar.update()

        import functools
        cmp_key = functools.cmp_to_key(
            lambda a, b: portage.versions.pkgcmp(
                portage.versions.pkgsplit(a),
                portage.versions.pkgsplit(b)
            )
        )

        # check if a candidate can be updated
        for cp in update_candidates:
            cpv_list_inst = self.pvar.vardb.match(cp)
            cpv_list_avai = self.pvar.portdb.match(cp)

            if getattr(self.pvar, "_allow_binpkgs", False):
                bin_list = sorted(self.pvar.bindb.match(cp), key=cmp_key)
                cpv_list_avai = cpv_list_avai + [x for x in bin_list if x not in cpv_list_avai]

            # IMPORTANT: sort the *entire* combined list
            cpv_list_avai = sorted(cpv_list_avai, key=cmp_key)

            cpv_dict_inst = self._get_cpv_slotted(cpv_list_inst)
            cpv_dict_avai = self._get_cpv_slotted(cpv_list_avai)

            dict_upda = {}
            dict_down = {}

            # candidate slots are installed slots
            slots = sorted(cpv_dict_inst.keys(), reverse=True)

            for s in slots:
                cpv_list_updates = []
                cpv_inst = cpv_dict_inst[s][0]  # only one install per slot

                # the slot can be outdated (not in the tree)
                if s not in cpv_dict_avai:
                    break

                tmp_list_avai = cpv_dict_avai[s]
                tmp_list_avai.reverse()

                for cpv in tmp_list_avai:
                    if self._cmp_cpv(cpv_inst, cpv) == -1:
                        cpv_list_updates.append(cpv)
                    else:  # because the list is sorted
                        break

                # no update for this slot
                if len(cpv_list_updates) == 0:
                    if [cpv_inst] == self.pvar.portdb.visible([cpv_inst]):
                        break  # really no update
                    else:
                        # that's actually a downgrade or even worst
                        if len(tmp_list_avai) == 0:
                            break  # this package is not known in the tree...
                        else:
                            dict_down[s] = [tmp_list_avai.pop()]

                cpv_list_updates = self._filter_free(cpv_list_updates, filters)

                if len(cpv_list_updates) == 0:
                    break

                if FILTER_NEWEST in filters:
                    best_cpv = portage.versions.best(cpv_list_updates)
                    cpv_list_updates = [best_cpv]

                dict_upda[s] = cpv_list_updates

            if len(dict_upda) != 0:
                cpv_updates[cp] = dict_upda
            if len(dict_down) != 0:
                cpv_downgra[cp] = dict_down

        # get security updates
        for atom in InternalPackageSet(initial_atoms=self.pvar.root_config
                                       .setconfig.getSetAtoms("security")):
            # send update message and remove atom from cpv_updates
            if atom.cp in cpv_updates:
                slot = self._get_metadata(atom.cpv, ["SLOT"])[0]
                if slot in cpv_updates[atom.cp]:
                    tmp_cpv_list = cpv_updates[atom.cp][slot][:]
                    for cpv in tmp_cpv_list:
                        if self._cmp_cpv(cpv, atom.cpv) >= 0:
                            # cpv is a security update and removed from list
                            cpv_updates[atom.cp][slot].remove(cpv)
                            self._package(cpv, INFO_SECURITY)
            else:  # update also non-world and non-system packages if security
                self._package(atom.cpv, INFO_SECURITY)

        # downgrades
        for cp in cpv_downgra:
            for slot in cpv_downgra[cp]:
                for cpv in cpv_downgra[cp][slot]:
                    self._package(cpv, INFO_IMPORTANT)

        # normal updates
        for cp in cpv_updates:
            for slot in cpv_updates[cp]:
                for cpv in cpv_updates[cp][slot]:
                    self._package(cpv, INFO_NORMAL)

    def install_packages(self, transaction_flags, pkgs):

        only_trusted = self._is_only_trusted(transaction_flags)
        simulate = self._is_simulate(transaction_flags)
        only_download = self._is_only_download(transaction_flags)
        allow_downgrade = self._is_allow_downgrade(transaction_flags)
        allow_reinstall = self._is_allow_reinstall(transaction_flags)

        return self._install_packages(only_trusted, pkgs, simulate=simulate,
                                      only_download=only_download, downgrade=allow_downgrade, reinstall=allow_reinstall)

    def _install_packages(self, only_trusted, pkgs, simulate=False, only_download=False, downgrade=False, reinstall=False):
        """
        Install packages using the same depgraph + scheduler sequence as emerge.
        """

        self.status(STATUS_RUNNING)
        self.allow_cancel(False)
        self.percentage(None)

        cpv_list = []
        for pkg in pkgs:
            cpv = self._id_to_cpv(pkg)
            if not self._is_cpv_valid(cpv):
                self.error(ERROR_PACKAGE_NOT_FOUND, f"Package {pkg} not found or not visible")
                continue
            if not reinstall:
                if self._is_installed(cpv):
                    self.error(ERROR_PACKAGE_ALREADY_INSTALLED, f"Package {pkg} is already installed boy")
                    continue
            if not downgrade:
                if not self._is_strictly_newer_than_installed(cpv):
                    self.error(ERROR_PACKAGE_NOT_FOUND, f"Package {pkg} Is a downgrade and not allowed")
                    continue
            cpv_list.append("=" + cpv)

        if not cpv_list:
            return

        if only_trusted:
            self.error(ERROR_MISSING_GPG_SIGNATURE, "Portage backend does not support GPG signature verification")
            return

        myopts = {}

        try:
            emerge_config = load_emerge_config()
            tmpcmdline = []
            tmpcmdline.extend(
                shlex.split(
                    emerge_config.target_config.settings.get("EMERGE_DEFAULT_OPTS", "")
                )
            )
            # parse defaults like emerge does
            emerge_config.action, emerge_config.opts, emerge_config.args = parse_opts(tmpcmdline)
            self.pvar.settings = emerge_config.target_config.settings
            myopts = emerge_config.opts
        except BaseException as e:
            self.error(ERROR_PACKAGE_FAILED_TO_INSTALL, f"parse_opts exploded: {type(e).__name__}: {e}")

        myopts["--with-bdeps"] = "y"

        getbin = bool(myopts.get("--getbinpkg"))
        getbinonly = bool(myopts.get("--getbinpkgonly"))

        # both flags set : error and stop
        if getbin and getbinonly:
            self.error(ERROR_DEP_RESOLUTION_FAILED, 
                "Conflicting binary package options: both '--getbinpkg' and '--getbinpkgonly' are enabled.\n"
                "The system is configured to both prefer binary packages and require them exclusively.\n"
                "Please disable one of these options in EMERGE_DEFAULT_OPTS")
            return

        root = self.pvar.settings['ROOT']
        bintree = self.pvar.trees[root].get('bintree')

        if getbin:
            myopts["--usepkg"] = True
            myopts.pop("--getbinpkgonly", None)
            if bintree:
                try:
                    bintree.populate(getbinpkgs=True)
                except Exception as e:
                    # not fatal: scheduler will fall back to source if needed
                    self.message(MESSAGE_INFO, f"bintree.populate(getbinpkg) failed: {e}")

        elif getbinonly:
            myopts["--usepkgonly"] = True
            myopts.pop("--getbinpkg", None)
            if bintree:
                try:
                    bintree.populate(getbinpkgs=True, getbinpkgonly=True)
                except Exception as e:
                    # fatal: only binpkgs
                    self.error(ERROR_PACKAGE_FAILED_TO_INSTALL, f"No binary packages available: {e}")
                    return
        else:
            # neither flag
            myopts.pop("--getbinpkg", None)
            myopts.pop("--getbinpkgonly", None)
            myopts.pop("--usepkg", None)
            myopts.pop("--usepkgonly", None)

        if simulate:
            myopts["--pretend"] = True
        if only_download:
            myopts["--fetchonly"] = True
        if reinstall:
            #myopts["--reinstall"] = True
            pass
        else:
            myopts["--selective"] = "y"

        if not downgrade:
            myopts["--ignore-downgrade"] = "y"

        # resolve deps (first attempt)
        self.status(STATUS_DEP_RESOLVE)
        myparams = create_depgraph_params(myopts, "")
        dep = depgraph(self.pvar.settings, self.pvar.trees, myopts, myparams, None)

        retval, favorites = dep.select_files(cpv_list)
        if not retval:
            self.error(ERROR_DEP_RESOLUTION_FAILED, "Wasn't able to get dependency graph")
            return

        altlist = dep.altlist()
        try:
            alt_cpvs = [getattr(x, "cpv", str(x)) for x in altlist]
            self.message(MESSAGE_INFO, f"dep.altlist: {alt_cpvs}")
            self.message(MESSAGE_INFO, f"favorites: {favorites}")
        except Exception:
            pass

        if not altlist:
            if getbinonly:
                self.error(ERROR_DEP_RESOLUTION_FAILED,
                        "No binary candidates found and --getbinpkgonly was requested; aborting.")
                return
            elif getbin:
                try:
                    self.message(MESSAGE_INFO, "No binary candidates found; falling back to source builds.")
                except Exception:
                    pass

                myopts_fallback = dict(myopts)
                myopts_fallback.pop("--usepkg", None)
                myopts_fallback.pop("--usepkgonly", None)

                # re-create depgraph and re-resolve
                myparams_fb = create_depgraph_params(myopts_fallback, "")
                dep_fb = depgraph(self.pvar.settings, self.pvar.trees, myopts_fallback, myparams_fb, None)
                retval2, favorites2 = dep_fb.select_files(cpv_list)
                if not retval2:
                    self.error(ERROR_DEP_RESOLUTION_FAILED, "Wasn't able to get dependency graph (fallback to source).")
                    return

                altlist = dep_fb.altlist()
                try:
                    alt_cpvs = [getattr(x, "cpv", str(x)) for x in altlist]
                    self.message(MESSAGE_INFO, f"dep.altlist (fallback): {alt_cpvs}")
                    self.message(MESSAGE_INFO, f"favorites (fallback): {favorites2}")
                except Exception:
                    pass

                if not altlist:
                    self.error(ERROR_DEP_RESOLUTION_FAILED, "Resolver produced an empty merge list for: " + ", ".join(cpv_list))
                    return

                dep = dep_fb
                favorites = favorites2
            else:
                self.error(ERROR_DEP_RESOLUTION_FAILED, "Resolver produced an empty merge list for: " + ", ".join(cpv_list))
                return

        self.message("MESSAGE_INFO", f"About to merge: {[getattr(x,'cpv',x) for x in altlist]}")

        # fetch restrictions
        self._check_fetch_restrict(altlist)

        self.status(STATUS_INSTALL)
        if simulate:
            return

        # run scheduler
        portage.elog.add_listener(self._elog_listener)
        try:
            self._block_output()
            mergetask = Scheduler(
                self.pvar.settings, self.pvar.trees, self.pvar.mtimedb,
                myopts, None, dep.altlist(), favorites,
                dep.schedulerGraph()
            )
            rval = mergetask.merge()
        finally:
            self._unblock_output()
            portage.elog.remove_listener(self._elog_listener)

        # refresh portage internal state
        try:
            self.pvar.update()
        except Exception:
            self.message("MESSAGE_INFO", "Warning: failed to refresh internal portage state")

        # validate result
        for entry in self._elog_messages:
            try:
                self.message(MESSAGE_INFO, str(entry))
            except Exception:
                pass
        installed_ok = all(self._is_installed(cpv.lstrip('=')) for cpv in cpv_list)
        if rval != os.EX_OK and not installed_ok:
            self._send_merge_error(ERROR_PACKAGE_FAILED_TO_INSTALL)

        self._elog_messages = []
        self._signal_config_update()


    def refresh_cache(self, force):
        # NOTES: can't manage progress even if it could be better
        # TODO: do not wait for exception, check timestamp
        # TODO: message if overlay repo has changed (layman)
        self.status(STATUS_REFRESH_CACHE)
        self.allow_cancel(False)
        self.percentage(None)

        myopts = {'--quiet': True}

        if force:
            timestamp_path = os.path.join(self.pvar.settings["PORTDIR"],
                                          "metadata", "timestamp.chk")
            if os.access(timestamp_path, os.F_OK):
                try:
                    os.remove(timestamp_path)
                except Exception:
                    pass

        try:
            self._block_output()
            action_sync(self.pvar.settings, self.pvar.trees,
                self.pvar.mtimedb, myopts, "")
        except:
            self.error(ERROR_INTERNAL_ERROR, traceback.format_exc())
        finally:
            self._unblock_output()

    def remove_packages(self, transaction_flags, pkgs, allowdep, autoremove):
        return self._remove_packages(transaction_flags, pkgs, allowdep, autoremove)

    def _remove_packages(self, transaction_flags, pkgs, allowdep, autoremove):
        """
        Remove packages using Portage's unmerge API.
        Mirrors what emerge --unmerge does
        """
        #FIXME: Fix removal of system package safety 
        #TODO: Look into dependencies of removed packages
        #FIXME: Problems with removing binpkgs, graph can not be computed correctly in certain cases
        #For now skip and use unmerge with atoms not altlist

        self.status(STATUS_RUNNING)
        self.allow_cancel(False)
        self.percentage(None)

        simulate = self._is_simulate(transaction_flags)

        # collect system packages for safeguard 
        system_packages = [
            atom.cp for atom in InternalPackageSet(
                initial_atoms=self.pvar.root_config.setconfig.getSetAtoms("system")
            )
        ]

        # Build atoms list
        atoms = []
        for pkg in pkgs:
            cpv = self._id_to_cpv(pkg)
            cpv = self._strip_buildid_for_db(cpv)

            if not self._is_cpv_valid(cpv):
                self.error(ERROR_PACKAGE_NOT_FOUND, f"Package {pkg} was not found")
                continue

            installed_cpvs = self.pvar.vardb.match(portage.versions.cpv_getkey(cpv))
            if not installed_cpvs:
                self.error(ERROR_PACKAGE_NOT_FOUND,
                        f"Package {pkg} was not found (no installed cpv)")
                continue

            for inst in installed_cpvs:
                atoms.append("=" + inst)

        if not atoms:
            return

        # load emerge config and parse default opts
        try:
            emerge_config = load_emerge_config()
            tmpcmdline = shlex.split(
                emerge_config.target_config.settings.get("EMERGE_DEFAULT_OPTS", "")
            )
            emerge_config.action, emerge_config.opts, emerge_config.args = parse_opts(tmpcmdline)

            emerge_config.action = "unmerge"
            myopts = dict(emerge_config.opts) if isinstance(emerge_config.opts, dict) else {}
        except Exception as e:
            self.error(ERROR_PACKAGE_FAILED_TO_REMOVE,
                    f"parse_opts exploded: {type(e).__name__}: {e}")
            return

        if simulate:
            myopts["--pretend"] = True

        # resolver
        self.status(STATUS_DEP_RESOLVE)
        myparams = create_depgraph_params(myopts, "unmerge")
        dep = depgraph(self.pvar.settings, self.pvar.trees, myopts, myparams, None)

        retval, favorites = dep.select_files(atoms)
        if not retval:
            #self.message(MESSAGE_INFO, "depgraph failed to resolve removal targets, falling back to direct unmerge")
            altlist = []
        else:
            altlist = dep.altlist()

        if not altlist:
            pass
            #self.message(MESSAGE_INFO, "depgraph returned empty removal list, falling back to direct unmerge")

        try:
            self.message(MESSAGE_INFO, f"About to remove: {[getattr(x,'cpv',str(x)) for x in altlist]}")
        except Exception:
            pass

        self.status(STATUS_REMOVE)
        if simulate:
            return

        portage.elog.add_listener(self._elog_listener)
        try:
            from _emerge.unmerge import unmerge

            result = unmerge(
                self.pvar.root_config,   # root_config is what unmerge expects
                myopts,
                emerge_config.action,   
                atoms,                   
                self.pvar.mtimedb,
                ordered=True,
                raise_on_error=0,
                scheduler=None,
            )

            if isinstance(result, tuple):
                rval = result[0]
            else:
                rval = result
        except Exception as exc:
            self._error_message = f"unmerge() raised: {type(exc).__name__}: {exc}"
            self._error_phase = "unmerge"
            rval = 1
        finally:
            portage.elog.remove_listener(self._elog_listener)

        try:
            self.pvar.update()
        except Exception:
            self.message(MESSAGE_INFO, "Warning: failed to refresh internal portage state")

        # Show collected elog messages
        for entry in self._elog_messages:
            try:
                self.message(MESSAGE_INFO, str(entry))
            except Exception:
                pass

        # Verify removal
        removed_ok = all(not self._is_installed(cpv.lstrip('=')) for cpv in atoms)

        if rval != os.EX_OK or not removed_ok:
            self._send_merge_error(ERROR_PACKAGE_FAILED_TO_REMOVE)

        self._elog_messages = []
        self._signal_config_update()

        try:
            self.pvar.update()
        except Exception:
            self.message("MESSAGE_INFO", "Warning: failed to refresh internal portage state")


    def repo_enable(self, repoid, enable):
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        if repoid == 'gentoo':
            if not enable:
                self.error(ERROR_CANNOT_DISABLE_REPOSITORY,
                           "gentoo repository can't be disabled")
            return

        cmd = ["eselect", "repository", "enable" if enable else "disable", repoid]
        try:
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception as exc:
            self.error(ERROR_INTERNAL_ERROR,
                       "Failed to {action} repository {repoid}: {err}".format(
                           action="enable" if enable else "disable",
                           repoid=repoid,
                           err=str(exc)
                       ))

    def resolve(self, filters, pkgs):
        self.status(STATUS_QUERY)
        self.allow_cancel(True)

        cp_list = self._get_all_cp(filters)
        progress = PackagekitProgress(compute_equal_steps(cp_list))
        self.percentage(progress.percent)

        reg_expr = []
        for pkg in pkgs:
            reg_expr.append("^" + re.escape(pkg) + "$")
        reg_expr = "|".join(reg_expr)

        # specifications says "be case sensitive"
        s = re.compile(reg_expr)

        for percentage, cp in zip(progress, cp_list):
            if s.match(cp):
                cpv_list = self._get_all_cpv(cp, filters, filter_newest=False)

                cpv_list = self._filter_free(cpv_list, filters)

                tmp_filters = list(filters) if not isinstance(filters, list) else filters[:]
                if FILTER_NEWEST not in tmp_filters:
                    tmp_filters.append(FILTER_NEWEST)
                cpv_list = self._filter_newest(cpv_list, tmp_filters)

                if FILTER_NEWEST in filters:
                    cpv_list = cpv_list[:1]

                # suppress per-build-id expansion and emit only latest build id
                self._limit_buildids_to_latest = True
                try:
                    for cpv in cpv_list:
                        try:
                            self._package(cpv)
                        except InvalidAtom:
                            continue
                finally:
                    # remove flag
                    self._limit_buildids_to_latest = False

            self.percentage(percentage)

        self.percentage(100)

    def search_details(self, filters, keys):

        self.status(STATUS_QUERY)
        self.allow_cancel(True)

        cp_list = self._get_all_cp(filters)
        search_list = self._get_search_list(keys)

        progress = PackagekitProgress(compute_equal_steps(cp_list))
        self.percentage(progress.percent)

        portdb = self.pvar.portdb

        # metadata cache locations
        repo_paths = portdb.porttrees

        for percentage, cp in zip(progress, cp_list):
            match = False

            # Split category/package
            try:
                cat, pkg = cp.split("/")
            except ValueError:
                self.percentage(percentage)
                continue

            text = cp

            try:
                for repo in repo_paths:

                    cache_dir = f"{repo}/metadata/md5-cache/{cat}"

                    if not os.path.isdir(cache_dir):
                        continue

                    # find first package cache entry
                    for fname in os.listdir(cache_dir):

                        if not fname.startswith(pkg + "-"): continue

                        path = f"{cache_dir}/{fname}"

                        try:
                            with open(path, "r") as f:

                                for line in f:

                                    if line.startswith("DESCRIPTION="):
                                        text += " " + line[12:].strip()

                                    elif line.startswith("HOMEPAGE="):
                                        text += " " + line[9:].strip()

                                    if len(text) > 500:
                                        break

                        except Exception:
                            continue
                        break

                    if len(text) > len(cp):
                        break

            except Exception:
                self.percentage(percentage)
                continue

            #search
            for s in search_list:
                if not s.search(text):
                    break
            else:
                match = True

            #expand match
            if match:
                cpv_all = self._get_all_cpv(cp, filters, filter_newest=False)
                cpv_list = self._filter_newest(cpv_all, filters)

                for cpv in cpv_list:

                    try:
                        self._package(cpv)
                    except InvalidAtom:
                        continue

            self.percentage(percentage)

        self.percentage(100)

    def search_file(self, filters, values):
        # FILTERS:
        # - ~installed is not accepted (error)
        # - free: ok
        # - newest: as only installed, by himself
        self.status(STATUS_QUERY)
        self.allow_cancel(True)

        if FILTER_NOT_INSTALLED in filters:
            self.error(ERROR_CANNOT_GET_FILELIST,
                       "search-file isn't available with ~installed filter")
            return

        cpv_list = self.pvar.vardb.cpv_all()
        is_full_path = True

        progress = PackagekitProgress(compute_equal_steps(values))
        self.percentage(progress.percent)

        for percentage, key in zip(progress, values):

            if key[0] != "/":
                is_full_path = False
                key = re.escape(key)
                searchre = re.compile("/" + key + "$", re.IGNORECASE)

            # free filter
            cpv_list = self._filter_free(cpv_list, filters)
            nb_cpv = float(len(cpv_list))

            for cpv in cpv_list:
                for f in self._get_file_list(cpv):
                    if (is_full_path and key == f) \
                            or (not is_full_path and searchre.search(f)):
                        self._package(cpv)
                        break

            self.percentage(percentage)

        self.percentage(100)

    def search_group(self, filters, groups):
        # TODO: filter unknown groups before searching ? (optimization)
        self.status(STATUS_QUERY)
        self.allow_cancel(True)

        cp_list = self._get_all_cp(filters)

        progress = PackagekitProgress(compute_equal_steps(cp_list))
        self.percentage(progress.percent)

        for percentage, cp in zip(progress, cp_list):
            for group in groups:
                if self._get_pk_group(cp) == group:
                    for cpv in self._get_all_cpv(cp, filters):
                        self._package(cpv)

            self.percentage(percentage)

        self.percentage(100)

    def search_name(self, filters, keys_list):
        # searching for all keys in package name
        # also filtering by categories if categery is specified in a key
        # keys contain more than one category name, no results can be found
        self.status(STATUS_QUERY)
        self.allow_cancel(True)

        categories = []
        for k in keys_list[:]:
            if "/" in k:
                cat, cp = portage.versions.catsplit(k)
                categories.append(cat)
                keys_list[keys_list.index(k)] = cp

        category_filter = None
        if len(categories) > 1:
            return
        elif len(categories) == 1:
            category_filter = categories[0]

        # do not use self._get_search_list because of this category feature
        search_list = []
        for k in keys_list:
            # not done entirely by pk-transaction
            k = re.escape(k)
            search_list.append(re.compile(k, re.IGNORECASE))

        cp_list = self._get_all_cp(filters)

        progress = PackagekitProgress(compute_equal_steps(cp_list))
        self.percentage(progress.percent)

        for percentage, cp in zip(progress, cp_list):
            if category_filter:
                cat, pkg_name = portage.versions.catsplit(cp)
                if cat != category_filter:
                    continue
            else:
                pkg_name = portage.versions.catsplit(cp)[1]
            found = True

            # pkg name has to correspond to _every_ keys
            for s in search_list:
                if not s.search(pkg_name):
                    found = False
                    break
            if found:
                for cpv in self._get_all_cpv(cp, filters):
                    self._package(cpv)

            self.percentage(percentage)

        self.percentage(100)

    def update_packages(self, transaction_flags, pkgs):

        only_trusted = self._is_only_trusted(transaction_flags)
        simulate = self._is_simulate(transaction_flags)
        only_download = self._is_only_download(transaction_flags)

        return self._update_packages(only_trusted, pkgs, simulate=simulate,
                                     only_download=only_download)

    def _update_packages(self, only_trusted, pkgs, simulate=False,
                         only_download=False):
        # TODO: manage errors
        # TODO: manage config file updates
        # TODO: every updated pkg should emit self.package()
        #       see around _emerge.Scheduler.Scheduler
        # TODO: return only the latest binpkg

        self.status(STATUS_RUNNING)
        self.allow_cancel(False)
        self.percentage(None)

        cpv_list = []
        for pkg in pkgs:
            cpv = self._id_to_cpv(pkg)
            if not self._is_cpv_valid(cpv):
                self.error(ERROR_UPDATE_NOT_FOUND, f"Package {pkg} not found")
                continue
            cpv_list.append("=" + cpv)

        if not cpv_list:
            return

        if only_trusted:
            pass

        myopts = {}

        try:
            emerge_config = load_emerge_config()
            tmpcmdline = []
            tmpcmdline.extend(
                shlex.split(
                    emerge_config.target_config.settings.get("EMERGE_DEFAULT_OPTS", "")
                )
            )
            emerge_config.action, emerge_config.opts, emerge_config.args = parse_opts(tmpcmdline)
            self.pvar.settings = emerge_config.target_config.settings
            myopts = emerge_config.opts
        except BaseException as e:
            self.error(ERROR_PACKAGE_FAILED_TO_INSTALL, f"parse_opts exploded: {type(e).__name__}: {e}")

        myopts["--with-bdeps"] = "y"

        getbin = bool(myopts.get("--getbinpkg"))
        getbinonly = bool(myopts.get("--getbinpkgonly"))

        if getbin and getbinonly:
            self.error(ERROR_DEP_RESOLUTION_FAILED, 
                "Conflicting binary package options: both '--getbinpkg' and '--getbinpkgonly' are enabled.\n"
                "The system is configured to both prefer binary packages and require them exclusively.\n"
                "Please disable one of these options in EMERGE_DEFAULT_OPTS")
            return

        root = self.pvar.settings['ROOT']
        bintree = self.pvar.trees[root].get('bintree')

        if getbin:
            myopts["--usepkg"] = True
            myopts.pop("--getbinpkgonly", None)
            if bintree:
                try:
                    bintree.populate(getbinpkgs=True)
                except Exception as e:
                    self.message(MESSAGE_INFO, f"bintree.populate(getbinpkg) failed: {e}")

        elif getbinonly:
            myopts["--usepkgonly"] = True
            myopts.pop("--getbinpkg", None)
            if bintree:
                try:
                    bintree.populate(getbinpkgs=True, getbinpkgonly=True)
                except Exception as e:
                    self.error(ERROR_PACKAGE_FAILED_TO_INSTALL, f"No binary packages available: {e}")
                    return
        else:
            myopts.pop("--getbinpkg", None)
            myopts.pop("--getbinpkgonly", None)
            myopts.pop("--usepkg", None)
            myopts.pop("--usepkgonly", None)

        if simulate:
            myopts["--pretend"] = True
        if only_download:
            myopts["--fetchonly"] = True
        else:
            myopts["--selective"] = "y"

        self.status(STATUS_DEP_RESOLVE)
        myparams = create_depgraph_params(myopts, "")
        dep = depgraph(self.pvar.settings, self.pvar.trees, myopts, myparams, None)

        retval, favorites = dep.select_files(cpv_list)
        if not retval:
            self.error(ERROR_DEP_RESOLUTION_FAILED, "Wasn't able to get dependency graph")
            return

        altlist = dep.altlist()
        try:
            alt_cpvs = [getattr(x, "cpv", str(x)) for x in altlist]
            self.message(MESSAGE_INFO, f"dep.altlist: {alt_cpvs}")
            self.message(MESSAGE_INFO, f"favorites: {favorites}")
        except Exception:
            pass

        if not altlist:
            if getbinonly:
                self.error(ERROR_DEP_RESOLUTION_FAILED,
                        "No binary candidates found and --getbinpkgonly was requested; aborting.")
                return
            elif getbin:
                try:
                    self.message(MESSAGE_INFO, "No binary candidates found; falling back to source builds.")
                except Exception:
                    pass

                myopts_fallback = dict(myopts)
                myopts_fallback.pop("--usepkg", None)
                myopts_fallback.pop("--usepkgonly", None)

                myparams_fb = create_depgraph_params(myopts_fallback, "")
                dep_fb = depgraph(self.pvar.settings, self.pvar.trees, myopts_fallback, myparams_fb, None)
                retval2, favorites2 = dep_fb.select_files(cpv_list)
                if not retval2:
                    self.error(ERROR_DEP_RESOLUTION_FAILED, "Wasn't able to get dependency graph (fallback to source).")
                    return

                altlist = dep_fb.altlist()
                try:
                    alt_cpvs = [getattr(x, "cpv", str(x)) for x in altlist]
                    self.message(MESSAGE_INFO, f"dep.altlist (fallback): {alt_cpvs}")
                    self.message(MESSAGE_INFO, f"favorites (fallback): {favorites2}")
                except Exception:
                    pass

                if not altlist:
                    self.error(ERROR_DEP_RESOLUTION_FAILED, "Resolver produced an empty merge list for: " + ", ".join(cpv_list))
                    return

                dep = dep_fb
                favorites = favorites2
            else:
                self.error(ERROR_DEP_RESOLUTION_FAILED, "Resolver produced an empty merge list for: " + ", ".join(cpv_list))
                return

        self.message("MESSAGE_INFO", f"About to merge: {[getattr(x,'cpv',x) for x in altlist]}")

        self._check_fetch_restrict(altlist)

        self.status(STATUS_INSTALL)
        if simulate:
            return

        portage.elog.add_listener(self._elog_listener)
        try:
            self._block_output()
            mergetask = Scheduler(
                self.pvar.settings, self.pvar.trees, self.pvar.mtimedb,
                myopts, None, dep.altlist(), favorites,
                dep.schedulerGraph()
            )
            rval = mergetask.merge()
        finally:
            self._unblock_output()
            portage.elog.remove_listener(self._elog_listener)

        try:
            self.pvar.update()
        except Exception:
            self.message("MESSAGE_INFO", "Warning: failed to refresh internal portage state")

        for entry in self._elog_messages:
            try:
                self.message(MESSAGE_INFO, str(entry))
            except Exception:
                pass
        installed_ok = all(self._is_installed(cpv.lstrip('=')) for cpv in cpv_list)
        if rval != os.EX_OK and not installed_ok:
            self._send_merge_error(ERROR_PACKAGE_FAILED_TO_INSTALL)

        self._elog_messages = []
        self._signal_config_update()

def main():
    backend = PackageKitPortageBackend("")
    backend.dispatcher(sys.argv[1:])

if __name__ == "__main__":
    main()
