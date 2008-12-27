#!/usr/bin/python

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
import string
import getpass
import os
import traceback

import rucktalk
import ruckcommand

import urlgrabber
import urlgrabber.grabber

ruck_name = "Red Carpet Command Line Client"
ruck_copyright = "Copyright (C) 2000-2003 Ximian Inc.  All Rights Reserved."
ruck_version = None

def import_commands(ruck_dir):
    import glob, imp
    sysdir = ruck_dir + "/commands"
    sys.path.append(sysdir)

    loaded_modules = []

    # First load modules in our current directory, for developers, and then
    # out of the system dir.
    files = glob.glob("*cmds.py")
    files = files + glob.glob("%s/*cmds.py" % sysdir)

    for file in files:
        (path, name) = os.path.split(file)
        (name, ext) = os.path.splitext(name)

        if name in loaded_modules:
            continue

        (file, filename, data) = imp.find_module(name, [path])

        try:
            module = imp.load_module(name, file, filename, data)
        except ImportError:
            rucktalk.warning("Can't import module " + filename)
        else:
            loaded_modules.append(name)

        if file:
            file.close()

def show_exception(e):
    if rucktalk.show_verbose:
        trace = ""
        exception = ""
        exc_list = traceback.format_exception_only (sys.exc_type, sys.exc_value)
        for entry in exc_list:
            exception += entry
            tb_list = traceback.format_tb(sys.exc_info()[2])
            for entry in tb_list:
                trace += entry

        rucktalk.error(str(e))
        rucktalk.error(trace)
    else:
        rucktalk.error(str(e))

def main(ver, ruck_dir):

    global local
    global ruck_version

    ruck_version = ver

    if os.environ.has_key("RUCK_DEBUG"):
        rucktalk.show_debug = 1

    import rucklocks
    rucklocks.init()

    import_commands(ruck_dir)

    ###
    ### Grab the option list and extract the first non-option argument that
    ### looks like a command.  This could get weird if someone passes the name
    ### of a command as the argument to an option.
    ###

    argv = sys.argv[1:]

    argv = ruckcommand.expand_synthetic_args(argv)

    if "--version" in argv:
        print
        print ruck_name + " " + ruck_version
        print ruck_copyright
        print
        sys.exit(0)

    command = ruckcommand.extract_command_from_argv(argv)

    if "-?" in argv or "--help" in argv:
        command.usage()
        sys.exit(0)

    # A hack to suppress extra whitespace when dumping.
    if command.name() == "dump":
        rucktalk.be_terse = 1

    argv = ruckcommand.get_user_default_args(argv, command)

    opt_dict, args = command.process_argv(argv)

    ###
    ### Control verbosity
    ###

    if opt_dict.has_key("terse"):
        rucktalk.be_terse = 1

    if opt_dict.has_key("quiet"):
        rucktalk.show_messages = 0
        rucktalk.show_warnings = 0

    if opt_dict.has_key("verbose"):
        rucktalk.show_verbose = 1

    ### Whitespace is nice, so we always print a blank line before
    ### executing the command

    if not rucktalk.be_terse:
        rucktalk.message("")

    if opt_dict.has_key("cache-only") or os.getuid() != 0:
        command.cache_only = True
    elif opt_dict.has_key("no-plugins"):
        command.no_plugins = True

    try:
        command.execute(opt_dict, args)
    except IOError, e:
        if e.errno == 13:
            rucktalk.error("You must be root to execute this command")
        else:
            show_exception(e)

        sys.exit(1)
    except Exception, e:
        show_exception(e)
        sys.exit(1)

    ### Whitespace is nice, so we always print a blank line after
    ### executing the command

    if not rucktalk.be_terse:
        rucktalk.message("")



