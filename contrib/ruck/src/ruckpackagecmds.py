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
import re
import os.path
import shutil
import glob
from ConfigParser import ConfigParser
import string
import rucktalk
import ruckformat
import ruckcommand

import rpmUtils.arch
from rpmUtils.miscutils import compareEVR
from packagekit import enums as pkenums

class PackageCmd(ruckcommand.RuckCommand):

    def is_installed(self, p):
        pkcon = self.pkcon()

        (n,a,e,v,r) = p.pkgtup
        # FIXME: this only searches name, not full naevr
        matches = pkcon.search_name(pkcon.PK_FILTER_INSTALLED, n)
        return len(matches) > 0

    def find_available_packages(self, list):
        yum = self.yum()

        exactmatches, matches, unmatched = self.find_packages(list)

        if len(unmatched) > 0:
            raise Exception("Could not find package '%s'" % unmatched[0])

        installable = unique(exactmatches + matches)
        archlist = yum.conf.exactarchlist

        installs = {}
        updates = {}

        for pkg in installable:
            if yum.rpmdb.installed(po=pkg):
                continue

            # everything installed that matches the name
            installedByKey = yum.rpmdb.searchNevra(name=pkg.name)
            comparable = []
            for instpo in installedByKey:
                (n2, a2, e2, v2, r2) = instpo.pkgtup
                if rpmUtils.arch.isMultiLibArch(a2) == rpmUtils.arch.isMultiLibArch(pkg.arch):
                    comparable.append(instpo)

            # go through each package
            if len(comparable) > 0:
                for instpo in comparable:
                    if pkg > instpo: # we're newer - this is an update, pass to them
                        if instpo.name in archlist:
                            if pkg.arch == instpo.arch:
                                if not updates.has_key(pkg.name):
                                    updates[pkg.name] = []

                                updates[pkg.name].append(pkg)
                        else:
                            if not updates.has_key(pkg.name):
                                updates[pkg.name] = []

                            updates[pkg.name].append(pkg)
                    elif pkg == instpo: # same, ignore
                        continue
            else: # we've not got any installed that match n or n+a
                if not installs.has_key(pkg.name):
                    installs[pkg.name] = []

                installs[pkg.name].append(pkg)

        pkglist = []
        for name in installs.keys():
            pkglist.extend(yum.bestPackagesFromList(installs[name]))

        installs = pkglist

        pkglist = []
        for name in updates.keys():
            pkglist.extend(yum.bestPackagesFromList(updates[name]))

        updates = pkglist

        return installs, updates

    def filter_installs(self, list):
        uninstalled = []
        installed = []

        for p in list:
            if self.is_installed(p):
                installed.append(p)
            else:
                uninstalled.append(p)

        return uninstalled, installed

    def find_packages(self, list, installed=False):
        yum = self.yum()

        if installed:
            avail = yum.rpmdb.returnPackages()
        else:
            avail = yum.pkgSack.returnPackages()

        return self.parsePackages(avail, list, casematch=0)

    def buildPkgRefDict(self, pkgs):
        """take a list of pkg objects and return a dict the contains all the possible
           naming conventions for them eg: for (name,i386,0,1,1)
           dict[name] = (name, i386, 0, 1, 1)
           dict[name.i386] = (name, i386, 0, 1, 1)
           dict[name-1-1.i386] = (name, i386, 0, 1, 1)
           dict[name-1] = (name, i386, 0, 1, 1)
           dict[name-1-1] = (name, i386, 0, 1, 1)
           dict[0:name-1-1.i386] = (name, i386, 0, 1, 1)
           dict[name-0:1-1.i386] = (name, i386, 0, 1, 1)
           """
        pkgdict = {}
        for pkg in pkgs:
            pkgtup = (pkg.name, pkg.arch, pkg.epoch, pkg.version, pkg.release)
            (n, a, e, v, r) = pkgtup
            name = n
            nameArch = '%s.%s' % (n, a)
            nameVerRelArch = '%s-%s-%s.%s' % (n, v, r, a)
            nameVer = '%s-%s' % (n, v)
            nameVerRel = '%s-%s-%s' % (n, v, r)
            envra = '%s:%s-%s-%s.%s' % (e, n, v, r, a)
            nevra = '%s-%s:%s-%s.%s' % (n, e, v, r, a)
            repoName = '%s:%s' % (pkg.repoid, n)
            repoNameArch = '%s:%s.%s' % (pkg.repoid, n, a)

            for item in [name, nameArch, nameVerRelArch, nameVer, nameVerRel, envra, nevra, repoName, repoNameArch]:
                if not pkgdict.has_key(item):
                    pkgdict[item] = []
                pkgdict[item].append(pkg)

        return pkgdict

    def parsePackages(self, pkgs, usercommands, casematch=0):
        pkgdict = self.buildPkgRefDict(pkgs)
        exactmatch = []
        matched = []
        unmatched = []
        for command in usercommands:
            if pkgdict.has_key(command):
                exactmatch.extend(pkgdict[command])
                del pkgdict[command]
            else:
                # anything we couldn't find a match for
                # could mean it's not there, could mean it's a wildcard
                if re.match('.*[\*,\[,\],\{,\},\?].*', command):
                    trylist = pkgdict.keys()
                    restring = fnmatch.translate(command)
                    if casematch:
                        regex = re.compile(restring) # case sensitive
                    else:
                        regex = re.compile(restring, flags=re.I) # case insensitive
                    foundit = 0
                    for item in trylist:
                        if regex.match(item):
                            matched.extend(pkgdict[item])
                            del pkgdict[item]
                            foundit = 1

                    if not foundit:
                        unmatched.append(command)

                else:
                    # we got nada
                    unmatched.append(command)

        matched = unique(matched)
        unmatched = unique(unmatched)
        exactmatch = unique(exactmatch)
        return exactmatch, matched, unmatched

    def get_package(self, pkg_tuple):
        yum = self.yum()

        (n,a,e,v,r) = pkg_tuple
        matches = yum.pkgSack.searchNevra(name=n, arch=a, epoch=e,
                                          ver=v, rel=r)
        return matches[0]

    def get_updates(self, repo=None):
        yum = self.yum()

        yum.doRpmDBSetup()
        yum.doUpdateSetup()

        updates = yum.up.updating_dict

        tuples = []

        for new in updates:
            (n,a,e,v,r) = new
            matches = yum.pkgSack.searchNevra(name=n, arch=a, epoch=e,
                                              ver=v, rel=r)
            new_pkg = matches[0]
            if repo and new_pkg.repoid != repo:
                continue

            (n,a,e,v,r) = updates[new][0]
            matches = yum.rpmdb.searchNevra(name=n, arch=a, epoch=e,
                                            ver=v, rel=r)
            old_pkg = matches[0]

            tuples.append((new_pkg, old_pkg))

        return tuples


    def resolve_deps(self):
        rucktalk.message('Resolving dependencies...')
        yum = self.yum()

        return yum.buildTransaction()

    def show_ts_list(self, items, isdep=False):
        for pkg in items:
            msg = "  " + ruckformat.package_to_str(pkg)
            if isdep:
                msg += " (dependency)"
            rucktalk.message(msg)

    def show_ts_packages(self):
        yum = self.yum()

        yum.tsInfo.makelists()

        list = yum.tsInfo.installed
        dep_list = yum.tsInfo.depinstalled
        if len(list) > 0 or len(dep_list) > 0:
            rucktalk.message('The following packages will be installed:')
            self.show_ts_list(list)
            self.show_ts_list(dep_list, True)

        list = yum.tsInfo.updated
        dep_list = yum.tsInfo.depupdated
        if len(list) > 0 or len(dep_list) > 0:
            rucktalk.message('The following packages will be upgraded:')
            self.show_ts_list(list)
            self.show_ts_list(dep_list, True)

        list = yum.tsInfo.removed
        dep_list = yum.tsInfo.depremoved
        if len(list) > 0 or len(dep_list) > 0:
            rucktalk.message('The following packages will be removed:')
            self.show_ts_list(list)
            self.show_ts_list(dep_list, True)

    def get_pkgs_to_download(self):
        yum = self.yum()

        downloadpkgs = []
        for txmbr in yum.tsInfo.getMembers():
            if txmbr.ts_state in ['i', 'u']:
                po = txmbr.po
                if po:
                    downloadpkgs.append(po)

        return downloadpkgs

    def parse_dep_str(self, dep_str):
        ret = {}

        info = string.split(dep_str)
        info_len = len(info)
        if info_len == 1:
            ret["dep"] = dep_str
            return ret
        elif info_len != 3:
            raise Exception("Invalid dep string")

        valid_relations = ["=", "<", "<=", ">", ">=", "!="]

        if not info[1] in valid_relations:
            raise Exception("Invalid relation %s" % info[1])

        ret["dep"] = info[0]
        ret["relation"] = info[1]

        version_regex = re.compile("^(?:(\d+):)?(.*?)(?:-([^-]+))?$")
        match = version_regex.match(info[2])

        if match.group(1):
            ret["has_epoch"] = 1
            ret["epoch"] = int(match.group(1))
        else:
            ret["has_epoch"] = 0
            ret["epoch"] = 0

        ret["version"] = match.group(2)

        if match.group(3):
            ret["release"] = match.group(3)
        else:
            ret["release"] = ""

        return ret

    def need_prompt(self):

        yum = self.yum()

        # prompt if:
        #  package was added to fill a dependency
        #  package is being removed
        #  package wasn't explictly given on the command line
        for txmbr in yum.tsInfo.getMembers():
            if txmbr.isDep or txmbr.ts_state == 'e':
                return True

        return False

    def gpgsigcheck(self, pkgs):
        yum = self.yum()

        for p in pkgs:
            result, errmsg = yum.sigCheckPkg(p)
            if result == 0:
                # woo!
                pass
            elif result == 1:
                # FIXME: need to download gpg
                rucktalk.error("Ignoring missing gpg key.")
                pass
            else:
                yumtalk.error(errmsg)
                return False

        return True

    def tsInfo_is_empty(self, tsInfo):
        return len(tsInfo.installed) == 0 and \
               len(tsInfo.depinstalled) == 0 and \
               len(tsInfo.updated) == 0 and \
               len(tsInfo.depupdated) == 0 and \
               len(tsInfo.removed) == 0 and \
               len(tsInfo.depremoved) == 0

    def start_transaction(self, dryrun=False, download_only=False):
        yum = self.yum()

        if not download_only:
            (rescode, resmsgs) = self.resolve_deps()
            if rescode != 2:
                for resmsg in resmsgs:
                    rucktalk.error(resmsg)

                return False

        self.show_ts_packages()

        if self.tsInfo_is_empty(yum.tsInfo):
            rucktalk.warning("Nothing to do.")
            return False

        downloadpkgs = self.get_pkgs_to_download()

        if len(downloadpkgs) > 0:
            total_size = 0

            for p in downloadpkgs:
                try:
                    size = int(p.size())
                    total_size += size
                except:
                    pass

            if total_size > 0:
                rucktalk.message("\nTotal download size: %s\n" % (ruckformat.bytes_to_str(total_size)))

        if self.need_prompt():
            answer = raw_input('\nProceed with transaction? (y/N) ')
            if len(answer) != 1 or answer[0] != 'y':
                rucktalk.message('Transaction Canceled')
                return False

        problems = yum.downloadPkgs(downloadpkgs)
        if len(problems.keys()) > 0:
            rucktalk.error("Error downloading packages:")
            for key in problems.keys():
                for error in unique(problems[key]):
                    rucktalk.message("  %s: %s" % (key, error))
            return False

        if download_only:
            for pkg in downloadpkgs:
                dest = ruckformat.package_to_str(pkg, repo=False) + ".rpm"
                shutil.move(pkg.localpath, dest)
                rucktalk.message ("Downloaded '%s'" % dest)

            return True

        # Check GPG signatures
        if not self.gpgsigcheck(downloadpkgs):
            return False

        tsConf = {}
        for feature in ['diskspacecheck']: # more to come, I'm sure
                tsConf[feature] = getattr(yum.conf, feature)

        if dryrun:
            testcb = ruckyum.RPMInstallCallback(output=1)
            testcb.tsInfo = yum.tsInfo
            # clean out the ts b/c we have to give it new paths to the rpms
            del yum.ts

            yum.initActionTs()
            yum.populateTs(keepold=0) # sigh
            tserrors = yum.ts.test(testcb, conf=tsConf)
            del testcb

            if len(tserrors) > 0:
                errstring = ''
                for descr in tserrors:
                    errstring += '  %s\n' % descr

                rucktalk.error(errstring)
                return False

        else:
            rucktalk.message('Running Transaction Test')

            testcb = ruckyum.RPMInstallCallback(output=0)
            testcb.tsInfo = yum.tsInfo
            # clean out the ts b/c we have to give it new paths to the rpms
            del yum.ts

            yum.initActionTs()
            # save our dsCallback out
            dscb = yum.dsCallback
            yum.dsCallback = None # dumb, dumb dumb dumb!
            yum.populateTs(keepold=0) # sigh
            tserrors = yum.ts.test(testcb, conf=tsConf)
            del testcb

            if len(tserrors) > 0:
                errstring = 'Transaction Check Error: '
                for descr in tserrors:
                    errstring += '  %s\n' % descr

                rucktalk.error(errstring)
                return False

            rucktalk.message('Transaction Test Succeeded\n')
            del yum.ts

            yum.initActionTs() # make a new, blank ts to populate
            yum.populateTs(keepold=0) # populate the ts
            yum.ts.check() #required for ordering
            yum.ts.order() # order

            # put back our depcheck callback
            yum.dsCallback = dscb

            cb = ruckyum.RPMInstallCallback(output=1)
            cb.tsInfo = yum.tsInfo

            yum.runTransaction(cb=cb)

        rucktalk.message('\nTransaction Finished')
        return True

