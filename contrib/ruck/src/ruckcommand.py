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
import getopt
import string
import ruckformat
import rucktalk
from packagekit import client

import logging

default_opt_table = [
    ["",  "version",  "",         "Print client version and exit"],
    ["V", "verbose",  "",         "Verbose output"],
    ["p", "no-plugins",  "",      "Don't load yum plugins"],
    ["C", "cache-only", "",       "Run entirely from cache, don't update cache"],
    ["",  "normal-output", "",    "Normal output (default)"],
    ["t", "terse",    "",         "Terse output"],
    ["",  "quiet",    "",         "Quiet output, print only error messages"],
    ["",  "read-from-file", "filename",   "Get args from file"],
    ["",  "read-from-stdin", "",  "Get args from stdin"],
    ["",  "ignore-rc-file", "",   "Don't read ruck's startup file (~/.ruckrc)"],
    ["",  "ignore-env", "",       "Ignore the RUCK_ARGS environment variable"],
    ["?", "help",     "",         "Get help on a specific command"]
]

default_orthogonal_opts = [["verbose", "no-plugins", "terse", "normal-output", "quiet"]]


command_dict = {}
alias_dict = {}


def register(constructor):
    obj = constructor()
    name = obj.name()
    aliases = obj.aliases()
    hidden = obj.is_hidden()
    basic = obj.is_basic()
    description = obj.description_short() or "<No Description Available>"
    category = obj.category()

    if command_dict.has_key(name):
        rucktalk.error("Command name collision: '"+name+"'")
    else:
        command_dict[name] = (description, constructor, aliases, hidden, basic, category)

    for a in aliases:
        al = string.lower(a)
        if command_dict.has_key(al):
            rucktalk.error("Command/alias collision: '"+a+"'")
        elif alias_dict.has_key(al):
            rucktalk.error("Alias collision: '"+a+"'")
        else:
            alias_dict[al] = name


def construct(name):
    nl = string.lower(name)

    if alias_dict.has_key(nl):
        nl = alias_dict[nl]

    if not command_dict.has_key(nl):
        return None

    cons = command_dict[nl][1]

    return cons()

# It seems bad that this is hard-wired here, but I don't really see
# any cleaner way around it
category_data = {"package":["Package management", -1000],
                 "dependency":["Dependency analysis", 0],
                 "user":["User management", 0],
                 "prefs":["Preference management", 0],
                 "service":["Service management", 0],
                 "other":["Other", 10000000]}

def command_sort(a, b):

    a_name = string.lower(a[0])
    b_name = string.lower(b[0])

    a_cat = string.lower(a[2])
    b_cat = string.lower(b[2])

    if category_data.has_key(a_cat):
        a_catcode = category_data[a_cat][1]
    else:
        a_catcode = 0

    if category_data.has_key(b_cat):
        b_catcode = category_data[b_cat][1]
    else:
        b_catcode = 0

    return cmp(a_catcode, b_catcode) or cmp(a_cat, b_cat) or cmp(a_name, b_name)

def print_command_list(commands, with_categories=0):

    max_len = 0
    cmd_list = []

    for c in commands:
        name, aliases, description, category = c

        if aliases:
            name = name + " (" + string.join(aliases, ", ") + ")"

        cmd_list.append([name, description, category])
        max_len = max(max_len, len(name))

    desc_len = max_len + 4

    cmd_list.sort(command_sort)

    previous_category = "we will banish all dwarves from the love kingdom"

    for c in cmd_list:

        name, description, category = c

        if with_categories and category != previous_category:
            if category_data.has_key(category):
                category_name = category_data[category][0]
            else:
                category_name = string.upper(category[0]) + category[1:]

            rucktalk.message("\n" + category_name + " commands:")
            previous_category = category

        # If, for some reason, the command list is *really* wide (which it never should
        # be), don't do something stupid.
        if 79 - desc_len > 10:
            desc = ruckformat.linebreak(description, 79-desc_len)
        else:
            desc = [description]

        desc_first = desc.pop(0)
        rucktalk.message("  " + string.ljust(name, max_len) + "  " + desc_first)
        for d in desc:
            rucktalk.message(" " * desc_len + d)

