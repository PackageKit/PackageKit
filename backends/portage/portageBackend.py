#!/usr/bin/python
# vim:set shiftwidth=4 tabstop=4 expandtab:
#
# Copyright (C) 2009 Mounir Lamouri (volkmar) <mounir.lamouri@gmail.com>
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

# packagekit imports
from packagekit.backend import *
from packagekit.progress import *
from packagekit.package import PackagekitPackage

# portage imports
# TODO: why some python app are adding try / catch around this ?
import portage
import _emerge

# layman imports
import layman.db
import layman.config

# misc imports
import sys
import signal
import re
from itertools import izip

# NOTES:
#
# Package IDs description:
# CAT/PN;PV;KEYWORD;[REPOSITORY|installed]
# Last field must be "installed" if installed. Otherwise it's the repo name
# TODO: KEYWORD ? (arch or ~arch) with update, it will work ?
#
# Naming convention:
# cpv: category package version, the standard representation of what packagekit
#   names a package (an ebuild for portage)

# TODO:
# ERRORS with messages ?
# use get_metadata instead of aux_get
# manage slots

# Map Gentoo categories to the PackageKit group name space
SECTION_GROUP_MAP = {
        "app-accessibility" : GROUP_ACCESSIBILITY,
        "app-admin" : GROUP_ADMIN_TOOLS,
        "app-antivirus" : GROUP_OTHER,  #TODO
        "app-arch" : GROUP_OTHER,
        "app-backup" : GROUP_OTHER,
        "app-benchmarks" : GROUP_OTHER,
        "app-cdr" : GROUP_OTHER,
        "app-crypt" : GROUP_OTHER,
        "app-dicts" : GROUP_OTHER,
        "app-doc" : GROUP_OTHER,
        "app-editors" : GROUP_OTHER,
        "app-emacs" : GROUP_OTHER,
        "app-emulation" : GROUP_OTHER,
        "app-forensics" : GROUP_OTHER,
        "app-i18n" : GROUP_OTHER,
        "app-laptop" : GROUP_OTHER,
        "app-misc" : GROUP_OTHER,
        "app-mobilephone" : GROUP_OTHER,
        "app-office" : GROUP_OFFICE,
        "app-pda" : GROUP_OTHER,
        "app-portage" : GROUP_OTHER,
        "app-shells" : GROUP_OTHER,
        "app-text" : GROUP_OTHER,
        "app-vim" : GROUP_OTHER,
        "app-xemacs" : GROUP_OTHER,
        "dev-ada" : GROUP_OTHER,
        "dev-cpp" : GROUP_OTHER,
        "dev-db" : GROUP_OTHER,
        "dev-dotnet" : GROUP_OTHER,
        "dev-embedded" : GROUP_OTHER,
        "dev-games" : GROUP_OTHER,
        "dev-haskell" : GROUP_OTHER,
        "dev-java" : GROUP_OTHER,
        "dev-lang" : GROUP_OTHER,
        "dev-libs" : GROUP_OTHER,
        "dev-lisp" : GROUP_OTHER,
        "dev-ml" : GROUP_OTHER,
        "dev-perl" : GROUP_OTHER,
        "dev-php" : GROUP_OTHER,
        "dev-php5" : GROUP_OTHER,
        "dev-python" : GROUP_OTHER,
        "dev-ruby" : GROUP_OTHER,
        "dev-scheme" : GROUP_OTHER,
        "dev-tcltk" : GROUP_OTHER,
        "dev-tex" : GROUP_OTHER,
        "dev-texlive" : GROUP_OTHER,
        "dev-tinyos" : GROUP_OTHER,
        "dev-util" : GROUP_OTHER,
        "games-action" : GROUP_GAMES, # DONE from there
        "games-arcade" : GROUP_GAMES,
        "games-board" : GROUP_GAMES,
        "games-emulation" : GROUP_GAMES,
        "games-engines" : GROUP_GAMES,
        "games-fps" : GROUP_GAMES,
        "games-kids" : GROUP_GAMES,
        "games-misc" : GROUP_GAMES,
        "games-mud" : GROUP_GAMES,
        "games-puzzle" : GROUP_GAMES,
        "games-roguelike" : GROUP_GAMES,
        "games-rpg" : GROUP_GAMES,
        "games-server" : GROUP_GAMES,
        "games-simulation" : GROUP_GAMES,
        "games-sports" : GROUP_GAMES,
        "games-strategy" : GROUP_GAMES,
        "games-util" : GROUP_GAMES,
        "gnome-base" : GROUP_DESKTOP_GNOME,
        "gnome-extra" : GROUP_DESKTOP_GNOME,
        "gnustep-apps" : GROUP_OTHER,   # TODO: from there
        "gnustep-base" : GROUP_OTHER,
        "gnustep-libs" : GROUP_OTHER,
        "gpe-base" : GROUP_OTHER,
        "gpe-utils" : GROUP_OTHER,
        "java-virtuals" : GROUP_OTHER,
        "kde-base" : GROUP_DESKTOP_KDE, # DONE from there
        "kde-misc" : GROUP_DESKTOP_KDE,
        "lxde-base" : GROUP_DESKTOP_OTHER,
        "mail-client" : GROUP_COMMUNICATION, # TODO: or GROUP_INTERNET ?
        "mail-filter" : GROUP_OTHER, # TODO: from there
        "mail-mta" : GROUP_OTHER,
        "media-fonts" : GROUP_FONTS, # DONE (only this one)
        "media-gfx" : GROUP_OTHER,
        "media-libs" : GROUP_OTHER,
        "media-plugins" : GROUP_OTHER,
        "media-radio" : GROUP_OTHER,
        "media-sound" : GROUP_OTHER,
        "media-tv" : GROUP_OTHER,
        "media-video" : GROUP_OTHER,
        "metadata" : GROUP_OTHER,
        "net-analyzer" : GROUP_OTHER,
        "net-dialup" : GROUP_OTHER,
        "net-dns" : GROUP_OTHER,
        "net-firewall" : GROUP_OTHER,
        "net-fs" : GROUP_OTHER,
        "net-ftp" : GROUP_OTHER,
        "net-im" : GROUP_OTHER,
        "net-irc" : GROUP_OTHER,
        "net-libs" : GROUP_OTHER,
        "net-mail" : GROUP_OTHER,
        "net-misc" : GROUP_OTHER,
        "net-nds" : GROUP_OTHER,
        "net-news" : GROUP_OTHER,
        "net-nntp" : GROUP_OTHER,
        "net-p2p" : GROUP_OTHER,
        "net-print" : GROUP_OTHER,
        "net-proxy" : GROUP_OTHER,
        "net-voip" : GROUP_OTHER,
        "net-wireless" : GROUP_OTHER,
        "net-zope" : GROUP_OTHER,
        "perl-core" : GROUP_OTHER,
        "profiles" : GROUP_OTHER,
        "rox-base" : GROUP_OTHER,
        "rox-extra" : GROUP_OTHER,
        "sci-astronomy" : GROUP_SCIENCE, # DONE from there
        "sci-biology" : GROUP_SCIENCE,
        "sci-calculators" : GROUP_SCIENCE,
        "sci-chemistry" : GROUP_SCIENCE,
        "sci-electronics" : GROUP_SCIENCE,
        "sci-geosciences" : GROUP_SCIENCE,
        "sci-libs" : GROUP_SCIENCE,
        "sci-mathematics" : GROUP_SCIENCE,
        "sci-misc" : GROUP_SCIENCE,
        "sci-physics" : GROUP_SCIENCE,
        "sci-visualization" : GROUP_SCIENCE,
        "sec-policy" : GROUP_OTHER, # TODO: from there
        "sys-apps" : GROUP_OTHER,
        "sys-auth" : GROUP_OTHER,
        "sys-block" : GROUP_OTHER,
        "sys-boot" : GROUP_OTHER,
        "sys-cluster" : GROUP_OTHER,
        "sys-devel" : GROUP_OTHER,
        "sys-freebsd" : GROUP_OTHER,
        "sys-fs" : GROUP_OTHER,
        "sys-kernel" : GROUP_OTHER,
        "sys-libs" : GROUP_OTHER,
        "sys-power" : GROUP_OTHER,
        "sys-process" : GROUP_OTHER,
        "virtual" : GROUP_OTHER,
        "www-apache" : GROUP_OTHER,
        "www-apps" : GROUP_OTHER,
        "www-client" : GROUP_OTHER,
        "www-misc" : GROUP_OTHER,
        "www-plugins" : GROUP_OTHER,
        "www-servers" : GROUP_OTHER,
        "x11-apps" : GROUP_OTHER,
        "x11-base" : GROUP_OTHER,
        "x11-drivers" : GROUP_OTHER,
        "x11-libs" : GROUP_OTHER,
        "x11-misc" : GROUP_OTHER,
        "x11-plugins" : GROUP_OTHER,
        "x11-proto" : GROUP_OTHER,
        "x11-terms" : GROUP_OTHER,
        "x11-themes" : GROUP_OTHER,
        "x11-wm" : GROUP_OTHER,
        "xfce-base" : GROUP_DESKTOP_XFCE, # DONE from there
        "xfce-extra" : GROUP_DESKTOP_XFCE
}