class PackagesCmd(PackageCmd):

    def name(self):
        return "packages"

    def aliases(self):
        return ["pa"]

    def category(self):
        return "basic"

    def is_basic(self):
        return 1

    def arguments(self):
        return ""

    def description_short(self):
        return "List all packages"

    def category(self):
        return "package"

    def local_opt_table(self):
        return [["",  "no-abbrev", "", "Do not abbreviate channel or version information"],
                ["i", "installed-only", "", "Show only installed packages"],
                ["u", "uninstalled-only", "", "Show only uninstalled packages"],
                ["",  "sort-by-name", "", "Sort packages by name (default)"],
                ["",  "sort-by-repo", "", "Sort packages by repository"]]

    def local_orthogonal_opts(self):
        return [["installed-only", "uninstalled-only"]]

    def execute(self, options_dict, non_option_args):
        # FIXME: does not know about status, not sure all is right default
        pkcon = self.pkcon()

        table_rows = []
        no_abbrev = options_dict.has_key("no-abbrev")

        sort_idx = 2
        table_headers = ["S", "Repository", "Name", "Version"]
        table_keys = ["installed", "repo", "name", "version"]

        filter = pkenums.FILTER_NEWEST
        if options_dict.has_key("uninstalled-only"):
            filter = filter + pkenums.FILTER_NOT_INSTALLED
        elif options_dict.has_key("installed-only"):
            filter = pkenums.FILTER_INSTALLED

        filter = pkenums.FILTER_INSTALLED
        pkgs = pkcon.get_packages(filter)

        remote_tuples = {}

        for p in pkgs:
            row = ruckformat.package_to_row(p, no_abbrev, table_keys)
            table_rows.append(row)

        if table_rows:
            if options_dict.has_key("sort-by-repo"):
                table_rows.sort(lambda x,y:cmp(string.lower(x[1]), string.lower(y[1])) or\
                                cmp(string.lower(x[2]), string.lower(y[2])))
            else:
                table_rows.sort(lambda x,y:cmp(string.lower(x[sort_idx]), string.lower(y[sort_idx])))
            ruckformat.tabular(table_headers, table_rows)
        else:
            rucktalk.message("--- No packages found ---")