def usage_basic():
    rucktalk.message("Usage: ruck <command> <options> ...")
    rucktalk.message("")

    keys = command_dict.keys()

    if keys:
        keys.sort()
        command_list = []
        for k in keys:
            description, constructor, aliases, hidden, basic, category  = command_dict[k]
            if not hidden and basic:
                command_list.append([k, aliases, description, category])

        rucktalk.message("Some basic commands are:")
        print_command_list(command_list)

        rucktalk.message("")
        rucktalk.message("For a more complete list of commands and important options,")
        rucktalk.message("run \"ruck help\".")
        rucktalk.message("")

    else:
        rucktalk.error("<< No commands found --- something is wrong! >>")

def usage_full():
    rucktalk.message("Usage: ruck <command> <options> ...")
    rucktalk.message("")

    rucktalk.message("The following options are understood by all commands:")
    ruckformat.opt_table(default_opt_table)

    keys = command_dict.keys()

    if keys:
        command_list = []
        for k in keys:
            description, constructor, aliases, hidden, basic, category  = command_dict[k]
            if not hidden:
                command_list.append([k, aliases, description, category])

        print_command_list(command_list, with_categories=1)

        rucktalk.message("")
        rucktalk.message("For more detailed information about a specific command,")
        rucktalk.message("run 'ruck <command name> --help'.")
        rucktalk.message("")

    else:
        rucktalk.error("<< No commands found --- something is wrong! >>")


def extract_command_from_argv(argv):
    command = None
    i = 0
    unknown_commands = []
    while i < len(argv) and not command:
        if argv[i][0] != "-":
            command = construct(argv[i])
            if command:
                argv.pop(i)
            else:
                unknown_commands.append(argv[i])
        else:
            takes_arg = 0
            for o in default_opt_table:
                if not (argv[i][1:] == o[0] or argv[i][2:] == o[1]):
                    continue

                if o[2] != "":
                    takes_arg = 1
                    break

            if takes_arg and string.find(argv[i], "=") == -1:
                i = i + 1

        i = i + 1

    if not command:
        map(lambda x:rucktalk.warning("Unknown command '%s'" % x),
            unknown_commands)
        rucktalk.warning("No command found on command line.")
        if "--help" in argv or "-?" in argv:
            usage_full()
        else:
            usage_basic()
        sys.exit(1)

    return command

###
### Handle .ruckrc and RUCK_ARGS
###

def get_user_default_args(argv, command_name):

    # We add arguments to the beginning of argv.  This means we can
    # override an magically added arg by explicitly putting the
    # orthogonal arg on the command line.
    def join_args(arglist, argv):
        return map(string.strip, arglist + argv)

    # Try to read the .ruckrc file.  It basically works like a .cvsrc file.
    if "--ignore-rc-file" not in argv:
        ruckrc_files = (os.path.expanduser("~/.ruckrc"),
                       os.path.expanduser("~/.rcrc"))

        rc_file = None
        for f in ruckrc_files:
            if os.path.exists(f):
                rc_file = f
                break

        if rc_file:
            try:
                ruckrc = open(rc_file, "r")
                while 1:
                    line = ruckrc.readline()

                    # strip out comments
                    hash_pos = string.find(line, "#")
                    if hash_pos >= 0:
                        line = line[0:hash_pos]

                    # skip empty lines
                    if not line:
                        break

                    pieces = string.split(line)
                    if len(pieces) and pieces[0] == command_name:
                        argv = join_args(pieces[1:], argv)
                ruckrc.close()

            except IOError:
                # If we can't open the rc file, that is fine... just
                # continue as if nothing happened.
                pass

    if "--ignore-env" not in argv:
        if os.environ.has_key("RUCK_ARGS"):
            args = string.split(os.environ["RUCK_ARGS"])
            argv = join_args(args, argv)

    return argv

