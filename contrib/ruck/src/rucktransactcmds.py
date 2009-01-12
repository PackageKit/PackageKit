###
### Copyright 2002 Ximian, Inc.
### Copyright 2008 Aidan Skinner <aidan@skinner.me.uk>
###
### This program is free software; you can redistribute it and/or modify
### it under the terms of the GNU General Public License, version 2,
### as published by the Free Software Foundation.
###
### This program is distributed in the hope that it will be useful,
### but WITHOUT ANY WARRANTY; without even the implied warranty of
### MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
### GNU General Public License for more details.
###
### You should have received a copy of the GNU General Public License
### along with this program; if not, write to the Free Software
### Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
###

import sys
import os
import os.path
import glob
from ConfigParser import ConfigParser
import string
import rucktalk
import ruckformat
import ruckcommand
import ruckpackagecmds

class TransactCmd(ruckpackagecmds.PackageCmd):
    def local_opt_table(self):
        return [["N",  "dry-run", "", "Perform a dry run"]]

class UpdateCmd(TransactCmd):

    def name(self):
        return "update"

    def aliases(self):
        return ["up"]

    def is_basic(self):
        return 1

    def category(self):
        return "package"

    def arguments(self):
        # TODO: this should optionally call UpdatePackages with a list of packages to update
        return ""

    def description_short(self):
        return "Perform an update"

    def local_opt_table(self):
        return []

    def execute(self, options_dict, non_option_args):
        pkcon = self.pkcon()

        updates = pkcon.get_updates()
        if (len(updates) == 0):
            rucktalk.message("--- No updates found ---")
            exit()

        rucktalk.message("The following packages will be updated:")

        table_keys = ["repo", "name", "version"]
        table_rows = []
        for new_pkg in updates:
            row = ruckformat.package_to_row(new_pkg, False, table_keys)
            table_rows.append(row)

        table_rows.sort(lambda x,y:cmp(string.lower(x[1]), string.lower(y[1])))

        ruckformat.tabular(["Repository", "Name","Version"],
                          table_rows)
        # FIXME: this prompt is horrid
        resp = rucktalk.prompt("Continue? Y/[N]")
        if (resp == 'y'):
            # FIXME: needs to deal with progress better
            pkcon.update_packages(updates)
        else:
            rucktalk.message("Update aborted")

ruckcommand.register(UpdateCmd)


class InstallCmd(TransactCmd):

    def name(self):
        return "install"

    def aliases(self):
        return ["in"]

    def is_basic(self):
        return 1

    def category(self):
        return "package"

    def arguments(self):
        return "<package> ..."

    def description_short(self):
        return "Perform an install"

    def local_opt_table(self):
        return []

    def separate_args(self, args):
        installs = []
        removals = []

        for arg in args:
            if arg.startswith('~'):
                removals.append(arg[1:])
            else:
                installs.append(arg)

        return installs, removals

    def execute(self, options_dict, non_option_args):
        if len(non_option_args) < 1:
            self.usage()
            return 1

        pk = self.pkcon()

        for arg in non_option_args:
            if os.path.exists(arg):
                rucktalk.error("This hasn't been implemented yet") # FIXME
            else:
                installs, removals = self.separate_args(non_option_args)

                pkids = pk.resolve(installs)
                if len(pkids) > 0:
                    pk.install_packages(pkids)
                else:
                    rucktalk.error("No packages found")
                    return 1

                if len(removals) > 0:
                    pk.remove_packages(removals)

ruckcommand.register(InstallCmd)


class RemoveCmd(TransactCmd):

    def name(self):
        return "remove"

    def aliases(self):
        return ["rm"]

    def is_basic(self):
        return 1

    def category(self):
        return "package"

    def arguments(self):
        return "<package> ..."

    def description_short(self):
        return "Perform a removal"

    def execute(self, options_dict, non_option_args):
        if len(non_option_args) < 1:
            self.usage()
            return 1

        yum = self.yum()

        exactmatches, matches, unmatched = self.find_packages(non_option_args, installed=1)
        if len(unmatched) > 0:
            rucktalk.error("Could not find package '%s'" % unmatched[0])
            return False

        plist = exactmatches + matches

        for p in plist:
            yum.remove(p)

        self.start_transaction(dryrun=options_dict.has_key('dry-run'))

#ruckcommand.register(RemoveCmd)

class PackageSolveDepsCmd(TransactCmd):

    def name(self):
        return "solvedeps"

    def aliases(self):
        return ["solve"]

    def is_basic(self):
        return 0

    def category(self):
        return "dependency"

    def arguments(self):
        return "<package-dep>"

    def description_short(self):
        return "Resolve dependencies for libraries"

    def execute(self, options_dict, non_option_args):
        if len(non_option_args) < 1:
            self.usage()
            return False

        plist = []
        yum = self.yum()

        for dep in non_option_args:
            if yum.returnInstalledPackagesByDep (dep):
                continue

            try:
                pkg = yum.returnPackageByDep(dep)
                plist.append(pkg.name)
            except:
                rucktalk.error("Unable to satisfy requirement '%s'" % dep)
                return False

        installs, updates = self.find_available_packages(plist)
        if not installs and not updates:
            rucktalk.message("Requirements are already met on the system.");
            return True

        for i in installs:
            yum.install(i)

        for u in updates:
            exactmatches, matches, unmatched = self.find_packages([u.name], installed=True)
            yum.tsInfo.addUpdate(u, exactmatches[0])

        self.start_transaction(dryrun=options_dict.has_key('dry-run'))

#ruckcommand.register(PackageSolveDepsCmd)