###
### "search" command
###

class PackageSearchCmd(PackageCmd):

    def name(self):
        return "search"

    def aliases(self):
        return ["se"]

    def is_basic(self):
        return 1

    def category(self):
        return "package"

    def arguments(self):
        return "[package name]"

    def description_short(self):
        return "Search packages"

    def local_opt_table(self):
        return [['d', "search-descriptions", '', "Search in package descriptions, as well as package names"],
                ['i', "installed-only", '', "Search only installed packages"],
                ['u', "uninstalled-only", '', "Search only uninstalled packages"],
                ["", "sort-by-name", "", "Sort packages by name (default)"],
                ["", "sort-by-repo", "", "Sort packages by repository, not by name"],
                ["", "no-abbrev",    "", "Do not abbreviate channel or version information"]]

    def execute(self, options_dict, non_option_args):
        pkcon = self.pkcon()

        searchlist = ['name']
        if options_dict.has_key('search-descriptions'):
            method = pkcon.search_details
        else:
            method = pkcon.search_name

        filter = pkenums.FILTER_NONE
        if options_dict.has_key('installed-only'):
            filter = pkenums.FILTER_INSTALLED
        elif options_dict.has_key('uninstalled-only'):
            filter = pkenums.FILTER_NOT_INSTALLED

        result = {}
        matches = method(non_option_args[0], filter)
        if len(matches) > 0:
            table_keys = ["installed", "repo", "name", "version"]
            table_rows = []
            no_abbrev = options_dict.has_key("no-abbrev")

            for pkg in matches:
                row = ruckformat.package_to_row(pkg, no_abbrev, table_keys)
                table_rows.append(row)

            if options_dict.has_key("sort-by-repo"):
                table_rows.sort(lambda x,y:cmp(string.lower(x[1]), string.lower(y[1])) or\
                                cmp(string.lower(x[2]), string.lower(y[2])))
            else:
                table_rows.sort(lambda x,y:cmp(string.lower(x[2]), string.lower(y[2])))
            ruckformat.tabular(["S", "Repository", "Name", "Version"], table_rows)
        else:
            rucktalk.message("--- No packages found ---")