###
### Handle --read-from-file and --read-from-stdin
###

def expand_synthetic_args(argv):

    ###
    ### First, walk across our argument list and find any --read-from-file
    ### options.  For each, read the arguments from the file and insert
    ### them directly after the --read-from-file option.
    ###

    i = 0
    is_file_to_read_from = 0
    while i < len(argv):
        arg = argv[i]
        file_to_read = None
        if is_file_to_read_from:
            file_to_read = arg
            is_file_to_read_from = 0
        if arg == "--read-from-file":
            is_file_to_read_from = 1
        elif string.find(arg, "--read-from-file=") == 0:
            file_to_read = arg[len("--read-from-file="):]
            is_file_to_read_from = 0

        if file_to_read:
            lines = []
            try:
                f = open(file_to_read, "r")
                lines = map(string.strip, f.readlines())
            except IOError:
                rucktalk.error("Couldn't open file '%s' to read arguments" % file_to_read)
                sys.exit(1)
            argv = argv[:i] + lines + argv[i+1:]
            i = i + len(lines)

        i = i + 1

    ###
    ### Next, look for --read-from-stdin options.  If there is more than
    ### one on the command line, we split our list of options on blank
    ### lines.
    ###

    rfs_count = argv.count("--read-from-stdin")
    if rfs_count > 0:
        lines = map(string.strip, sys.stdin.readlines())

        i = 0  # position in argv
        j = 0  # position in lines
        while i < len(argv):

            if argv[i] == "--read-from-stdin":

                if j < len(lines):
                    if rfs_count > 1 and "" in lines[j:]:
                        j1 = j + lines[j:].index("")
                        argv = argv[:i+1] + lines[j:j1] + argv[i+1:]
                        j = j1+1
                    else:
                        argv = argv[:i+1] + \
                               filter(lambda x:x!="", lines[j:]) + \
                               argv[i+1:]
                        j = len(lines)


                rfs_count = rfs_count - 1

            i = i + 1

    ###
    ### Finally, we filter our all of those --read-from-* arguments
    ### that we left lying around in argv.
    ###

    argv = filter(lambda x: \
                  string.find(x,"--read-from-file") != 0 \
                  and x != "--read-from-stdin",
                  argv)

    return argv


###
### The actual Ruckcommand class
###