def sigquit(signum, frame):
    sys.exit(1)

def id_to_cpv(pkgid):
    '''
    Transform the package id (packagekit) to a cpv (portage)
    '''
    # TODO: raise error if ret[0] doesn't contain a '/'
    ret = split_package_id(pkgid)

    if len(ret) < 4:
        raise "id_to_cpv: package id not valid"

    return ret[0] + "-" + ret[1]

# TODO: move to class ?
def get_group(cp):
    ''' Return the group of the package
    Argument could be cp or cpv. '''
    cat = portage.catsplit(cp)[0]
    if SECTION_GROUP_MAP.has_key(cat):
        return SECTION_GROUP_MAP[cat]

    return GROUP_UNKNOWN


class PackageKitPortageBackend(PackageKitBaseBackend, PackagekitPackage):

    def __init__(self, args, lock=True):
        signal.signal(signal.SIGQUIT, sigquit)
        PackageKitBaseBackend.__init__(self, args)

        self.portage_settings = portage.config()
        self.vardb = portage.db[portage.settings["ROOT"]]["vartree"].dbapi
        #self.portdb = portage.db[portage.settings["ROOT"]]["porttree"].dbapi

        if lock:
            self.doLock()

    def is_installed(self, cpv):
        if self.vardb.cpv_exists(cpv):
            return True
        return False

    def get_newer_cpv(self, cpv_list):
        newer = cpv_list[0]
        for cpv in cpv_list:
	        if portage.pkgcmp(portage.pkgsplit(cpv),portage.pkgsplit(newer)) == 1:
		        newer = cpv
        return newer

    def get_metadata(self, cpv, keys, in_dict = False):
        if self.is_installed(cpv):
            aux_get = self.vardb.aux_get
        else:
            aux_get = portage.portdb.aux_get

        if in_dict:
            return dict(izip(keys, aux_get(cpv, keys)))
        else:
            return aux_get(cpv, keys)

    def filter_free(self, cpv_list, fltlist):
        def _has_validLicense(cpv):
            metadata = self.get_metadata(cpv, ["LICENSE", "USE", "SLOT"], True)
            return not self.portage_settings._getMissingLicenses(cpv, metadata)

        if FILTER_FREE in fltlist or FILTER_NOT_FREE in fltlist:
            free_licenses = "@FSF-APPROVED"
            if FILTER_FREE in fltlist:
                licenses = "-* " + free_licenses
            else:
                licenses = "* -" + free_licenses
            backup_license = self.portage_settings["ACCEPT_LICENSE"]
            self.portage_settings["ACCEPT_LICENSE"] = licenses
            self.portage_settings.backup_changes("ACCEPT_LICENSE")
            self.portage_settings.regenerate()

            cpv_list = filter(_has_validLicense, cpv_list)

            self.portage_settings["ACCEPT_LICENSE"] = backup_license
            self.portage_settings.backup_changes("ACCEPT_LICENSE")
            self.portage_settings.regenerate()

        return cpv_list

    def get_all_cp(self, fltlist):
        # NOTES:
        # returns a list of cp
        #
        # FILTERS:
        # - installed: ok
        # - free: ok (should be done with cpv)
        # - newest: ok (should be finished with cpv)
        cp_list = []

        if FILTER_INSTALLED in fltlist:
            cp_list = self.vardb.cp_all()
        elif FILTER_NOT_INSTALLED in fltlist:
            cp_list = portage.portdb.cp_all()
        else:
            # need installed packages first
            cp_list = self.vardb.cp_all()
            for cp in portage.portdb.cp_all():
                if cp not in cp_list:
                    cp_list.append(cp)

        return cp_list

    def get_all_cpv(self, cp, fltlist):
        # NOTES:
        # returns a list of cpv
        #
        # FILTERS:
        # - installed: ok
        # - free: ok
        # - newest: ok

        cpv_list = []

        # populate cpv_list taking care of installed filter
        if FILTER_INSTALLED in fltlist:
            cpv_list = self.vardb.match(cp)
        elif FILTER_NOT_INSTALLED in fltlist:
            for cpv in portage.portdb.match(cp):
                if not self.is_installed(cpv):
                    cpv_list.append(cpv)
        else:
            cpv_list = self.vardb.match(cp)
            for cpv in portage.portdb.match(cp):
                if cpv not in cpv_list:
                    cpv_list.append(cpv)

        if len(cpv_list) == 0:
            return []

        # free filter
        cpv_list = self.filter_free(cpv_list, fltlist)

        if len(cpv_list) == 0:
            return []

        # newest filter
        if FILTER_NEWEST in fltlist:
            # if FILTER_INSTALLED in fltlist, cpv_list=cpv_list
            if FILTER_NOT_INSTALLED in fltlist:
                cpv_list = [cpv_list[-1]]
            elif FILTER_INSTALLED not in fltlist:
                # cpv_list is not ordered so getting newer and filter others
                newer_cpv = self.get_newer_cpv(cpv_list)
                cpv_list = filter(
                        lambda cpv: self.is_installed(cpv) or cpv == newer_cpv,
                        cpv_list)

        if len(cpv_list) == 0:
            return []

        return cpv_list

    def cpv_to_id(self, cpv):
        '''
        Transform the cpv (portage) to a package id (packagekit)
        '''
        # TODO: manage SLOTS !
        package, version, rev = portage.pkgsplit(cpv)
        pkg_keywords, repo = self.get_metadata(cpv, ["KEYWORDS", "repository"])

        pkg_keywords = pkg_keywords.split()
        sys_keywords = self.portage_settings["ACCEPT_KEYWORDS"].split()
        keywords = []

        for x in sys_keywords:
            if x in pkg_keywords:
                keywords.append(x)

        # if no keywords, check in package.keywords
        if not keywords:
            key_dict = self.portage_settings.pkeywordsdict.get(portage.dep_getkey(cpv))
            if key_dict:
                for _, keys in key_dict.iteritems():
                    for x in keys:
                        keywords.append(x)

        if not keywords:
            keywords.append("no keywords")
            self.message(MESSAGE_UNKNOWN, "No keywords have been found for %s" % cpv)

        # don't want to see -r0
        if rev != "r0":
            version = version + "-" + rev

        # if installed, repo should be 'installed', packagekit rule
        if self.is_installed(cpv):
            repo = "installed"

        return get_package_id(package, version, ' '.join(keywords), repo)

    def package(self, cpv, info=None):
        desc = self.get_metadata(cpv, ["DESCRIPTION"])[0]
        if not info:
            if self.is_installed(cpv):
                info = INFO_INSTALLED
            else:
                info = INFO_AVAILABLE
        PackageKitBaseBackend.package(self, self.cpv_to_id(cpv), info, desc)

    def get_depends(self, filters, pkgids, recursive):
        # TODO: manage filters
        # TODO: optimize by using vardb for installed packages ?
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        for pkgid in pkgids:
            cpv = id_to_cpv(pkgid)

            # is cpv valid
            if not portage.portdb.cpv_exists(cpv):
                # self.warning ? self.error ?
                self.message(MESSAGE_COULD_NOT_FIND_PACKAGE,
                        "Could not find the package %s" % pkgid)
                continue

            myopts = {}
            if recursive:
                myopts.pop("--emptytree", None)
                myopts["--emptytree"] = True
            spinner = ""
            settings, trees, mtimedb = _emerge.load_emerge_config()
            myparams = _emerge.create_depgraph_params(myopts, "")
            spinner = _emerge.stdout_spinner()
            depgraph = _emerge.depgraph(settings, trees, myopts, myparams, spinner)
            retval, fav = depgraph.select_files(["="+cpv])
            if not retval:
                self.error(ERROR_INTERNAL_ERROR, "Wasn't able to get dependency graph")
                continue

            if recursive:
                # printing the whole tree
                pkgs = depgraph.altlist(reversed=1)
                for pkg in pkgs:
                    if pkg[2] != cpv:
                        self.package(pkg[2])
            else: # !recursive
                # only printing child of the root node
                # actually, we have "=cpv" -> "cpv" -> children
                root_node = depgraph.digraph.root_nodes()[0] # =cpv
                root_node = depgraph.digraph.child_nodes(root_node)[0] # cpv
                children = depgraph.digraph.child_nodes(root_node)
                for child in children:
                    self.package(child[2])

    def get_details(self, pkgs):
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        for pkg in pkgs:
            cpv = id_to_cpv(pkg)

            # is cpv valid
            if not portage.portdb.cpv_exists(cpv):
                # self.warning ? self.error ?
                self.message(MESSAGE_COULD_NOT_FIND_PACKAGE,
                        "Could not find the package %s" % pkg)
                continue

            homepage, desc, license = self.get_metadata(cpv,
                    ["HOMEPAGE", "DESCRIPTION", "LICENSE"])

            # size should be prompted only if not installed
            size = 0
            if not self.is_installed(cpv):
                ebuild = portage.portdb.findname(cpv)
                if ebuild:
                    dir = os.path.dirname(ebuild)
                    manifest = portage.manifest.Manifest(dir, portage.settings["DISTDIR"])
                    uris = portage.portdb.getFetchMap(cpv)
                    size = manifest.getDistfilesSize(uris)

            self.details(self.cpv_to_id(cpv), license, get_group(cpv),
                    desc, homepage, size)

    def get_files(self, pkgids):
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        for pkgid in pkgids:
            cpv = id_to_cpv(pkgid)

            # is cpv valid
            if not portage.portdb.cpv_exists(cpv):
                self.error(ERROR_PACKAGE_NOT_FOUND,
                        "Package %s was not found" % pkgid)
                continue

            if not self.vardb.cpv_exists(cpv):
                self.error(ERROR_PACKAGE_NOT_INSTALLED,
                        "Package %s is not installed" % pkgid)
                continue

            cat, pv = portage.catsplit(cpv)
            db = portage.dblink(cat, pv, portage.settings["ROOT"],
                    self.portage_settings, treetype="vartree", vartree=self.vardb)
            files = db.getcontents().keys()
            files = sorted(files)
            files = ";".join(files)

            self.files(pkgid, files)

    def get_packages(self, filters):
        # TODO: use cases tests on fedora 11
        # TODO: progress ?
        # TODO: installed before non-installed ?
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)

        fltlist = filters.split(';')

        for cp in self.get_all_cp(fltlist):
            for cpv in self.get_all_cpv(cp, fltlist):
                self.package(cpv)

    def get_repo_list(self, filters):
        # TODO: filters
        # TODO: not official
        # TODO: not supported (via filters ?)
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        layman_db = layman.db.RemoteDB(layman.config.Config())
        for o in layman_db.overlays.keys():
            self.repo_detail(o, layman_db.overlays[o].description, True)

    def get_requires(self, filters, pkgs, recursive):
        # TODO: filters
        # TODO: recursive not implemented
        # TODO: usefulness ? use cases
        # TODO: work only on installed packages
        self.status(STATUS_RUNNING)
        self.allow_cancel(True)
        self.percentage(None)

        myopts = {}
        spinner = ""
        favorites = []
        settings, trees, mtimedb = _emerge.load_emerge_config()
        spinner = _emerge.stdout_spinner()
        rootconfig = _emerge.RootConfig(self.portage_settings, trees["/"],
                portage._sets.load_default_config(self.portage_settings, trees["/"]))

        for pkg in pkgs:
            cpv = id_to_cpv(pkg)

            # is cpv installed
            # TODO: keep error msg ?
            if not self.vardb.match(cpv):
                self.error(ERROR_PACKAGE_NOT_INSTALLED,
                        "Package %s is not installed" % pkg)
                continue

            required_set_names = ("system", "world")
            required_sets = {}

            args_set = portage._sets.base.InternalPackageSet()
            args_set.update(["="+cpv]) # parameters is converted to atom
            # or use portage.dep_expand

            if not args_set:
                self.error(ERROR_INTERNAL_ERROR, "Was not able to generate atoms")
                continue
            
            depgraph = _emerge.depgraph(settings, trees, myopts,
                    _emerge.create_depgraph_params(myopts, "remove"), spinner)
            vardb = depgraph.trees["/"]["vartree"].dbapi

            for s in required_set_names:
                required_sets[s] = portage._sets.base.InternalPackageSet(
                        initial_atoms=rootconfig.setconfig.getSetAtoms(s))

            # TODO: error/warning if world = null or system = null ?

            # TODO: not sure it's needed. for deselect in emerge...
            required_sets["world"].clear()
            for pkg in vardb:
                spinner.update()
                try:
                    if args_set.findAtomForPackage(pkg) is None:
                        required_sets["world"].add("=" + pkg.cpv)
                except portage.exception.InvalidDependString, e:
                    required_sets["world"].add("=" + pkg.cpv)

            set_args = {}
            for s, pkg_set in required_sets.iteritems():
                set_atom = portage._sets.SETPREFIX + s
                set_arg = _emerge.SetArg(arg=set_atom, set=pkg_set,
                        root_config=depgraph.roots[portage.settings["ROOT"]])
                set_args[s] = set_arg
                for atom in set_arg.set:
                    depgraph._dep_stack.append(
                            _emerge.Dependency(atom=atom, root=portage.settings["ROOT"],
                                parent=set_arg))
                    depgraph.digraph.add(set_arg, None)

            if not depgraph._complete_graph():
                self.error(ERROR_INTERNAL_ERROR, "Error when generating depgraph")
                continue

            def cmp_pkg_cpv(pkg1, pkg2):
                if pkg1.cpv > pkg2.cpv:
                    return 1
                elif pkg1.cpv == pkg2.cpv:
                    return 0
                else:
                    return -1

            for pkg in sorted(vardb,
                    key=portage.util.cmp_sort_key(cmp_pkg_cpv)):
                arg_atom = None
                try:
                    arg_atom = args_set.findAtomForPackage(pkg)
                except portage.exception.InvalidDependString:
                    continue

                if arg_atom and pkg in depgraph.digraph:
                    parents = depgraph.digraph.parent_nodes(pkg)
                    for node in parents:
                        self.package(node[2])

        self.percentage(100)

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

            cpv = id_to_cpv(pkg)

            if not portage.portdb.cpv_exists(cpv):
                self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, "could not find %s" % pkg)

            for cpv in self.vardb.match(portage.pkgsplit(cpv)[0]):
                updates.append(cpv)
            updates = "&".join(updates)

            # temporarily set vendor_url = homepage
            homepage = self.get_metadata(cpv, ["HOMEPAGE"])[0]
            vendor_url = homepage

            self.update_detail(pkg, updates, obsoletes, vendor_url, bugzilla_url,
                    cve_url, "none", "No update text", "No ChangeLog",
                    UPDATE_STATE_STABLE, None, None)

    def get_updates(self, filters):
        # NOTES:
        # portage prefer not to update _ALL_ packages so we will only list updated
        # packages in world or system

        # TODO: filters ?
        # TODO: INFO
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        # best way to get that ?
        settings, trees, mtimedb = _emerge.load_emerge_config()
        rootconfig = _emerge.RootConfig(self.portage_settings, trees["/"],
                portage._sets.load_default_config(self.portage_settings, trees["/"]))

        cp_to_check = []

        # get system and world sets
        for s in ("system", "world"):
            set = portage._sets.base.InternalPackageSet(
                    initial_atoms=rootconfig.setconfig.getSetAtoms(s))
            for cp in set:
                cp_to_check.append(cp)

        # check if bestmatch is installed
        for cp in cp_to_check:
            best_cpv = portage.portdb.xmatch("bestmatch-visible", cp)
            if not self.vardb.cpv_exists(best_cpv):
                self.package(best_cpv, INFO_NORMAL)

    def install_packages(self, only_trusted, pkgs):
        self.status(STATUS_RUNNING)
        self.allow_cancel(True) # TODO: sure ?
        self.percentage(None)

        # FIXME: use only_trusted

        for pkg in pkgs:
            # check for installed is not mandatory as there are a lot of reason
            # to re-install a package (USE/{LD,C}FLAGS change for example) (or live)
            # TODO: keep a final position
            cpv = id_to_cpv(pkg)

            # is cpv valid
            if not portage.portdb.cpv_exists(cpv):
                self.error(ERROR_PACKAGE_NOT_FOUND, "Package %s was not found" % pkg)
                continue

            # inits
            myopts = {} # TODO: --nodepends ?
            spinner = ""
            favorites = []
            settings, trees, mtimedb = _emerge.load_emerge_config()
            myparams = _emerge.create_depgraph_params(myopts, "")
            spinner = _emerge.stdout_spinner()

            depgraph = _emerge.depgraph(settings, trees, myopts, myparams, spinner)
            retval, favorites = depgraph.select_files(["="+cpv])
            if not retval:
                self.error(ERROR_INTERNAL_ERROR, "Wasn't able to get dependency graph")
                continue

            if "resume" in mtimedb and \
            "mergelist" in mtimedb["resume"] and \
            len(mtimedb["resume"]["mergelist"]) > 1:
                mtimedb["resume_backup"] = mtimedb["resume"]
                del mtimedb["resume"]
                mtimedb.commit()

            mtimedb["resume"] = {}
            mtimedb["resume"]["myopts"] = myopts.copy()
            mtimedb["resume"]["favorites"] = [str(x) for x in favorites]

            # TODO: check for writing access before calling merge ?

            mergetask = _emerge.Scheduler(settings, trees, mtimedb,
                    myopts, spinner, depgraph.altlist(),
                    favorites, depgraph.schedulerGraph())
            mergetask.merge()

    def refresh_cache(self, force):
        # TODO: use force ?
        self.status(STATUS_REFRESH_CACHE)
        self.allow_cancel(True)
        self.percentage(None)

        myopts = {} # TODO: --quiet ?
        myopts.pop("--quiet", None)
        myopts["--quiet"] = True
        settings, trees, mtimedb = _emerge.load_emerge_config()
        spinner = _emerge.stdout_spinner()
        try:
            _emerge.action_sync(settings, trees, mtimedb, myopts, "")
        finally:
            self.percentage(100)

    def remove_packages(self, allowdep, pkgs):
        # can't use allowdep: never removing dep
        # TODO: filters ?
        self.status(STATUS_RUNNING)
        self.allow_cancel(True)
        self.percentage(None)

        for pkg in pkgs:
            cpv = id_to_cpv(pkg)

            # is cpv valid
            if not portage.portdb.cpv_exists(cpv):
                self.error(ERROR_PACKAGE_NOT_FOUND, "Package %s was not found" % pkg)
                continue

            # is package installed
            if not self.vardb.match(cpv):
                self.error(ERROR_PACKAGE_NOT_INSTALLED,
                        "Package %s is not installed" % pkg)
                continue

            myopts = {} # TODO: --nodepends ?
            spinner = ""
            favorites = []
            settings, trees, mtimedb = _emerge.load_emerge_config()
            spinner = _emerge.stdout_spinner()
            rootconfig = _emerge.RootConfig(self.portage_settings, trees["/"],
                    portage._sets.load_default_config(self.portage_settings, trees["/"])
                    )

            if "resume" in mtimedb and \
            "mergelist" in mtimedb["resume"] and \
            len(mtimedb["resume"]["mergelist"]) > 1:
                mtimedb["resume_backup"] = mtimedb["resume"]
                del mtimedb["resume"]
                mtimedb.commit()

            mtimedb["resume"] = {}
            mtimedb["resume"]["myopts"] = myopts.copy()
            mtimedb["resume"]["favorites"] = [str(x) for x in favorites]

            db_keys = list(portage.portdb._aux_cache_keys)
            metadata = self.get_metadata(cpv, db_keys)
            package = _emerge.Package(
                    type_name="ebuild",
                    built=True,
                    installed=True,
                    root_config=rootconfig,
                    cpv=cpv,
                    metadata=metadata,
                    operation="uninstall")

            mergetask = _emerge.Scheduler(settings,
                    trees, mtimedb, myopts, spinner, [package], favorites, package)
            mergetask.merge()

    def repo_enable(self, repoid, enable):
        self.status(STATUS_INFO)
        self.allow_cancel(True)
        self.percentage(None)

        # get installed and available dbs
        installed_layman_db = layman.db.DB(layman.config.Config())
        available_layman_db = layman.db.RemoteDB(layman.config.Config())

        # disabling (removing) a db
        if not enable:
            if not repoid in installed_layman_db.overlays.keys():
                self.error(ERROR_REPO_NOT_FOUND, "Repository %s was not found" %repoid)
                return

            overlay = installed_layman_db.select(repoid)

            if not overlay:
                self.error(ERROR_REPO_NOT_FOUND, "Repository %s was not found" %repoid)
                return

            try:
                installed_layman_db.delete(overlay)
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR,
                        "Failed to disable repository " + repoid + " : " + str(e))
                return

        # enabling (adding) a db
        if enable:
            if not repoid in available_layman_db.overlays.keys():
                self.error(ERROR_REPO_NOT_FOUND, "Repository %s was not found" %repoid)
                return

            overlay = available_layman_db.select(repoid)

            if not overlay:
                self.error(ERROR_REPO_NOT_FOUND, "Repository %s was not found" %repoid)
                return

            try:
                installed_layman_db.add(overlay, True)
            except Exception, e:
                self.error(ERROR_INTERNAL_ERROR,
                        "Failed to disable repository " + repoid + " : " + str(e))
                return

    def resolve(self, filters, pkgs):
        # TODO: filters
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)

        for pkg in pkgs:
            # TODO: be case sensitive ?
            searchre = re.compile(pkg, re.IGNORECASE)

            # TODO: optim with filter = installed
            for cp in portage.portdb.cp_all():
                if searchre.search(cp):
                    #print self.vardb.dep_bestmatch(cp)
                    self.package(portage.portdb.xmatch("bestmatch-visible", cp))

    def search_details(self, filters, key):
        # TODO: add keywords when they will be available
        # TODO: filters
        # TODO: split keys
        # TODO: PERFORMANCE !
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)

        searchre = re.compile(key, re.IGNORECASE)
        cpvlist = []

        for cp in portage.portdb.cp_all():
            # TODO: baaad, we are working on _every_ cpv :-/
            for cpv in portage.portdb.match(cp): #TODO: cp_list(cp) ?
                infos = self.get_metadata(cpv, ["HOMEPAGE","DESCRIPTION","repository"]) # LICENSE ?
                for x in infos:
                    if searchre.search(x):
                        self.package(cpv)
                        break

    def search_file(self, filters, key):
        # TODO: update specifications
        # FILTERS:
        # - ~installed is not accepted (error)
        # - free: ok
        # - newest: as only installed, by himself
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(0)

        fltlist = filters.split(';')
        if FILTER_NOT_INSTALLED in fltlist:
            self.error(ERROR_CANNOT_GET_FILELIST,
                    "search-filelist isn't available with ~installed filter")
            return

        cpv_results = []
        cpv_list = self.vardb.cpv_all()
        nb_cpv = 0.0
        cpv_processed = 0.0
        is_full_path = True

        if key[0] != "/":
            is_full_path = False
            searchre = re.compile("/" + key + "$", re.IGNORECASE)

        # free filter
        cpv_list = self.filter_free(cpv_list, fltlist)
        nb_cpv = float(len(cpv_list))

        for cpv in cpv_list:
            cat, pv = portage.catsplit(cpv)
            db = portage.dblink(cat, pv, portage.settings["ROOT"],
                    self.portage_settings, treetype="vartree",
                    vartree=self.vardb)
            contents = db.getcontents()
            if not contents:
                continue
            for file in contents.keys():
                if (is_full_path and key == file) \
                or (not is_full_path and searchre.search(file)):
                    cpv_results.append(cpv)
                    break

            cpv_processed += 100.0
            self.percentage(cpv_processed/nb_cpv)

        self.percentage(100)

        for cpv in cpv_results:
            self.package(cpv)

    def search_group(self, filters, group):
        # TODO: input has to be checked by server before
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(0)

        fltlist = filters.split(';')
        cpv_list = self.get_all_cp(fltlist)
        nb_cpv = float(len(cpv_list))
        cpv_processed = 0.0

        for cp in cpv_list:
            if get_group(cp) == group:
                for cpv in self.get_all_cpv(cp, fltlist):
                    self.package(cpv)

            cpv_processed += 100.0
            self.percentage(cpv_processed/nb_cpv)

        self.percentage(100)

    def search_name(self, filters, key):
        # TODO: input has to be checked by server before
        # TODO: use muli-key input ?
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(0)

        fltlist = filters.split(';')
        cpv_list = self.get_all_cp(fltlist)
        nb_cpv = float(len(cpv_list))
        cpv_processed = 0.0
        searchre = re.compile(key, re.IGNORECASE)

        for cp in cpv_list:
            if searchre.search(cp):
                for cpv in self.get_all_cpv(cp, fltlist):
                    self.package(cpv)

            cpv_processed += 100.0
            self.percentage(cpv_processed/nb_cpv)

        self.percentage(100)

    def update_packages(self, only_trusted, pkgs):
        # TODO: add some checks ?
        self.install_packages(only_trusted, pkgs)

    def update_system(self, only_trusted):
        # TODO: only_trusted
        self.status(STATUS_RUNNING)
        self.allow_cancel(True)
        self.percentage(None)

        # inits
        myopts = {}
        myopts.pop("--deep", None)
        myopts.pop("--newuse", None)
        myopts.pop("--update", None)
        myopts["--deep"] = True
        myopts["--newuse"] = True
        myopts["--update"] = True

        spinner = ""
        favorites = []
        settings, trees, mtimedb = _emerge.load_emerge_config()
        myparams = _emerge.create_depgraph_params(myopts, "")
        spinner = _emerge.stdout_spinner()

        depgraph = _emerge.depgraph(settings, trees, myopts, myparams, spinner)
        retval, favorites = depgraph.select_files(["system", "world"])
        if not retval:
            self.error(ERROR_INTERNAL_ERROR, "Wasn't able to get dependency graph")
            return

        if "resume" in mtimedb and \
        "mergelist" in mtimedb["resume"] and \
        len(mtimedb["resume"]["mergelist"]) > 1:
            mtimedb["resume_backup"] = mtimedb["resume"]
            del mtimedb["resume"]
            mtimedb.commit()

        mtimedb["resume"] = {}
        mtimedb["resume"]["myopts"] = myopts.copy()
        mtimedb["resume"]["favorites"] = [str(x) for x in favorites]

        mergetask = _emerge.Scheduler(settings, trees, mtimedb,
                myopts, spinner, depgraph.altlist(),
                favorites, depgraph.schedulerGraph())
        mergetask.merge()

def main():
    backend = PackageKitPortageBackend("") #'', lock=True)
    backend.dispatcher(sys.argv[1:])

if __name__ == "__main__":
    main()