class PackageListUpdatesCmd(PackageCmd):

    def name(self):
        return "list-updates"

    def is_basic(self):
        return 1

    def aliases(self):
        return ["lu"]

    def arguments(self):
        return

    def description_short(self):
        return "List available updates"

    def category(self):
        return "package"

    def local_opt_table(self):
        return [["",  "no-abbrev", "", "Do not abbreviate channel or version information"],
                ["",  "sort-by-name", "", "Sort packages by name (default)"],
                ["",  "sort-by-repo", "", "Sort packages by repository"]]

    def execute(self, options_dict, non_option_args):
        no_abbrev = options_dict.has_key("no-abbrev") or \
                    options_dict.has_key("terse")

        pkcon = self.pkcon()

        table_keys = ["repo", "name", "version"]
        table_rows = []

        updates = pkcon.get_updates()

        for new_pkg in updates:
            row = ruckformat.package_to_row(new_pkg, no_abbrev, table_keys)
            table_rows.append(row)

        if len(table_rows):
            if options_dict.has_key("sort-by-repo"):
                table_rows.sort(lambda x,y:cmp(string.lower(x[0]), string.lower(y[0])) or\
                                cmp(string.lower(x[1]), string.lower(y[1])))
            else:
                table_rows.sort(lambda x,y:cmp(string.lower(x[1]), string.lower(y[1])))

            ruckformat.tabular(["Repository", "Name",
                               "Version"],
                              table_rows)
        else:
            rucktalk.message("--- No updates found ---")

