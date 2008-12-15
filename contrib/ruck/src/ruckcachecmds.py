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

class CleanCmd(ruckcommand.RuckCommand):

    def name(self):
        return "clean"

    def aliases(self):
        return ["cl"]

    def is_basic(self):
        return 1

    def category(self):
        return "system"

    def arguments(self):
        return ""

    def description_short(self):
        return "Clean cache"

    def local_opt_table(self):
        return []

    def execute(self, options_dict, non_option_args):
        pkcon = self.pkcon()
        pkcon.refresh_cache(True)
        rucktalk.message('--- Refresh Successful ---')

ruckcommand.register(CleanCmd)


class RefreshCmd(ruckcommand.RuckCommand):

    def name(self):
        return "refresh"

    def aliases(self):
        return ["ref"]

    def is_basic(self):
        return 1

    def category(self):
        return "system"

    def arguments(self):
        return ""

    def description_short(self):
        return "Refresh the package cache"

    def execute(self, options_dict, non_option_args):
        pkcon = self.pkcon()
        pkcon.refresh_cache(False)
        rucktalk.message('--- Refresh Successful ---')


ruckcommand.register(RefreshCmd)

class RepolistCmd(ruckcommand.RuckCommand):

    def name(self):
        return "repos"

    def aliases(self):
        return ["sl", "ca", "ch"]

    def is_basic(self):
        return 1

    def category(self):
        return "system"

    def description_short(self):
        return "List active repositories"

    def local_opt_table(self):
        return [["", "disabled", "", "Show disabled services"]]

    def execute(self, options_dict, non_option_args):
        pkcon = self.pkcon()

        enabled_only = not options_dict.has_key("disabled")
        repos = []
        for repo in pkcon.get_repo_list():
            if enabled_only:
                if repo['enabled']:
                    line = [repo['id'], repo['desc']]
                    repos.append(line)
            else:
                line = [repo['id'], ruckformat.bool_to_short_str(repo['enabled']), repo['desc']]
                repos.append(line)


        repos.sort(lambda x,y:cmp(x[0], y[0]))

        if len(repos):
            headers = ['Name', 'Description']
            if not enabled_only:
                headers.insert(1, 'Enabled')
            ruckformat.tabular(headers, repos)
        else:
            rucktalk.message("--- No repositories found ---")

ruckcommand.register(RepolistCmd)

class RepoCmd(ruckcommand.RuckCommand):

    def name(self):
        return "enable"

    def aliases(self):
        return ["en"]

    def is_basic(self):
        return 1

    def category(self):
        return "system"

    def description_short(self):
        return "Enable a repository"

    def is_hidden(self):
        return 1

    def setenabled(self, repos, val):
        pkcon = self.pkcon()

        for repo in repos:
            pkcon.repo_enable(repo, val)
            if val:
                rucktalk.message("--- Enabled repository '%s' ---" % repo)
            else:
                rucktalk.message("--- Disabled repository '%s' ---" % repo)

class RepoEnableCmd(RepoCmd):

    def name(self):
        return "enable"

    def aliases(self):
        return ["en"]

    def is_basic(self):
        return 1

    def is_hidden(self):
        return 0

    def category(self):
        return "system"

    def description_short(self):
        return "Enable a repository"

    def execute(self, options_dict, non_option_args):
        self.setenabled(non_option_args, True)

ruckcommand.register(RepoEnableCmd)

class RepoDisableCmd(RepoCmd):

    def name(self):
        return "disable"

    def aliases(self):
        return ["di"]

    def is_basic(self):
        return 1

    def is_hidden(self):
        return 0

    def category(self):
        return "system"

    def description_short(self):
        return "Disable a repository"

    def execute(self, options_dict, non_option_args):
        self.setenabled(non_option_args, False)

ruckcommand.register(RepoDisableCmd)

class RepoAddCmd(RepoCmd):

    def name(self):
        return "repo-add"

    def aliases(self):
        return ["sa", "ra"]

    def is_basic(self):
        return 1

    def is_hidden(self):
        return 1

    def category(self):
        return "system"

    def arguments(self):
        return "<name> <url>"

    def description_short(self):
        return "Add a repository"

    def local_opt_table(self):
        return [["c", "check-signatures",  "", "Check gpg signatures"],
                ["n", "name", "name", "Name of the repository"],
                ["m", "mirrorlist", "", "The url specified is a mirror list"]]

    def check_url(self, url):
        if url.startswith('http://') or url.startswith('ftp://') or \
           url.startswith('file://') or url.startswith('https://'):
            return True
        else:
            return False


    def execute(self, options_dict, non_option_args):
        if len(non_option_args) != 2:
            self.usage()
            return False

        repoid = non_option_args[0]
        url = non_option_args[1]

        if not self.check_url(url):
            rucktalk.error("Invalid url '%s'" % url)
            return False

        if self.find_repo_file(repoid) != None:
            rucktalk.error("Repository '%s' already exists" % repoid)
            return False

        yum = self.yum(repos=False)

        repopath = os.path.join(yum.conf.reposdir[0], repoid + ".repo")
        if not os.path.exists(os.path.dirname(repopath)):
            os.makedirs(os.path.dirname(repopath))

        parser = ConfigParser()
        parser.add_section(repoid)

        name = repoid
        if options_dict.has_key('name'):
            name = options_dict['name']

        parser.set(repoid, "name", name)

        if options_dict.has_key('mirrorlist'):
            parser.set(repoid, "mirrorlist", url)
        else:
            parser.set(repoid, "baseurl", url)

        parser.set(repoid, "enabled", "1")

        gpgval = "0"
        if options_dict.has_key('check-signatures'):
            gpgval = "1"

        parser.set(repoid, "gpgcheck", gpgval)



        parser.write(file(repopath, "w+"))

        rucktalk.message("--- Successfully added '%s' ---" % repoid)


ruckcommand.register(RepoAddCmd)

class RepoDelCmd(RepoCmd):

    def name(self):
        return "repo-delete"

    def aliases(self):
        return ["sd", "rd"]

    def is_basic(self):
        return 1

    def is_hidden(self):
        return 1

    def category(self):
        return "system"

    def arguments(self):
        return "<id>"

    def description_short(self):
        return "Remove a repository"

    def execute(self, options_dict, non_option_args):
        if len(non_option_args) != 1:
            self.usage()
            return False

        repoid = non_option_args[0]
        repopath = self.find_repo_file(repoid)
        if repopath == None:
            rucktalk.error("Repository '%s' does not exist" % repoid)
            return False

        parser = ConfigParser()
        parser.read(repopath)
        parser.remove_section(repoid)
        if len(parser.sections()) == 0:
            os.unlink(repopath)
        else:
            parser.write(file(repopath, 'w+'))

        rucktalk.message("--- Successfully removed '%s' ---" % repoid)


ruckcommand.register(RepoDelCmd)
