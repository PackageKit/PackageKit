#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Provides an apt backend to PackageKit

Copyright (C) 2007 Ali Sabil <ali.sabil@gmail.com>
Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
Copyright (C) 2008-2009 Sebastian Heinlein <glatzor@ubuntu.com>
Copyright (C) 2010 Daniel Nicoletti <dantti12@gmail.com>

Licensed under the GNU General Public License Version 2

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

import locale
import logging
import optparse
import time

from packagekit.backend import *

logging.basicConfig(format="%(levelname)s:%(message)s")
pklog = logging.getLogger("PackageKitBackend")
pklog.setLevel(logging.NOTSET)

# Check if update-manager-core is installed to get aware of the
# latest distro releases
try:
    from UpdateManager.Core.MetaRelease import MetaReleaseCore
except ImportError:
    META_RELEASE_SUPPORT = False
else:
    META_RELEASE_SUPPORT = True


DEFAULT_ENCODING = "UTF-8"

# Required to get translated descriptions
try:
    locale.setlocale(locale.LC_ALL, "")
except locale.Error:
    pklog.debug("Failed to unset LC_ALL")

# Required to parse RFC822 time stamps
try:
    locale.setlocale(locale.LC_TIME, "C")
except locale.Error:
    pklog.debug("Failed to unset LC_TIME")

class PackageKitAptccBackend(PackageKitBaseBackend):
    """
    PackageKit backend for aptcc
    """
    # Methods ( client -> engine -> backend )
    def get_distro_upgrades(self):
        """
        Implement the {backend}-get-distro-upgrades functionality
        """
        pklog.info("Get distro upgrades")
        self.status(STATUS_INFO)
        self.allow_cancel(False)
        self.percentage(None)

        if META_RELEASE_SUPPORT == False:
            return

        #FIXME Evil to start the download during init
        meta_release = MetaReleaseCore(False, False)
        #FIXME: should use a lock
        while meta_release.downloading:
            time.sleep(1)
        #FIXME: Add support for description
        if meta_release.new_dist != None:
            self.distro_upgrade(DISTRO_UPGRADE_STABLE,
                                meta_release.new_dist.name,
                                 "%s %s" % (meta_release.new_dist.name,
                                            meta_release.new_dist.version))



    def _sigquit(self, signum, frame):
        self._unlock_cache()
        sys.exit(1)

def debug_exception(type, value, tb):
    """
    Provides an interactive debugging session on unhandled exceptions
    See http://aspn.activestate.com/ASPN/Cookbook/Python/Recipe/65287
    """
    if hasattr(sys, 'ps1') or not sys.stderr.isatty() or \
       not sys.stdin.isatty() or not sys.stdout.isatty() or type==SyntaxError:
        # Calls the default handler in interactive mode, if output is·
        # redirected or on syntax errors
        sys.__excepthook__(type, value, tb)
    else:
        import traceback, pdb
        traceback.print_exception(type, value, tb)
        print()
        pdb.pm()

def run(args, single=False):
    """
    Start the apt backend
    """
    backend = PackageKitAptccBackend("")
    if single == True:
        backend.dispatch_command(args[0], args[1:])
    else:
        backend.dispatcher(args)

def main():
    parser = optparse.OptionParser(description="Aptcc backend for PackageKit")
    parser.add_option("-r", "--root",
                      action="store", type="string", dest="root",
                      help="Use the given directory as the system root "
                           "(Only needed by developers)")
    parser.add_option("-p", "--profile",
                      action="store", type="string", dest="profile",
                      help="Store profiling stats in the given file "
                           "(Only needed by developers)")
    parser.add_option("-d", "--debug",
                      action="store_true", dest="debug",
                      help="Show a lot of additional information and drop to "
                           "a debugging console on unhandled exceptions "
                           "(Only needed by developers)")
    parser.add_option("-s", "--single",
                      action="store_true", dest="single",
                      help="Only perform one command and don't listen on stdin "
                           "(Only needed by developers)")
    (options, args) = parser.parse_args()
    if options.debug:
        pklog.setLevel(logging.DEBUG)
        sys.excepthook = debug_exception

    run(args, options.single)

if __name__ == '__main__':
    main()

# vim: ts=4 et sts=4