class  PackageUpdatesSummaryCmd(PackageCmd):

    def name(self):
        return "summary"

    def is_basic(self):
        return 1

    def aliases(self):
        return ["sum"]

    def arguments(self):
        return ""

    def description_short(self):
        return "Display a summary of available updates updates"

    def category(self):
        return "package"

    def local_opt_table(self):
        return [["",  "no-abbrev", "", "Do not abbreviate channel or version information"]]

    def execute(self, options_dict, non_option_args):
        pkcon = self.pkcon()

        updates = pkcon.get_updates()
        repos = {}
        for pkg in updates:
            bits = ruckformat.package_to_row(pkg, False, ['repo'])
            if not repos.has_key(bits[0]):
                repos[bits[0]] = [bits[0], 0]
            repos[bits[0]][1] = str(int(repos[bits[0]][1])+1)

        repolist = []
        for repo in repos.keys():
            repolist.append(repos[repo])

        if len(repolist) > 0:
            repolist.sort(lambda x,y:cmp(y[1], x[1]))
            headers = ["Repository", "Total"]
            ruckformat.tabular(headers, repolist)
        else:
            rucktalk.message("--- No updates found ---")


class PackageInfoCmd(ruckcommand.RuckCommand):

    def name(self):
        return "info"

    def is_basic(self):
        return 1

    def aliases(self):
        return ["if"]

    def category(self):
        return "package"

    def arguments(self):
        return "<package-name>"

    def description_short(self):
        return "Show detailed information about a package"

    def execute(self, options_dict, non_option_args):

        if not non_option_args:
            self.usage()
            sys.exit(1)

        pkcon = self.pkcon()

        for a in non_option_args:

            inform = 0
            channel = None
            package = None

            plist = pkcon.resolve("none", a)

            if plist == None or len(plist) == 0:
                rucktalk.message("--- No packages found ---")
                sys.exit(1)

            ## Find the latest version
            latest_ver, latest_id = None, None
            for pkg in plist:
                row = ruckformat.package_to_row(pkg, False, ['version'])
                if latest_ver == None or row[0] > latest_ver:
                    latest_ver= row[0]
                    latest_id = pkg

            latest = pkcon.get_details(latest_id['id'])[0]
            details = ruckformat.package_to_row(latest, False, ['name', 'version', 'repo', 'installed'])
            latest['name'] = details[0]
            latest['version'] = details[1]
            latest['repo'] = details[2]
            latest['installed'] = (details[3] == 'I')

            rucktalk.message("")
            rucktalk.message("Name: " + latest['name'])
            rucktalk.message("Version: " + latest['version'])

            if latest_id['installed']:
                rucktalk.message("Installed: Yes")
            else:
                rucktalk.message("Installed: No")

            rucktalk.message("Package size: " + str(latest['size']))

            rucktalk.message("Group: " + latest['group'])
            rucktalk.message("Homepage: " + latest['url'])
            rucktalk.message("Description: " + latest['detail'])

