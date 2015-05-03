#!/usr/bin/python2
# -*- coding: utf-8 -*-
# vim:set shiftwidth=4 tabstop=4 expandtab:
#
# Copyright (C) 2009 Mounir Lamouri (volkmar) <mounir.lamouri@gmail.com>
# Copyright (C) 2010-2013 Fabio Erculiani (lxnay) <lxnay@gentoo.org>
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
from itertools import izip

# layman imports (>=2)
import layman.config
import layman.db
import layman.remotedb
# packagekit imports
from packagekit.backend import (
    PackageKitBaseBackend,
    get_package_id,
    split_package_id,
)
from packagekit.enums import *
from packagekit.package import PackagekitPackage
from packagekit.progress import *
# portage imports
import _emerge.AtomArg
import _emerge.actions
import _emerge.create_depgraph_params
import _emerge.stdout_spinner
import portage
import portage.dep
import portage.versions
from portage._sets.base import InternalPackageSet
from portage.exception import InvalidAtom

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
        self.root_config = None

        self.update()

    def update(self):
        self.settings, self.trees, self.mtimedb = \
            _emerge.actions.load_emerge_config()
        self.vardb = self.trees[self.settings['ROOT']]['vartree'].dbapi
        self.portdb = self.trees[self.settings['ROOT']]['porttree'].dbapi
        self.root_config = self.trees[self.settings['ROOT']]['root_config']

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

    # TODO: should be removed when using non-verbose function API
    def _block_output(self):
        sys.stdout = self._dev_null
        sys.stderr = self._dev_null

    # TODO: should be removed when using non-verbose function API
    def _unblock_output(self):
        sys.stdout = sys.__stdout__
        sys.stderr = sys.__stderr__

    def _is_only_trusted(self, transaction_flags):
        return (TRANSACTION_FLAG_ONLY_TRUSTED in transaction_flags) or (
            TRANSACTION_FLAG_SIMULATE in transaction_flags)

    def _is_simulate(self, transaction_flags):
        return TRANSACTION_FLAG_SIMULATE in transaction_flags

    def _is_only_download(self, transaction_flags):
        return TRANSACTION_FLAG_ONLY_DOWNLOAD in transaction_flags

    def _is_repo_enabled(self, layman_db, repo_name):
        return repo_name in layman_db.overlays.keys()

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
                    ' '.join([x.strip() for x in \
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
        return self.pvar.vardb.cpv_exists(cpv)

    def _is_cpv_valid(self, cpv):
        return any([self._is_installed(cpv), self.pvar.portdb.cpv_exists(cpv)])

    def _get_real_license_str(self, cpv, metadata):
        # use conditionals info (w/ USE) in LICENSE and remove ||
        ebuild_settings = self._get_ebuild_settings(cpv, metadata)
        license = set(portage.flatten(
            portage.dep.use_reduce(
                portage.dep.paren_reduce(metadata["LICENSE"]),
                uselist=ebuild_settings.get("USE", "").split()
            )
        ))
        license.discard('||')
        return ' '.join(license)

    def _signal_config_update(self):
        result = list(portage.util.find_updated_config_files(
            self.pvar.settings['ROOT'],
            self.pvar.settings.get('CONFIG_PROTECT', '').split()))

        if result:
            self.message(
                MESSAGE_CONFIG_FILES_CHANGED,
                "Some configuration files need updating."
                ";You should use Gentoo's tools to update them (dispatch-conf)"
                ";If you can't do that, ask your system administrator."
            )

    def _get_restricted_fetch_files(self, cpv, metadata):
        '''
        This function checks files in SRC_URI and look if they are in DESTDIR.
        Missing files are returned. If there is no issue, None is returned.
        We don't care about digest but only about existance of files.

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

    def _send_merge_error(self, default):
        # EAPI-2 compliant (at least)
        # 'other' phase is ignored except this one, every phase should be there
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
        db = self.pvar.vardb if self._is_installed(cpv) else self.pvar.portdb

        if add_cache_keys:
            keys.extend(list(db._aux_cache_keys))

        if in_dict:
            return dict(izip(keys, db.aux_get(cpv, keys)))
        else:
            return db.aux_get(cpv, keys)

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
            metadata = self._get_metadata(cpv, ["IUSE", "SLOT"], in_dict=True)

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
            size = sum(fetch_file)

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
            metadata = self._get_metadata(cpv, ["LICENSE", "USE", "SLOT"], True)
            return not self.pvar.settings._getMissingLicenses(cpv, metadata)

        if FILTER_FREE in filters or FILTER_NOT_FREE in filters:
            free_licenses = "@FSF-APPROVED"
            if FILTER_FREE in filters:
                licenses = "-* " + free_licenses
            elif FILTER_NOT_FREE in filters:
                licenses = "* -" + free_licenses
            backup_license = self.pvar.settings["ACCEPT_LICENSE"]

            self.pvar.apply_settings({'ACCEPT_LICENSE': licences})
            cpv_list = filter(_has_validLicense, cpv_list)
            self.pvar.apply_settings({'ACCEPT_LICENSE': backup_licence})

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
        slots = cpv_dict.keys()
        slots.reverse()

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

        return cp_list

    def _get_all_cpv(self, cp, filters, filter_newest=True):
        # NOTES:
        # returns a list of cpv
        #
        # FILTERS:
        # - installed: ok
        # - free: ok
        # - newest: ok

        cpv_list = []

        # populate cpv_list taking care of installed filter
        if FILTER_INSTALLED in filters:
            cpv_list = self.pvar.vardb.match(cp)
        elif FILTER_NOT_INSTALLED in filters:
            cpv_list = [cpv for cpv in self.pvar.portdb.match(cp)
                        if not self._is_installed(cpv)]
        else:
            cpv_list = self.pvar.vardb.match(cp)
            cpv_list.extend(self.pvar.portdb.match(cp))
            cpv_list = set(cpv_list)

        # free filter
        cpv_list = self._filter_free(cpv_list, filters)

        # newest filter
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

        # remove slot info from version field
        version = ret[1].split(':')[0]

        return ret[0] + "-" + version

    def _cpv_to_id(self, cpv):
        '''
        Transform the cpv (portage) to a package id (packagekit)
        '''
        package, version, rev = portage.versions.pkgsplit(cpv)
        pkg_keywords, repo, slot = self._get_metadata(
            cpv, ["KEYWORDS", "repository", "SLOT"]
        )

        # filter accepted keywords
        keywords = list(set(pkg_keywords.split()).intersection(
            set(self.pvar.settings["ACCEPT_KEYWORDS"].split())
        ))

        # if no keywords, check in package.keywords
        if not keywords:
            key_dict = self.pvar.settings.pkeywordsdict.get(
                portage.dep.dep_getkey(cpv)
            )
            if key_dict:
                for keys in key_dict.values():
                    keyword.extend(keys)

        if not keywords:
            keywords.append("no keywords")
            self.message(MESSAGE_UNKNOWN,
                         "No keywords have been found for %s" % cpv)

        # don't want to see -r0
        if rev != "r0":
            version = version + "-" + rev
        # add slot info if slot != 0
        if slot != '0':
            version = version + ':' + slot

        # if installed, repo should be 'installed', packagekit rule
        if self._is_installed(cpv):
            repo = "installed"

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
        def filter_cpv_input(x): return x.cpv not in cpv_input
        return filter(filter_cpv_input, packages_list)


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
        self.package(self._cpv_to_id(cpv), info, desc)

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

            cat_id = name # same thing
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
        cpv_list = filter(_filter_uninstall, cpv_list)

        # install filter
        if FILTER_INSTALLED in filters:
            cpv_list = filter(_filter_installed, cpv_list)
        if FILTER_NOT_INSTALLED in filters:
            cpv_list = filter(_filter_not_installed, cpv_list)

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
        self.percentage(0)

        nb_pkg = float(len(pkgs))
        pkg_processed = 0.0

        for pkg in pkgs:
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

            pkg_processed += 100.0
            self.percentage(int(pkg_processed/nb_pkg))

        self.percentage(100)

    def get_files(self, pkgs):
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(0)

        nb_pkg = float(len(pkgs))
        pkg_processed = 0.0

        for pkg in pkgs:
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

            pkg_processed += 100.0
            self.percentage(int(pkg_processed/nb_pkg))

        self.percentage(100)

    def get_packages(self, filters):
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(0)

        cp_list = self._get_all_cp(filters)
        nb_cp = float(len(cp_list))
        cp_processed = 0.0

        for cp in self._get_all_cp(filters):
            for cpv in self._get_all_cpv(cp, filters):
                try:
                    self._package(cpv)
                except InvalidAtom:
                    continue

            cp_processed += 100.0
            self.percentage(int(cp_processed/nb_cp))

        self.percentage(100)

    def get_repo_list(self, filters):
        """ Get list of repository.

        Get the list of repository tagged as official and supported by current
        setup of layman.

        Adds a dummy entry for gentoo-x86 official tree even though it appears
        in layman's listing nowadays.
        """
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        conf = layman.config.BareConfig()
        conf.set_option('quiet', True)
        installed_layman_db = layman.db.DB(conf)
        available_layman_db = layman.remotedb.RemoteDB(conf)

        # 'gentoo' is a dummy repo
        self.repo_detail('gentoo', 'Gentoo Portage tree', True)

        if FILTER_NOT_DEVELOPMENT not in filters:
            for repo_name, overlay in available_layman_db.overlays.items():
                if overlay.is_official() and overlay.is_supported():
                    self.repo_detail(
                        repo_name,
                        overlay.name,
                        self._is_repo_enabled(installed_layman_db, repo_name)
                    )

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

        # now we can populate cpv_list
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

            if not self.pvar.portdb.cpv_exists(cpv):
                self.message(MESSAGE_COULD_NOT_FIND_PACKAGE,
                             "could not find %s" % pkg)

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

        # check if a candidate can be updated
        for cp in update_candidates:
            cpv_list_inst = self.pvar.vardb.match(cp)
            cpv_list_avai = self.pvar.portdb.match(cp)

            cpv_dict_inst = self._get_cpv_slotted(cpv_list_inst)
            cpv_dict_avai = self._get_cpv_slotted(cpv_list_avai)

            dict_upda = {}
            dict_down = {}

            # candidate slots are installed slots
            slots = cpv_dict_inst.keys()
            slots.reverse()

            for s in slots:
                cpv_list_updates = []
                cpv_inst = cpv_dict_inst[s][0] # only one install per slot

                # the slot can be outdated (not in the tree)
                if s not in cpv_dict_avai:
                    break

                tmp_list_avai = cpv_dict_avai[s]
                tmp_list_avai.reverse()

                for cpv in tmp_list_avai:
                    if self._cmp_cpv(cpv_inst, cpv) == -1:
                        cpv_list_updates.append(cpv)
                    else: # because the list is sorted
                        break

                # no update for this slot
                if len(cpv_list_updates) == 0:
                    if [cpv_inst] == self.pvar.portdb.visible([cpv_inst]):
                        break # really no update
                    else:
                        # that's actually a downgrade or even worst
                        if len(tmp_list_avai) == 0:
                            break # this package is not known in the tree...
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
            else: # update also non-world and non-system packages if security
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

        return self._install_packages(only_trusted, pkgs, simulate=simulate,
                                      only_download=only_download)

    def _install_packages(self, only_trusted, pkgs, simulate=False,
                          only_download=False):
        # NOTES:
        # can't install an already installed packages
        # even if it happens to be needed in Gentoo but probably not this API
        # TODO: every merged pkg should emit self.package()
        #       see around _emerge.Scheduler.Scheduler

        self.status(STATUS_RUNNING)
        self.allow_cancel(False)
        self.percentage(None)

        cpv_list = []

        for pkg in pkgs:
            cpv = self._id_to_cpv(pkg)

            if not self._is_cpv_valid(cpv):
                self.error(ERROR_PACKAGE_NOT_FOUND,
                           "Package %s was not found" % pkg)
                continue

            if self._is_installed(cpv):
                self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                           "Package %s is already installed" % pkg)
                continue

            cpv_list.append('=' + cpv)

        # only_trusted isn't supported
        # but better to show it after important errors
        if only_trusted:
            self.error(ERROR_MISSING_GPG_SIGNATURE,
                       "Portage backend does not support GPG signature")
            return

        # creating installation depgraph
        myopts = {}
        if only_download:
            myopts['--fetchonly'] = True

        favorites = []
        myparams = _emerge.create_depgraph_params \
            .create_depgraph_params(myopts, "")

        self.status(STATUS_DEP_RESOLVE)

        depgraph = _emerge.depgraph.depgraph(self.pvar.settings,
                                             self.pvar.trees, myopts,
                                             myparams, None)
        retval, favorites = depgraph.select_files(cpv_list)
        if not retval:
            self.error(ERROR_DEP_RESOLUTION_FAILED,
                       "Wasn't able to get dependency graph")
            return

        # check fetch restrict, can stop the function via error signal
        self._check_fetch_restrict(depgraph.altlist())

        self.status(STATUS_INSTALL)

        if simulate:
            return

        # get elog messages
        portage.elog.add_listener(self._elog_listener)

        try:
            self._block_output()
            # compiling/installing
            mergetask = _emerge.Scheduler.Scheduler(
                self.pvar.settings, self.pvar.trees, self.pvar.mtimedb,
                myopts, None, depgraph.altlist(), favorites,
                depgraph.schedulerGraph()
            )
            rval = mergetask.merge()
        finally:
            self._unblock_output()

        # when an error is found print error messages
        if rval != os.EX_OK:
            self._send_merge_error(ERROR_PACKAGE_FAILED_TO_INSTALL)

        # show elog messages and clean
        portage.elog.remove_listener(self._elog_listener)
        for msg in self._elog_messages:
            # TODO: use specific message ?
            self.message(MESSAGE_UNKNOWN, msg)
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

        conf = layman.config.BareConfig()
        conf.set_option('quiet', True)
        installed_layman_db = layman.db.DB(conf)

        if force:
            timestamp_path = os.path.join(self.pvar.settings["PORTDIR"],
                                          "metadata", "timestamp.chk")
            if os.access(timestamp_path, os.F_OK):
                os.remove(timestamp_path)

        try:
            self._block_output()
            for overlay in installed_layman_db.overlays.keys():
                installed_layman_db.sync(overlay)
            _emerge.actions.action_sync(self.pvar.settings, self.pvar.trees,
                                        self.pvar.mtimedb, myopts, "")
        except:
            self.error(ERROR_INTERNAL_ERROR, traceback.format_exc())
        finally:
            self._unblock_output()

    def remove_packages(self, allowdep, autoremove, pkgs):
        return self._remove_packages(allowdep, autoremove, pkgs)

    def _remove_packages(self, allowdep, autoremove, pkgs, simulate=False):
        # TODO: every to-be-removed pkg should emit self.package()
        #       see around _emerge.Scheduler.Scheduler
        self.status(STATUS_RUNNING)
        self.allow_cancel(False)
        self.percentage(None)

        cpv_list = []
        packages = []
        required_packages = []
        system_packages = []

        # get system packages
        for atom in InternalPackageSet(initial_atoms=self.pvar.root_config
                                       .setconfig.getSetAtoms("system")):
            system_packages.append(atom.cp)

        # create cpv_list
        for pkg in pkgs:
            cpv = self._id_to_cpv(pkg)

            if not self._is_cpv_valid(cpv):
                self.error(ERROR_PACKAGE_NOT_FOUND,
                           "Package %s was not found" % pkg)
                continue

            if not self._is_installed(cpv):
                self.error(ERROR_PACKAGE_NOT_INSTALLED,
                           "Package %s is not installed" % pkg)
                continue

            # stop removal if a package is in the system set
            if portage.versions.pkgsplit(cpv)[0] in system_packages:
                self.error(
                    ERROR_CANNOT_REMOVE_SYSTEM_PACKAGE,
                    "Package %s is a system package. "
                    "If you really want to remove it, please use portage" %
                    pkg
                )
                continue

            cpv_list.append(cpv)

        # backend do not implement autoremove
        if autoremove:
            self.message(MESSAGE_AUTOREMOVE_IGNORED,
                         "Portage backend do not implement autoremove option")

        # get packages needing candidates for removal
        required_packages = self._get_required_packages(cpv_list,
                                                        recursive=True)

        # if there are required packages, allowdep must be on
        if required_packages and not allowdep:
            self.error(ERROR_DEP_RESOLUTION_FAILED,
                       "Could not perform remove operation has packages "
                       "are needed by other packages")
            return

        # first, we add required packages
        for p in required_packages:
            package = _emerge.Package.Package(
                type_name=p.type_name,
                built=p.built,
                installed=p.installed,
                root_config=p.root_config,
                cpv=p.cpv,
                metadata=p.metadata,
                operation='uninstall'
            )
            packages.append(package)

        # and now, packages we want really to remove
        for cpv in cpv_list:
            metadata = self._get_metadata(cpv, [],
                                          in_dict=True, add_cache_keys=True)
            package = _emerge.Package.Package(
                type_name="ebuild",
                built=True,
                installed=True,
                root_config=self.pvar.root_config,
                cpv=cpv,
                metadata=metadata,
                operation="uninstall"
            )
            packages.append(package)

        if simulate:
            return

        # need to define favorites to remove packages from world set
        favorites = []
        for p in packages:
            favorites.append('=' + p.cpv)

        # get elog messages
        portage.elog.add_listener(self._elog_listener)

        # now, we can remove
        try:
            self._block_output()
            mergetask = _emerge.Scheduler.Scheduler(
                self.pvar.settings, self.pvar.trees, self.pvar.mtimedb,
                mergelist=packages, myopts={}, spinner=None,
                favorites=favorites, digraph=None
            )
            rval = mergetask.merge()
        finally:
            self._unblock_output()

        # when an error is found print error messages
        if rval != os.EX_OK:
            self._send_merge_error(ERROR_PACKAGE_FAILED_TO_REMOVE)

        # show elog messages and clean
        portage.elog.remove_listener(self._elog_listener)
        for msg in self._elog_messages:
            # TODO: use specific message ?
            self.message(MESSAGE_UNKNOWN, msg)
        self._elog_messages = []

    def repo_enable(self, repoid, enable):
        # NOTES: use layman API >= 1.2.3
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        # special case: trying to work with gentoo repo
        if repoid == 'gentoo':
            if not enable:
                self.error(ERROR_CANNOT_DISABLE_REPOSITORY,
                           "gentoo repository can't be disabled")
            return

        conf = layman.config.BareConfig()
        conf.set_option('quiet', True)
        installed_layman_db = layman.db.DB(conf)
        available_layman_db = layman.remotedb.RemoteDB(conf)

        # check now for repoid so we don't have to do it after
        if repoid not in available_layman_db.overlays.keys():
            self.error(ERROR_REPO_NOT_FOUND,
                       "Repository %s was not found" % repoid)
            return

        # disabling (removing) a db
        # if repository already disabled, ignoring
        if not enable and self._is_repo_enabled(installed_layman_db, repoid):
            try:
                installed_layman_db.delete(installed_layman_db.select(repoid))
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR,
                           "Failed to disable repository "+repoid+" : "+str(e))
                return

        # enabling (adding) a db
        # if repository already enabled, ignoring
        if enable and not self._is_repo_enabled(installed_layman_db, repoid):
            try:
                # TODO: clean the trick to prevent outputs from layman
                self._block_output()
                installed_layman_db.add(available_layman_db.select(repoid))
                self._unblock_output()
            except Exception, e:
                self._unblock_output()
                self.error(ERROR_INTERNAL_ERROR,
                           "Failed to enable repository "+repoid+" : "+str(e))
                return

    def resolve(self, filters, pkgs):
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(0)

        cp_list = self._get_all_cp(filters)
        nb_cp = float(len(cp_list))
        cp_processed = 0.0

        reg_expr = []
        for pkg in pkgs:
            reg_expr.append("^" + re.escape(pkg) + "$")
        reg_expr = "|".join(reg_expr)

        # specifications says "be case sensitive"
        s = re.compile(reg_expr)

        for cp in cp_list:
            if s.match(cp):
                for cpv in self._get_all_cpv(cp, filters):
                    self._package(cpv)

            cp_processed += 100.0
            self.percentage(int(cp_processed/nb_cp))

        self.percentage(100)

    def search_details(self, filters, keys):
        # NOTES: very bad performance
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(0)

        cp_list = self._get_all_cp(filters)
        nb_cp = float(len(cp_list))
        cp_processed = 0.0
        search_list = self._get_search_list(keys)

        for cp in cp_list:
            # unfortunatelly, everything is related to cpv, not cp
            # can't filter cp
            cpv_list = []

            # newest filter can't be executed now
            # because some cpv are going to be filtered by search conditions
            # and newest filter could be alterated
            for cpv in self._get_all_cpv(cp, filters, filter_newest=False):
                match = True
                metadata = self._get_metadata(
                    cpv, ["DESCRIPTION", "HOMEPAGE", "IUSE", "LICENSE",
                          "repository", "SLOT", "EAPI", "KEYWORDS"],
                    in_dict=True
                )
                # update LICENSE to correspond to system settings
                metadata["LICENSE"] = self._get_real_license_str(cpv, metadata)
                for s in search_list:
                    found = False
                    for x in metadata:
                        if s.search(metadata[x]):
                            found = True
                            break
                    if not found:
                        match = False
                        break
                if match:
                    cpv_list.append(cpv)

            # newest filter
            cpv_list = self._filter_newest(cpv_list, filters)

            for cpv in cpv_list:
                self._package(cpv)

            cp_processed += 100.0
            self.percentage(int(cp_processed/nb_cp))

        self.percentage(100)

    def search_file(self, filters, values):
        # FILTERS:
        # - ~installed is not accepted (error)
        # - free: ok
        # - newest: as only installed, by himself
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(0)

        if FILTER_NOT_INSTALLED in filters:
            self.error(ERROR_CANNOT_GET_FILELIST,
                       "search-file isn't available with ~installed filter")
            return

        cpv_list = self.pvar.vardb.cpv_all()
        nb_cpv = 0.0
        cpv_processed = 0.0
        is_full_path = True

        count = 0
        values_len = len(values)
        for key in values:

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

            count += 1
            self.percentage(float(count)/values_len)

        self.percentage(100)

    def search_group(self, filters, groups):
        # TODO: filter unknown groups before searching ? (optimization)
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(0)

        cp_list = self._get_all_cp(filters)
        nb_cp = float(len(cp_list))
        cp_processed = 0.0

        for cp in cp_list:
            for group in groups:
                if self._get_pk_group(cp) == group:
                    for cpv in self._get_all_cpv(cp, filters):
                        self._package(cpv)

            cp_processed += 100.0
            self.percentage(int(cp_processed/nb_cp))

        self.percentage(100)

    def search_name(self, filters, keys_list):
        # searching for all keys in package name
        # also filtering by categories if categery is specified in a key
        # keys contain more than one category name, no results can be found
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(0)

        categories = []
        for k in keys_list[:]:
            if "/" in k:
                cat, cp = portage.versions.catsplit(k)
                categories.append(cat)
                keys_list[keys_list.index(k)] = cp

        category_filter = None
        if len(categories) > 1:
            # nothing will be found because we have two cat/pkg
            # with a AND operator search
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
        nb_cp = float(len(cp_list))
        cp_processed = 0.0

        for cp in cp_list:
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

            cp_processed += 100.0
            self.percentage(int(cp_processed/nb_cp))

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

        self.status(STATUS_RUNNING)
        self.allow_cancel(False)
        self.percentage(None)

        cpv_list = []

        for pkg in pkgs:
            cpv = self._id_to_cpv(pkg)

            if not self._is_cpv_valid(cpv):
                self.error(ERROR_UPDATE_NOT_FOUND,
                           "Package %s was not found" % pkg)
                continue

            cpv_list.append('=' + cpv)

        # only_trusted isn't supported
        # but better to show it after important errors
        if only_trusted:
            self.error(ERROR_MISSING_GPG_SIGNATURE,
                       "Portage backend does not support GPG signature")
            return

        # creating update depgraph
        myopts = {}
        if only_download:
            myopts['--fetchonly'] = True
        favorites = []
        myparams = _emerge.create_depgraph_params \
            .create_depgraph_params(myopts, "")

        self.status(STATUS_DEP_RESOLVE)

        depgraph = _emerge.depgraph.depgraph(self.pvar.settings,
                                             self.pvar.trees, myopts,
                                             myparams, None)
        retval, favorites = depgraph.select_files(cpv_list)
        if not retval:
            self.error(ERROR_DEP_RESOLUTION_FAILED,
                       "Wasn't able to get dependency graph")
            return

        # check fetch restrict, can stop the function via error signal
        self._check_fetch_restrict(depgraph.altlist())

        self.status(STATUS_INSTALL)

        if simulate:
            return

        # get elog messages
        portage.elog.add_listener(self._elog_listener)

        try:
            self._block_output()
            # compiling/installing
            mergetask = _emerge.Scheduler.Scheduler(
                self.pvar.settings, self.pvar.trees, self.pvar.mtimedb,
                myopts, None, depgraph.altlist(), favorites,
                depgraph.schedulerGraph()
            )
            rval = mergetask.merge()
        finally:
            self._unblock_output()

        # when an error is found print error messages
        if rval != os.EX_OK:
            self._send_merge_error(ERROR_PACKAGE_FAILED_TO_INSTALL)

        # show elog messages and clean
        portage.elog.remove_listener(self._elog_listener)
        for msg in self._elog_messages:
            # TODO: use specific message ?
            self.message(MESSAGE_UNKNOWN, msg)
        self._elog_messages = []

        self._signal_config_update()


def main():
    backend = PackageKitPortageBackend("")
    backend.dispatcher(sys.argv[1:])

if __name__ == "__main__":
    main()