class RuckCommand:

    def __init__(self):
        self.__yum = None
        self.__pkcon = None
        self.cache_only = False
        self.no_plugins = False

    def pkcon(self):
        if not self.__pkcon:
            self.__pkcon = client.PackageKitClient()
        return self.__pkcon

    def name(self):
        return "Unknown!"

    def aliases(self):
        return []

    # If is_hidden returns true, the command will not appear in 'usage'
    # list of available commands.
    def is_hidden(self):
        return 0

    def is_basic(self):
        return 0

    def is_local(self):
        return 0

    def category(self):
        return "other"

    def arguments(self):
        return "..."

    def description_short(self):
        return ""

    def description_long(self):
        return ""

    def default_opt_table(self):
        return default_opt_table

    def local_opt_table(self):
        return []

    def opt_table(self):
        return self.default_opt_table() + self.local_opt_table()


    def default_orthogonal_opts(self):
        return default_orthogonal_opts

    def local_orthogonal_opts(self):
        return []

    def orthogonal_opts(self):
        return self.default_orthogonal_opts() + self.local_orthogonal_opts()


    def usage(self):

        rucktalk.message("")
        rucktalk.message("Usage: ruck " + self.name() + " <options> " + \
                       self.arguments())
        rucktalk.message("")

        description = self.description_long() or self.description_short()
        if description:
            description = "'" + self.name() + "': " + description
            for l in ruckformat.linebreak(description, 72):
                rucktalk.message(l)
            rucktalk.message("")

        opts = self.local_opt_table()
        if opts:
            rucktalk.message("'" + self.name() + "' Options:")
            ruckformat.opt_table(opts)
            rucktalk.message("")

        opts = self.default_opt_table()
        if opts:
            rucktalk.message("General Options:")
            ruckformat.opt_table(opts)
            rucktalk.message("")



    def execute(self, server, options_dict, non_option_args):
        rucktalk.error("Execute not implemented!")
        sys.exit(1)

    def process_argv(self, argv):
        ###
        ### Expand our synthetic args.
        ### Then compile our list of arguments into something that getopt can
        ### understand.  Finally, call getopt on argv and massage the results
        ### in something easy-to-use.
        ###

        argv = get_user_default_args(argv, self.name())

        opt_table = self.opt_table()

        short_opt_getopt = ""
        long_opt_getopt  = []

        short2long_dict = {}

        for o in opt_table:

            short_opt = o[0]
            long_opt  = o[1]
            opt_desc  = o[2]

            if short_opt:

                if short2long_dict.has_key(short_opt):
                    rucktalk.error("Short option collision!")
                    rucktalk.error("-" + short_opt + ", --" + long_opt)
                    rucktalk.error("  vs.")
                    rucktalk.error("-" + short_opt + ", --" + short2long_dict[short_opt])
                    sys.exit(1)

                short2long_dict[short_opt] = long_opt
                short_opt_getopt = short_opt_getopt + short_opt
                if opt_desc:
                    short_opt_getopt = short_opt_getopt + ":"

            if opt_desc:
                long_opt_getopt.append(long_opt + "=")
            else:
                long_opt_getopt.append(long_opt)

        try:
            optlist, args = getopt.getopt(argv, short_opt_getopt, long_opt_getopt)
        except getopt.error:
            did_something = 0
            for a in argv:
                if string.find(a,"--") == 0:
                    if not a[2:] in map(lambda x:x[1], opt_table):
                        rucktalk.error("Invalid argument " + a)
                        did_something = 1
                elif string.find(a, "-") == 0:
                    if not a[1:] in map(lambda x:x[0], opt_table):
                        rucktalk.error("Invalid argument " + a)
                        did_something = 1

            # Just in case something strange went wrong and we weren't
            # able to describe quite why the options parsing failed,
            # we print a catch-all error message.
            if not did_something:
                rucktalk.error("Invalid arguments")

            self.usage()

            sys.exit(1)

        ###
        ### Walk through our list of options and replace short options with the
        ### corresponding long option.
        ###

        i = 0
        while i < len(optlist):
            key = optlist[i][0]
            if key[0:2] != "--":
                optlist[i] = ("--" + short2long_dict[key[1:]], optlist[i][1])
            i = i + 1


        ###
        ### Get the list of "orthogonal" options for this command and, if our
        ### list of options contains orthogonal elements, remove all but the
        ### last such option.
        ### (i.e. if we are handed --quiet --verbose, we drop the --quiet)
        ###

        optlist.reverse()
        for oo_list in self.orthogonal_opts():
            i = 0
            seen_oo = 0
            while i < len(optlist):
                key = optlist[i][0]
                if key[2:] in oo_list:
                    if seen_oo:
                        del optlist[i]
                        i = i - 1
                    seen_oo = 1
                i = i + 1
        optlist.reverse()

        ###
        ### Store our options in a dictionary
        ###

        opt_dict = {}

        for key, value in optlist:
            opt_dict[key[2:]] = value


        return opt_dict, args

class HelpCmd(RuckCommand):

    def name(self):
        return "help"

    def is_basic(self):
        return 1

    def is_local(self):
        return 1

    def description_short(self):
        return "A list of all of the available commands"

    def execute(self, options_dict, non_option_args):
        usage_full()

    def usage(self):
        usage_full()

register(HelpCmd)


class PoopCmd(RuckCommand):

    def name(self):
        return "poop"

    def is_basic(self):
        return 1

    def is_local(self):
        return 1

    def is_hidden(self):
        return 1

    def execute(self, options_dict, non_option_args):
        os.system("figlet POOP")

register(PoopCmd)