class PackageFileCmd(ruckcommand.RuckCommand):

    def name(self):
        return "package-file"

    def aliases(self):
        return ["pf"]

    def arguments(self):
        return "<file> ..."

    def is_basic(self):
        return 1

    def category(self):
        return "package"

    def description_short(self):
        return "List packages that own the files provided"

    def execute(self, options_dict, non_option_args):
        size = len(non_option_args)
        if size < 1:
            self.usage()
            return False

        table_rows = []
        yum = self.yum()

        for file in non_option_args:
            if not os.access (file, os.F_OK):
                rucktalk.error("File %s does not exist" % file)
                continue

            matches = yum.rpmdb.searchFiles (file);
            if not matches:
                rucktalk.message("No package owns file %s" % file)
                continue

            for pkg in matches:
                row = ruckformat.package_to_row(yum, pkg, False, ["name", "version"])
                if size > 1:
                    row.insert (0, file)
                table_rows.append (row)

        if len(table_rows):
            if size == 1:
                ruckformat.tabular(["Name", "Version"], table_rows)
            else:
                ruckformat.tabular(["File", "Name", "Version"], table_rows)


class PackageFileListCmd(ruckcommand.RuckCommand):

    def name(self):
        return "file-list"

    def aliases(self):
        return ["fl"]

    def arguments(self):
        return "<package>"

    def is_basic(self):
        return 1

    def category(self):
        return "package"

    def description_short(self):
        return "List files within a package"

    def execute(self, options_dict, non_option_args):
        if len(non_option_args) != 1:
            self.usage()
            return False

        yum = self.yum()
        pkg = non_option_args[0]

        matches = yum.rpmdb.searchNevra (name=pkg);
        if not matches:
            yum.doSackFilelistPopulate()
            matches = yum.pkgSack.searchNevra (name=pkg);
            if not matches:
                rucktalk.message("--- No package found ---")

        for p in matches:
            files = None

            # FIXME: returnFileEntries() is always empty for installed
            # packages
            if p.repoid == 'installed':
                files = p.returnSimple('filenames');
            else:
                files = p.returnFileEntries();

            if not files:
                rucktalk.message("--- No files available ---")

            files.sort(lambda x,y:cmp(x,y))
            for file in files:
                rucktalk.message(file)


### Dep commands ###

class PackageDepCmd(PackageCmd):

    def category(self):
        return "package"

    def arguments(self):
        return "<package-dep> ..."

    def is_basic(self):
        return 1

    def local_opt_table(self):
        return [["i", "installed-only", "", "Show only installed packages"],
                ["u", "uninstalled-only", "", "Show only uninstalled packages"]]

    def check_relation(self, di, pkg):
        que_rel = di["relation"]
        eq_list = ['=', '<=', '>=']
        rel_map = {"EQ":"=", "LT":"<", "LE":"<=", "GT":">", "GE":">="}

        for (n, f, (e, v, r)) in pkg.returnPrco(self.dep_type()):
            if di["dep"] != n:
                continue

            # match anything if the package dep doesn't have a relation
            if not f:
                return True

            pkg_rel = rel_map[f]
            result = rpmUtils.miscutils.compareEVR((di["epoch"], di["version"], di["release"]),\
                                                   (e, v, r))

            if result < 0:
                if que_rel in ['!=', '>', '>=']:
                    return True
            elif result > 0:
                if que_rel in ['!=', '<', '<=']:
                    return True
            elif result == 0:
                if que_rel in eq_list and pkg_rel in eq_list:
                    return True

        return False

    def execute(self, options_dict, non_option_args):
        if len(non_option_args) < 1:
            self.usage()
            return False

        table_rows = []
        yum = self.yum()

        for dep in non_option_args:
            dep_info = self.parse_dep_str(dep)

            matches = []
            if not options_dict.has_key('installed-only'):
                matches = self.dep_search_uninstalled (dep_info["dep"]);

            if not options_dict.has_key('uninstalled-only'):
                matches += self.dep_search_installed (dep_info["dep"]);

            if dep_info.has_key("relation"):
                matches = [p for p in matches if self.check_relation (dep_info, p)]

            if not matches:
                rucktalk.message("--- No matches for %s ---" % dep)
                continue

            for pkg in matches:
                row = ruckformat.package_to_row(yum, pkg, False, ["name", "version"])

                repo = pkg.repoid
                if yum.rpmdb.installed(name=pkg.name, arch=pkg.arch):
                    repo = "installed"

                row.insert(0, repo)
                table_rows.append (row)

        if len(table_rows):
            ruckformat.tabular(["Repository", "Name", "Version"], table_rows)


class WhatProvidesCmd(PackageDepCmd):

    def name(self):
        return "what-provides"

    def aliases(self):
        return ["wp"]

    def description_short(self):
        return "List packages that provide what you specify"

    def dep_search_installed(self, name):
        yum = self.yum()
        return yum.rpmdb.searchProvides(name)

    def dep_search_uninstalled(self, name):
        yum = self.yum()
        return yum.pkgSack.searchProvides(name)

    def dep_type(self):
        return "provides"


class WhatRequiresCmd(PackageDepCmd):

    def name(self):
        return "what-requires"

    def aliases(self):
        return ["wr"]

    def category(self):
        return "package"

    def description_short(self):
        return "List packages that require what you specify"

    def dep_search_installed(self, name):
        yum = self.yum()
        return yum.rpmdb.searchRequires(name)

    def dep_search_uninstalled(self, name):
        yum = self.yum()
        return yum.pkgSack.searchRequires(name)

    def dep_type(self):
        return "requires"

class WhatConflictsCmd(PackageDepCmd):

    def name(self):
        return "what-conflicts"

    def aliases(self):
        return ["wc"]

    def category(self):
        return "package"

    def description_short(self):
        return "List packages that conflict with what you specify"

    def dep_search_installed(self, name):
        yum = self.yum()
        return yum.rpmdb.searchConflicts(name)

    def dep_search_uninstalled(self, name):
        yum = self.yum()
        return yum.pkgSack.searchConflicts(name)

    def dep_type(self):
        return "conflicts"

class PackageInfoBaseCmd(PackageCmd):

    def category(self):
        return "package"

    def arguments(self):
        return "<package> ..."

    def is_basic(self):
        return 1

    def local_opt_table(self):
        return [["i", "installed-only", "", "Show only installed packages"],
                ["u", "uninstalled-only", "", "Show only uninstalled packages"]]


    def execute(self, options_dict, non_option_args):
        if len(non_option_args) < 1 or (options_dict.has_key('uninstalled-only') and options_dict.has_key('installed-only')):
            self.usage()
            return False

        table_rows = []
        yum = self.yum()
        dtype = self.dep_type();

        plist = []
        unmatched1 = None
        unmatched2 = None

        if not options_dict.has_key('uninstalled-only'):
            exactmatches, matches, unmatched1 = self.find_packages (non_option_args, installed=True)
            plist = exactmatches + matches

        if not options_dict.has_key('installed-only'):
            exactmatches, matches, unmatched2 = self.find_packages (non_option_args, installed=False)
            plist += exactmatches
            plist += matches

        if (unmatched1 is None or len(unmatched1) > 0) and (unmatched2 is None or len(unmatched2)) > 0:
            if unmatched1 != None:
                arg = unmatched1[0]
            else:
                arg = unmatched2[0]

            rucktalk.error("Could not find package '%s'" % arg)
            return False

        for p in plist:
            rucktalk.message("--- %s ---" % ruckformat.package_to_str(p))
            deps = p.returnPrco(dtype)

            if len(deps) == 0:
                rucktalk.message("\nNo %s found\n" % dtype)
            else:
                for dep in deps:
                    #FIXME: piece of crap prcoPrintable sometimes chokes
                    try:
                        rucktalk.message(p.prcoPrintable(dep))
                    except:
                        pass
                rucktalk.message('')


class PackageInfoProvidesCmd(PackageInfoBaseCmd):

    def name(self):
        return "info-provides"

    def aliases(self):
        return ["ip"]

    def description_short(self):
        return "List a package's provides"

    def dep_type(self):
        return "provides"


class PackageInfoRequiresCmd(PackageInfoBaseCmd):

    def name(self):
        return "info-requires"

    def aliases(self):
        return ["ir"]

    def description_short(self):
        return "List a package's requires"

    def dep_type(self):
        return "requires"

class PackageInfoConflictsCmd(PackageInfoBaseCmd):

    def name(self):
        return "info-conflicts"

    def aliases(self):
        return ["ic"]

    def description_short(self):
        return "List a package's conflicts"

    def dep_type(self):
        return "conflicts"

class PackageInfoObsoletesCmd(PackageInfoBaseCmd):

    def name(self):
        return "info-obsoletes"

    def aliases(self):
        return ["io"]

    def description_short(self):
        return "List a package's obsoletes"

    def dep_type(self):
        return "obsoletes"

class LockListCmd(PackageCmd):

    def name(self):
        return "lock-list"

    def is_basic(self):
        return 1

    def aliases(self):
        return ["ll"]

    def description_short(self):
        return "List locks"

    def category(self):
        return "package"

    def execute(self, options_dict, non_option_args):
        yum = self.yum()

        table_rows = []

        locks = rucklocks.get_locks()

        i = 1
        for (repo, lock) in locks:
            if repo is None:
                repo = ''

            table_rows.append((str(i), repo, lock))
            i += 1

        if len(table_rows):
            ruckformat.tabular(["#", "Repository", "Lock"], table_rows)
        else:
            rucktalk.message("--- No locks found ---")

class LockAddCmd(PackageCmd):

    def name(self):
        return "lock-add"

    def is_basic(self):
        return 1

    def aliases(self):
        return ["la"]

    def description_short(self):
        return "Add a lock"

    def category(self):
        return "package"

    def local_opt_table(self):
        return [["r",  "repo", "repo", "Lock only the given repo"]]

    def execute(self, options_dict, non_option_args):
        if len(non_option_args) != 1:
            self.usage()
            return False

        repo = None
        if options_dict.has_key('repo'):
            repo = options_dict['repo']

        rucklocks.add_lock(non_option_args[0], repo=repo)
        rucktalk.message("--- Lock successfully added ---")


class LockRemoveCmd(PackageCmd):

    def name(self):
        return "lock-remove"

    def is_basic(self):
        return 1

    def aliases(self):
        return ["lr"]

    def description_short(self):
        return "Remove a lock"

    def category(self):
        return "package"

    def execute(self, options_dict, non_option_args):
        if len(non_option_args) != 1:
            self.usage()
            return False

        locks = rucklocks.get_locks()
        i = int(non_option_args[0]) - 1

        if i >= len(locks):
            rucktalk.error("Invalid lock %s" % str(i + 1))
            return False

        rucklocks.remove_lock(i)
        rucktalk.message("--- Lock successfully removed ---")


class OrphansCmd(PackageCmd):

    def name(self):
        return "orphans"

    def is_basic(self):
        return 1

    def aliases(self):
        return ["or"]

    def description_short(self):
        return "List installed packages that don't exist in repositories"

    def category(self):
        return "package"

    def execute(self, options_dict, non_option_args):
        if len(non_option_args) != 0:
            self.usage()
            return False

        table_headers = ["Name", "Version"]
        table_keys = ["name", "version"]
        table_rows = []

        no_abbrev = options_dict.has_key("no-abbrev")

        yum = self.yum()

        for installed in yum.rpmdb.returnPackages():
            matches = yum.pkgSack.searchNevra(name=installed.name)

            if len(matches) == 0:
                table_rows.append(ruckformat.package_to_row(yum, installed, no_abbrev, table_keys))

        if len(table_rows) > 0:
            ruckformat.tabular(table_headers, table_rows)
        else:
            rucktalk.message('--- No orphans found ---')

if not vars().has_key('registered'):
#    ruckcommand.register(PackageFileCmd)
    ruckcommand.register(PackagesCmd)
    ruckcommand.register(PackageSearchCmd)
    ruckcommand.register(PackageListUpdatesCmd)
    ruckcommand.register(PackageInfoCmd)
#    ruckcommand.register(WhatProvidesCmd)
#    ruckcommand.register(WhatRequiresCmd)
#    ruckcommand.register(WhatConflictsCmd)
#    ruckcommand.register(PackageInfoProvidesCmd)
#    ruckcommand.register(PackageInfoRequiresCmd)
#    ruckcommand.register(PackageInfoConflictsCmd)
#    ruckcommand.register(PackageInfoObsoletesCmd)
#    ruckcommand.register(PackageFileListCmd)
#    ruckcommand.register(LockListCmd)
#    ruckcommand.register(LockAddCmd)
#    ruckcommand.register(LockRemoveCmd)
#    ruckcommand.register(OrphansCmd)
#    ruckcommand.register(PackageUpdatesSummaryCmd)
    registered = True
