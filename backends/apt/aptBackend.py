#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Provides an apt backend to PackageKit

Copyright (C) 2007 Ali Sabil <ali.sabil@gmail.com>
Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
Copyright (C) 2008-2009 Sebastian Heinlein <glatzor@ubuntu.com>

Licensed under the GNU General Public License Version 2

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

__author__  = "Sebastian Heinlein <devel@glatzor.de>"

import datetime
import errno
import fcntl
import gdbm
import glob
import gzip
import locale
import logging
import logging.handlers
import optparse
import os
import pty
import re
import signal
import socket
import stat
import string
import subprocess
import sys
import time

import apt
import apt.debfile
import apt_pkg

from packagekit.backend import (PackageKitBaseBackend, format_string)
from packagekit import enums

logging.basicConfig(format="%(levelname)s:%(message)s")
pklog = logging.getLogger("PackageKitBackend")
pklog.setLevel(logging.NOTSET)

try:
    _syslog = logging.handlers.SysLogHandler("/dev/log",
                                      logging.handlers.SysLogHandler.LOG_DAEMON)
    formatter = logging.Formatter('PackageKit: %(levelname)s: %(message)s')
    _syslog.setFormatter(formatter)
    pklog.addHandler(_syslog)
except:
    pass

# Xapian database is optionally used to speed up package description search
XAPIAN_DB_PATH = os.environ.get("AXI_DB_PATH", "/var/lib/apt-xapian-index")
XAPIAN_DB = XAPIAN_DB_PATH + "/index"
XAPIAN_DB_VALUES = XAPIAN_DB_PATH + "/values"
XAPIAN_SUPPORT = False
try:
    import xapian
except ImportError:
    pass
else:
    if os.access(XAPIAN_DB, os.R_OK):
        pklog.debug("Use XAPIAN for the search")
        XAPIAN_SUPPORT = True

# SoftwareProperties is required to proivde information about repositories
try:
    import softwareproperties.SoftwareProperties
except ImportError:
    REPOS_SUPPORT = False
else:
    REPOS_SUPPORT = True

# Check if update-manager-core is installed to get aware of the
# latest distro releases
try:
    from UpdateManager.Core.MetaRelease import MetaReleaseCore
except ImportError:
    META_RELEASE_SUPPORT = False
else:
    META_RELEASE_SUPPORT = True


# Set a timeout for the changelog download
socket.setdefaulttimeout(2)

# Required for daemon mode
os.putenv("PATH",
          "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin")
# Avoid questions from the maintainer scripts as far as possible
os.putenv("DEBIAN_FRONTEND", "noninteractive")
os.putenv("APT_LISTCHANGES_FRONTEND", "none")
os.putenv("APT_LISTBUGS_FRONTEND", "none")
# Force terminal messages in dpkg to be untranslated, status-fd or debconf
# prompts won't be affected
os.putenv("DPKG_UNTRANSLATED_MESSAGES", "1")

# Map Debian sections to the PackageKit group name space
SECTION_GROUP_MAP = {
    "admin" :enums.GROUP_ADMIN_TOOLS,
    "base" :enums.GROUP_SYSTEM,
    "comm" :enums.GROUP_COMMUNICATION,
    "devel" :enums.GROUP_PROGRAMMING,
    "doc" :enums.GROUP_DOCUMENTATION,
    "editors" :enums.GROUP_PUBLISHING,
    "electronics" :enums.GROUP_ELECTRONICS,
    "embedded" :enums.GROUP_SYSTEM,
    "games" :enums.GROUP_GAMES,
    "gnome" :enums.GROUP_DESKTOP_GNOME,
    "graphics" :enums.GROUP_GRAPHICS,
    "hamradio" :enums.GROUP_COMMUNICATION,
    "interpreters" :enums.GROUP_PROGRAMMING,
    "kde" :enums.GROUP_DESKTOP_KDE,
    "libdevel" :enums.GROUP_PROGRAMMING,
    "libs" :enums.GROUP_SYSTEM,
    "mail" :enums.GROUP_INTERNET,
    "math" :enums.GROUP_SCIENCE,
    "misc" :enums.GROUP_OTHER,
    "net" :enums.GROUP_NETWORK,
    "news" :enums.GROUP_INTERNET,
    "oldlibs" :enums.GROUP_LEGACY,
    "otherosfs" :enums.GROUP_SYSTEM,
    "perl" :enums.GROUP_PROGRAMMING,
    "python" :enums.GROUP_PROGRAMMING,
    "science" :enums.GROUP_SCIENCE,
    "shells" :enums.GROUP_SYSTEM,
    "sound" :enums.GROUP_MULTIMEDIA,
    "tex" :enums.GROUP_PUBLISHING,
    "text" :enums.GROUP_PUBLISHING,
    "utils" :enums.GROUP_ACCESSORIES,
    "web" :enums.GROUP_INTERNET,
    "x11" :enums.GROUP_DESKTOP_OTHER,
    "unknown" :enums.GROUP_UNKNOWN,
    "alien" :enums.GROUP_UNKNOWN,
    "translations" :enums.GROUP_LOCALIZATION,
    "metapackages" :enums.GROUP_COLLECTIONS,
    }

# Regular expressions to detect bug numbers in changelogs according to the
# Debian Policy Chapter 4.4. For details see the footnote 16:
# http://www.debian.org/doc/debian-policy/footnotes.html#f16
MATCH_BUG_CLOSES_DEBIAN=r"closes:\s*(?:bug)?\#?\s?\d+(?:,\s*(?:bug)?\#?\s?\d+)*"
MATCH_BUG_NUMBERS=r"\#?\s?(\d+)"
# URL pointing to a bug in the Debian bug tracker
HREF_BUG_DEBIAN="http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=%s"

MATCH_BUG_CLOSES_UBUNTU = r"lp:\s+\#\d+(?:,\s*\#\d+)*"
HREF_BUG_UBUNTU = "https://bugs.launchpad.net/bugs/%s"

# Regular expression to find cve references
MATCH_CVE="CVE-\d{4}-\d{4}"
HREF_CVE="http://web.nvd.nist.gov/view/vuln/detail?vulnId=%s"

SYNAPTIC_PIN_FILE = "/var/lib/synaptic/preferences"

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

def lock_cache(func):
    """Lock the system package cache before excuting the decorated function and
    release the lock afterwards.
    """
    def _locked_cache(*args, **kwargs):
        backend = args[0]
        backend.status(enums.STATUS_WAITING_FOR_LOCK)
        while True:
            try:
                # see if the lock for the download dir can be acquired
                # (work around bug in python-apt/apps that call _fetchArchives)
                lockfile = apt_pkg.config.FindDir("Dir::Cache::Archives") + \
                           "lock"
                lock = apt_pkg.GetLock(lockfile)
                if lock < 0:
                    raise SystemError("failed to lock '%s'" % lockfile)
                else:
                    os.close(lock)
                # then lock the main package system
                apt_pkg.PkgSystemLock()
            except SystemError:
                time.sleep(3)
            else:
                break
        try:
            func(*args, **kwargs)
        finally:
            backend._unlock_cache()
    return _locked_cache


class PKError(Exception):
    pass

class PackageManagerFailedPKError(PKError):
    def __init__(self, msg, pkg, output):
        self.message = msg
        self.package = pkg
        self.output = output

class InstallTimeOutPKError(PKError):
    pass


class DpkgInstallProgress(apt.progress.base.InstallProgress):

    """Class to initiate and monitor installation of local package
    files with dpkg.
    """

    #FIXME: Use the merged DpkgInstallProgress of python-apt
    def recover(self):
        """Run 'dpkg --configure -a'."""
        cmd = ["/usr/bin/dpkg", "--status-fd", str(self.writefd),
               "--root", apt_pkg.config["Dir"],
               "--force-confdef", "--force-confold", 
               "--configure", "-a"]
        self.run(cmd)

    def install(self, filenames):
        """Install the given package using a dpkg command line call."""
        cmd = ["/usr/bin/dpkg", "--force-confdef", "--force-confold",
               "--status-fd", str(self.writefd), 
               "--root", apt_pkg.config["Dir"], "-i"]
        cmd.extend([str(f) for f in filenames])
        self.run(cmd)

    def run(self, cmd):
        """Run and monitor a dpkg command line call."""
        pklog.debug("Executing: %s" % cmd)
        (self.master_fd, slave) = pty.openpty()
        fcntl.fcntl(self.master_fd, fcntl.F_SETFL, os.O_NONBLOCK)
        p = subprocess.Popen(cmd, stdout=slave, stdin=slave)
        self.child_pid = p.pid
        res = self.wait_child()
        return res


class PackageKitOpProgress(apt.progress.base.OpProgress):

    """Handle the cache opening progress."""

    def __init__(self, backend, start=0, end=100, progress=True):
        self._backend = backend
        apt.progress.base.OpProgress.__init__(self)
        self.steps = []
        for val in [0.12, 0.25, 0.50, 0.75, 1.00]:
            step = start + (end - start) * val
            self.steps.append(step)
        self.pstart = float(start)
        self.pend = self.steps.pop(0)
        self.pprev = None
        self.show_progress = progress

    # OpProgress callbacks
    def update(self, percent):
        progress = int(self.pstart + percent / 100 * (self.pend - self.pstart))
        if self.show_progress == True and self.pprev < progress:
            self._backend.percentage(progress)
            self.pprev = progress

    def done(self):
        self.pstart = self.pend
        try:
            self.pend = self.steps.pop(0)
        except:
            pklog.warning("An additional step to open the cache is required")


class PackageKitAcquireProgress(apt.progress.base.AcquireProgress):

    """Handle the package download progress.
    TODO: Add a progress for Updating the cache.
    """

    def __init__(self, backend, start=0, end=100):
        self._backend = backend
        apt.progress.base.AcquireProgress.__init__(self)
        self.start_progress = start
        self.end_progress = end
        self.last_progress = None
        self.last_sub_progress = None
        self.package_states = {}

    def pulse(self, owner):
        #TODO: port to pulse(owner)
        percent = self.current_bytes * 100.0 / self.total_bytes
        progress = int(self.start_progress + percent / 100 *
                       (self.end_progress - self.start_progress))
        # A backwards running progress is reported as a not available progress
        if self.last_progress > progress:
            self._backend.percentage()
        else:
            self._backend.percentage(progress)
            self.last_progress = progress
        for worker in owner.workers:
            if not worker.current_item or not worker.total_size:
                continue
            item_id = "%s;;;" % worker.current_item.shortdesc
            item_percent = worker.current_size * 100 / worker.total_size
            self._backend.item_percentage(item_id, item_percent)
        return True

    def fetch(self, item):
        info = enums.INFO_DOWNLOADING
        try:
            pkg = self._backend._cache[item.shortdesc]
        except:
            self._backend.package("%s;;;" % item.shortdesc, info, "")
        else:
            self._backend._emit_package(pkg, info)

    def start(self):
        self._backend.status(enum.STATUS_DOWNLOAD)
        self._backend.allow_cancel(True)

    def stop(self):
        self._backend.percentage(self.end_progress)
        self._backend.allow_cancel(False)

    def media_change(self, medium, drive):
        #FIXME: Perhaps use hal to show a nicer drive name
        self._backend.media_change_required(enums.MEDIA_TYPE_DISC, medium,
                                            drive)
        # FIXME: We cannot call sys.exit() here. APT module would procduce
        #        a backend error message otherwise. This way the backend
        #        sends another error message in the FetchFailedError handling
        #        later, but this one will be skipped by the daemon
        self._backend.error(enums.ERROR_MEDIA_CHANGE_REQUIRED,
                            "Insert the CDROM or DVD labeled '%s' "
                            "into drive '%s'" % (medium, drive),
                            exit=False)
        return False


class PackageKitAcquireRepoProgress(PackageKitAcquireProgress):

    """Handle the download of of repository information."""

    def pulse(self, owner):
        self._backend.percentage(None)
        #TODO: Emit repositories here
        #for worker in owner.workers:
        #    if not worker.current_item or not worker.total_size:
        #        continue
        #    item_id = "%s;;;" % worker.current_item.shortdesc
        #    item_percent = worker.current_size * 100 / worker.total_size
        #    self._backend.item_percentage(item_id, item_percent)
        return True

    def fetch(self, item):
        pass

    def start(self):
        self._backend.status(enums.STATUS_DOWNLOAD_REPOSITORY)
        self._backend.allow_cancel(True)


class PackageKitInstallProgress(apt.progress.base.InstallProgress):

    """Handle the installation and removal process."""

    def __init__(self, backend, start=0, end=100):
        apt.progress.base.InstallProgress.__init__(self)
        self._backend = backend
        self.pstart = start
        self.pend = end
        self.pprev = None
        self.last_activity = None
        self.conffile_prompts = set()
        # insanly long timeout to be able to kill hanging maintainer scripts
        self.timeout = 10 * 60
        self.start_time = None
        self.output = ""
        self.master_fd = None
        self.child_pid = None
        self.last_pkg = None
        self.last_item_percentage = None

    def status_change(self, pkg_name, percent, status):
        """Callback for APT status updates."""
        self.last_activity = time.time()
        progress = self.pstart + percent / 100 * (self.pend - self.pstart)
        if self.pprev < progress:
            self._backend.percentage(int(progress))
            self.pprev = progress
        # INSTALL/UPDATE lifecycle (taken from aptcc)
        # - Starts:
        #   - "Running dpkg"
        # - Loops:
        #   - "Installing pkg" (0%)
        #   - "Preparing pkg" (25%)
        #   - "Unpacking pkg" (50%)
        #   - "Preparing to configure pkg" (75%)
        # - Some packages have:
        #   - "Runnung post-installation"
        #   - "Running dpkg"
        # - Loops:
        #   - "Configuring pkg" (0%)
        #   - Sometimes "Configuring pkg" (+25%)
        #   - "Installed pkg"
        # - Afterwards:
        #   - "Running post-installation"
        #
        # REMOVING lifecylce
        # - Starts:
        #   - "Running dpkg"
        # - loops:
        #   - "Removing pkg" (25%)
        #   - "Preparing for removal" (50%)
        #   - "Removing pkg" (75%)
        #   - "Removed pkg" (100%)
        # - Afterwards:
        #   - "Running post-installation"
        # Emit a Package signal for the currently processed package
        elif status.startswith("Preparing"):
            item_percentage = self.last_item_percentage + 25
            info = enums.INFO_PREPARING
        elif status.startswith("Installing"):
            item_percentage = 0
            info = enums.INFO_INSTALLING
        elif status.startswith("Installed"):
            item_percentage = 100
            info = enums.INFO_FINISHED
        elif status.startswith("Configuring"):
            if self.last_item_percentage >= 100:
                item_percentage = 0
            item_percentage = self.last_item_percentage + 25
            info = enums.INFO_INSTALLING
        elif status.startswith("Removing"):
            item_percentage = self.last_item_percentage + 25
            info = enums.INFO_REMOVING
        elif status.startswith("Removed"):
            item_percentage = 100
            info = enums.INFO_FINISHED
        elif status.startswith("Completely removing"):
            item_percentage = self.last_item_percentage + 25
            info = enums.INFO_REMOVING
        elif status.startswith("Completely removed"):
            item_percentage = 100
            info = enums.INFO_FINISHED
        elif status.startswith("Unpacking"):
            item_percentage = 50
            info = enums.INFO_DECOMPRESSING
        elif status.startswith("Noting disappearance of"):
            item_percentage = self.last_item_percentage
            info = enums.INFO_UNKNOWN
        elif status.startswith("Running"):
            item_percentage = self.last_item_percentage
            info = enums.INFO_CLEANUP
        else:
            item_percentage = self.last_item_percentage
            info = enums.INFO_UNKNOWN

        try:
            pkg = self._backend._cache[pkg_name]
        except KeyError:
            # Emit a fake package
            id = "%s;;;" % pkg_name
            self._backend.package(id, info, "")
            self._backend.item_percentage(id, item_percentage)
        else:
            # Always use the candidate - except for removals
            self._backend._emit_package(pkg, info, not pkg.marked_delete)
            if pkg.marked_delete:
                version = pkg.installed
            else:
                version = pkg.candidate
            id = self._backend._get_id_from_version(version)
            self._backend.item_percentage(id, item_percentage)

        self.last_pkg = pkg_name
        self.last_item_percentage = item_percentage

    def dpkg_status_changed(self, pkg_name, status):
        """Callback for dpkg status updates."""
        if status == "install":
            info = enums.INFO_INSTALLING
        elif status == "configure":
            info = enums.INFO_INSTALLING
        elif status == "remove":
            info = enums.INFO_REMOVING
        elif status == "purge":
            info = enums.INFO_PURGING
        elif status == "disappear":
            info = enums.INFO_CLEANINGUP
        elif status == "upgrade":
            info = enums.INFO_UPDATING
        elif status == "trigproc":
            info = enums.INFO_CLEANINGUP
        else:
            info = enums.INFO_UNKNOWN
        self._backend.package("%s;;;" % pkg_name, info, "")

    def start_update(self):
        # The apt system lock was set by _lock_cache() before
        self._backend._unlock_cache()
        self._backend.status(enums.STATUS_COMMIT)
        self.last_activity = time.time()
        self.start_time = time.time()

    def fork(self):
        pklog.debug("fork()")
        (pid, self.master_fd) = pty.fork()
        if pid != 0:
            fcntl.fcntl(self.master_fd, fcntl.F_SETFL, os.O_NONBLOCK)
        return pid

    def update_interface(self):
        apt.progress.base.InstallProgress.update_interface(self)
        # Collect the output from the package manager
        try:
            out = os.read(self.master_fd, 512)
            self.output = self.output + out
            pklog.debug("APT out: %s " % out)
        except OSError:
            pass
        # catch a time out by sending crtl+c
        if self.last_activity + self.timeout < time.time():
            pklog.critical("no activity for %s time sending ctrl-c" \
                           % self.timeout)
            os.write(self.master_fd, chr(3))
            #FIXME: include this into the normal install progress and add 
            #       correct package information
            raise InstallTimeOutPKError(self.output)

    def conffile(self, current, new):
        pklog.warning("Config file prompt: '%s' (sending no)" % current)
        self.conffile_prompts.add(new)

    def error(self, pkg, msg):
        raise PackageManagerFailedPKError(pkg, msg, self.output)

    def finish_update(self):
        pklog.debug("finishUpdate()")
        if self.conffile_prompts:
            self._backend.message(enums.MESSAGE_CONFIG_FILES_CHANGED,
                                  "The following conffile prompts were found "
                                  "and need investigation: %s" % \
                                  "\n".join(self.conffile_prompts))
        # Check for required restarts
        if os.path.exists("/var/run/reboot-required") and \
           os.path.getmtime("/var/run/reboot-required") > self.start_time:
            self._backend.require_restart(enums.RESTART_SYSTEM, "")


class PackageKitDpkgInstallProgress(DpkgInstallProgress,
                                    PackageKitInstallProgress):
    """
    Class to integrate the progress of core dpkg operations into PackageKit
    """
    def run(self, filenames):
        return DpkgInstallProgress.run(self, filenames)

    def update_interface(self):
        DpkgInstallProgress.update_interface(self)
        try:
            out = os.read(self.master_fd, 512)
            self.output += out
            if out != "": pklog.debug("Dpkg out: %s" % out)
        except OSError:
            pass
        # we timed out, send ctrl-c
        if self.last_activity + self.timeout < time.time():
            pklog.critical("no activity for %s time sending "
                           "ctrl-c" % self.timeout)
            os.write(self.master_fd, chr(3))
            raise InstallTimeOutPKError(self.output)


if REPOS_SUPPORT == True:
    class PackageKitSoftwareProperties(softwareproperties.SoftwareProperties.SoftwareProperties):
        """
        Helper class to fix a siily bug in python-software-properties
        """
        def set_modified_sourceslist(self):
            self.save_sourceslist()


class PackageKitAptBackend(PackageKitBaseBackend):

    """PackageKit backend for APT"""

    def __init__(self, args):
        pklog.info("Initializing APT backend")
        signal.signal(signal.SIGQUIT, self._sigquit)
        self._cache = None
        self._last_cache_refresh = None
        apt_pkg.init_config()
        apt_pkg.config.set("DPkg::Options::", '--force-confdef')
        apt_pkg.config.set("DPkg::Options::", '--force-confold')
        PackageKitBaseBackend.__init__(self, args)
        self._open_cache(progress=False)

    # Methods ( client -> engine -> backend )

    def search_file(self, filters, filenames):
        """Search for files in packages.

        Works only for installed files if apt-file isn't installed.
        """
        pklog.info("Searching for file: %s" % filenames)
        self.status(enums.STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)

        result_names = set()
        # Optionally make use of apt-file's Contents cache to search for not
        # installed files. But still search for installed files additionally
        # to make sure that we provide up-to-date results
        if (os.path.exists("/usr/bin/apt-file") and
            enums.FILTER_INSTALLED not in filters):
            #FIXME: Make use of rapt-file on Debian if the network is available
            #FIXME: Show a warning to the user if the apt-file cache is several
            #       weeks old
            pklog.debug("Using apt-file")
            filenames_regex = []
            for filename in filenames:
                if filename.startswith("/"):
                    pattern = "^%s$" % filename[1:].replace("/", "\/")
                else:
                    pattern = "\/%s$" % filename
                filenames_regex.append(pattern)
            cmd = ["/usr/bin/apt-file", "--regexp", "--non-interactive",
                   "--package-only", "find", "|".join(filenames_regex)]
            pklog.debug("Calling: %s" % cmd)
            apt_file = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE)
            stdout, stderr = apt_file.communicate()
            if apt_file.returncode == 0:
                #FIXME: Actually we should check if the file is part of the
                #       candidate, e.g. if unstable and experimental are
                #       enabled and a file would only be part of the
                #       experimental version
                result_names.update(stdout.split())
                self._emit_visible_packages_by_name(filters, result_names)
            else:
                self.error(enums.ERROR_INTERNAL_ERROR,
                           format_string("%s %s" % (stdout, stderr)))
        # Search for installed files
        filenames_regex = []
        for filename in filenames:
            if filename.startswith("/"):
                pattern = "^%s$" % filename.replace("/", "\/")
            else:
                pattern = ".*\/%s$" % filename
            filenames_regex.append(pattern)
        files_pattern = re.compile("|".join(filenames_regex))
        for pkg in self._cache:
            if pkg.name in result_names:
                continue
            for installed_file in pkg.installed_files:
                if files_pattern.match(installed_file):
                    self._emit_visible_package(filters, pkg)
                    break

    def search_group(self, filters, groups):
        """Search packages by their group."""
        pklog.info("Searching for groups: %s" % groups)
        self.status(enums.STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)

        for pkg in self._cache:
            if self._get_package_group(pkg) in groups:
                self._emit_visible_package(filters, pkg)

    def search_name(self, filters, values):
        """Search packages by name."""
        def matches(searches, text):
            for search in searches:
                if not search in text:
                    return False
            return True
        pklog.info("Searching for package name: %s" % values)
        self.status(enums.STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)

        for pkg_name in self._cache.keys():
            if matches(values, pkg_name):
                self._emit_all_visible_pkg_versions(filters,
                                                    self._cache[pkg_name])

    def search_details(self, filters, values):
        """Search packages by details."""
        pklog.info("Searching for package details: %s" % values)
        self.status(enums.STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)
        results = []

        if XAPIAN_SUPPORT == True:
            search_flags = (xapian.QueryParser.FLAG_BOOLEAN |
                            xapian.QueryParser.FLAG_PHRASE |
                            xapian.QueryParser.FLAG_LOVEHATE |
                            xapian.QueryParser.FLAG_BOOLEAN_ANY_CASE)
            pklog.debug("Performing xapian db based search")
            db = xapian.Database(XAPIAN_DB)
            parser = xapian.QueryParser()
            parser.set_default_op(xapian.Query.OP_AND)
            query = parser.parse_query(" ".join(values), search_flags)
            enquire = xapian.Enquire(db)
            enquire.set_query(query)
            matches = enquire.get_mset(0, 1000)
            for pkg_name in (match.document.get_data()
                             for match in enquire.get_mset(0, 1000)):
                if pkg_name in self._cache:
                    self._emit_visible_package(filters, self._cache[pkg_name])
        else:
            def matches(searches, text):
                for search in searches:
                    if not search in text:
                        return False
                return True
            pklog.debug("Performing apt cache based search")
            values = [val.lower() for val in values]
            for pkg in self._cache:
                txt = pkg.name
                try:
                    txt += pkg.candidate.raw_description.lower()
                    txt += pkg.candidate._translated_records.long_desc.lower()
                except AttributeError:
                    pass
                if matches(values, txt):
                    self._emit_visible_package(filters, pkg)

    def get_distro_upgrades(self):
        """
        Implement the {backend}-get-distro-upgrades functionality
        """
        pklog.info("Get distro upgrades")
        self.status(enums.STATUS_INFO)
        self.allow_cancel(False)
        self.percentage(None)

        if META_RELEASE_SUPPORT == False:
            if "update-manager-core" in self._cache and \
               self._cache["update-manager-core"].isInstalled == False:
                self.error(enums.ERROR_INTERNAL_ERROR,
                           "Please install the package update-manager-core to "
                           "get notified of the latest distribution releases.")
            else:
                self.error(enums.ERROR_INTERNAL_ERROR,
                           "Please make sure that update-manager-core is"
                           "correctly installed.")
            return

        #FIXME Evil to start the download during init
        meta_release = MetaReleaseCore(False, False)
        #FIXME: should use a lock
        while meta_release.downloading:
            time.sleep(1)
        #FIXME: Add support for description
        if meta_release.new_dist != None:
            self.distro_upgrade("stable",
                                "%s %s" % (meta_release.new_dist.name,
                                           meta_release.new_dist.version),
                                "The latest stable release")

    def get_updates(self, filters):
        """Get available package updates."""
        def succeeds_security_update(pkg):
            """
            Return True if an update succeeds a previous security update

            An example would be a package with version 1.1 in the security
            archive and 1.1.1 in the archive of proposed updates or the
            same version in both archives.
            """
            for version in pkg.versions:
                # Only check versions between the installed and the candidate
                if (pkg.installed and
                    apt_pkg.version_compare(version.version,
                                            pkg.installed.version) <= 0 and
                    apt_pkg.version_compare(version.version,
                                            pkg.candidate.version) > 0):
                    continue
                for origin in version.origins:
                    if origin.origin in ["Debian", "Ubuntu"] and \
                       (origin.archive.endswith("-security") or \
                        origin.label == "Debian-Security") and \
                       origin.trusted:
                        return True
            return False
        #FIXME: Implment the basename filter
        pklog.info("Get updates")
        self.status(enums.STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)
        self._check_init(progress=False)
        # Start with a safe upgrade
        self._cache.upgrade(dist_upgrade=True)
        # Search for upgrades which are not already part of the safe upgrade
        # but would only require the installation of additional packages
        for pkg in self._cache:
            if not pkg.isUpgradable:
                continue
            # This may occur on pinned packages which have been updated to
            # later version than the pinned one
            if not pkg.candidate.origins:
                continue
            if not pkg.marked_upgrade:
                #FIXME: Would be nice to have a reason here why
                self._emit_package(pkg, enums.INFO_BLOCKED,
                                   force_candidate=True)
            # The update can be safely installed
            info = enums.INFO_NORMAL
            # Detect the nature of the upgrade (e.g. security, enhancement)
            candidate_origin = pkg.candidate.origins[0]
            archive = candidate_origin.archive
            origin = candidate_origin.origin
            trusted =candidate_origin.trusted
            label = candidate_origin.label
            if origin in ["Debian", "Ubuntu"] and trusted == True:
                if archive.endswith("-security") or label == "Debian-Security":
                    info = enums.INFO_SECURITY
                elif succeeds_security_update(pkg):
                    pklog.debug("Update of %s succeeds a security update. "
                                "Raising its priority." % pkg.name)
                    info = enums.INFO_SECURITY
                elif archive.endswith("-backports"):
                    info = enums.INFO_ENHANCEMENT
                elif archive.endswith("-updates"):
                    info = enums.INFO_BUGFIX
            if origin in ["Backports.org archive"] and trusted == True:
                info = enums.INFO_ENHANCEMENT
            self._emit_package(pkg, info, force_candidate=True)
        self._cache.clear()

    def get_update_detail(self, pkg_ids):
        """Get details about updates."""
        def get_bug_urls(changelog):
            """Return a list of urls pointing to closed bugs in the
            changelog.
            """
            urls = []
            for r in re.findall(MATCH_BUG_CLOSES_DEBIAN, changelog,
                                re.IGNORECASE | re.MULTILINE):
                urls.extend([HREF_BUG_DEBIAN % bug for bug in \
                             re.findall(MATCH_BUG_NUMBERS, r)])
            for r in re.findall(MATCH_BUG_CLOSES_UBUNTU, changelog,
                                re.IGNORECASE | re.MULTILINE):
                urls.extend([HREF_BUG_UBUNTU % bug for bug in \
                             re.findall(MATCH_BUG_NUMBERS, r)])
            return urls

        def get_cve_urls(changelog):
            """Return a list of urls pointing to CVEs reports referred to in
            the changelog.
            """
            return [HREF_CVE % c for c in re.findall(MATCH_CVE, changelog,
                                                     re.MULTILINE)]

        pklog.info("Get update details of %s" % pkg_ids)
        self.status(enums.STATUS_DOWNLOAD_CHANGELOG)
        self.percentage(0)
        self.allow_cancel(True)
        self._check_init(progress=False)
        total = len(pkg_ids)
        for count, pkg_id in enumerate(pkg_ids):
            self.percentage(count * 100 / total)
            pkg = self._get_package_by_id(pkg_id)
            # FIXME add some real data
            if pkg.installed.origins:
                installed_origin = pkg.installed.origins[0].label
            else:
                installed_origin = ""
            updates = "%s;%s;%s;%s" % (pkg.name, pkg.installed.version,
                                       pkg.installed.architecture,
                                       installed_origin)
            obsoletes = ""
            vendor_url = ""
            restart = "none"
            update_text = ""
            state = ""
            issued = ""
            updated = ""
            #FIXME: make this more configurable. E.g. a dbus update requires
            #       a reboot on Ubuntu but not on Debian
            if pkg.name.startswith("linux-image-") or \
               pkg.name in ["libc6"]:
                restart == enums.RESTART_SYSTEM
            #FIXME: Should be part of python-apt
            changelog_dir = apt_pkg.config.find_dir("Dir::Cache::Changelogs")
            if changelog_dir == "/":
                changelog_dir = os.path.join(apt_pkg.config.find_dir("Dir::"
                                                                     "Cache"),
                                             "Changelogs")
            filename = os.path.join(changelog_dir,
                                    "%s_%s.gz" % (pkg.name,
                                                  pkg.candidate.version))
            changelog_raw = ""
            if os.path.exists(filename):
                pklog.debug("Reading changelog from cache")
                changelog_file = gzip.open(filename, "rb")
                try:
                    changelog_raw = changelog_file.read().decode("UTF-8")
                finally:
                    changelog_file.close()
            if not changelog_raw:
                pklog.debug("Downloading changelog")
                changelog_raw = pkg.get_changelog()
                # The internal download error string of python-apt ist not
                # provided as unicode object
                if not isinstance(changelog_raw, str):
                    changelog_raw = changelog_raw.decode("UTF-8")
                else:
                    # Write the changelog to the cache
                    if not os.path.exists(changelog_dir):
                        os.makedirs(changelog_dir)
                    # Remove obsolete cached changelogs
                    pattern = os.path.join(changelog_dir, "%s_*.gz" % pkg.name)
                    for old_changelog in glob.glob(pattern):
                        os.remove(os.path.join(changelog_dir, old_changelog))
                    with gzip.open(filename, mode="wb") as changelog_file:
                        changelog_file.write(changelog_raw.encode("UTF-8"))
            # Convert the changelog to markdown syntax
            changelog = ""
            for line in changelog_raw.split("\n"):
                if line == "":
                    changelog += " \n"
                else:
                    changelog += "    %s  \n" % line
                if line.startswith(pkg.candidate.source_name):
                    match = re.match(r"(?P<source>.+) \((?P<version>.*)\) "
                                      "(?P<dist>.+); urgency=(?P<urgency>.+)",
                                     line)
                    update_text += "%s\n%s\n\n" % (match.group("version"),
                                                    "=" * \
                                                    len(match.group("version")))
                elif line.startswith("  "):
                    update_text += "  %s  \n" % line
                elif line.startswith(" --"):
                    #FIXME: Add %z for the time zone - requires Python 2.6
                    update_text += "  \n"
                    match = re.match("^ -- (?P<maintainer>.+) (?P<mail><.+>)  "
                                     "(?P<date>.+) (?P<offset>[-\+][0-9]+)$",
                                     line)
                    date = datetime.datetime.strptime(match.group("date"),
                                                      "%a, %d %b %Y %H:%M:%S")

                    issued = date.isoformat()
                    if not updated:
                        updated = date.isoformat()
            if issued == updated:
                updated = ""
            bugzilla_url = ";;".join(get_bug_urls(changelog))
            cve_url = ";;".join(get_cve_urls(changelog))
            self.update_detail(pkg_id, updates, obsoletes, vendor_url,
                               bugzilla_url, cve_url, restart,
                               format_string(update_text),
                               format_string(changelog),
                               state, issued, updated)

    def get_details(self, pkg_ids):
        """Emit details about packages."""
        pklog.info("Get details of %s" % pkg_ids)
        self.status(enums.STATUS_INFO)
        self.percentage(None)
        self.allow_cancel(True)
        self._check_init(progress=False)
        total = len(pkg_ids)
        for count, pkg_id in enumerate(pkg_ids):
            self.percentage(count * 100 / total)
            pkg = self._get_package_by_id(pkg_id)
            #FIXME: We need more fine grained license information!
            candidate = pkg.candidateOrigin
            if candidate != None and  \
               candidate[0].component in ["main", "universe"] and \
               candidate[0].origin in ["Debian", "Ubuntu"]:
                license = "free"
            else:
                license = "unknown"
            group = self._get_package_group(pkg)
            self.details(pkg_id, license, group,
                         format_string(pkg.description),
                         pkg.homepage,
                         pkg.packageSize)

    @lock_cache
    def update_system(self, only_trusted):
        """Upgrade the system."""
        pklog.info("Upgrading system")
        self.status(enums.STATUS_UPDATE)
        self.allow_cancel(False)
        self.percentage(0)
        self._check_init()
        self._cache.upgrade()
        #FIXME: Emit message about skipped updates
#        for pkg in self._cache:
#            if pkg.is_upgradable and pkg.marked_upgrade:
#                continue
        self._check_trusted(only_trusted)
        self._commit_changes()

    @lock_cache
    def remove_packages(self, allow_deps, auto_remove, ids):
        """Remove packages."""
        pklog.info("Removing package(s): id %s" % ids)
        self.status(enums.STATUS_REMOVE)
        self.allow_cancel(False)
        self.percentage(0)
        self._check_init()
        if auto_remove:
            auto_removables = [pkg.name for pkg in self._cache \
                               if pkg.is_auto_removable]
        pkgs = self._mark_for_removal(ids)
        # Check if the removal would remove further packages
        if not allow_deps and self._cache.delete_count != len(ids):
            dependencies = [pkg.name for pkg in self._cache.get_changes() \
                            if pkg.name not in pkgs]
            self.error(enums.ERROR_DEP_RESOLUTION_FAILED,
                       "The following packages would have also to be removed: "
                       "%s" % " ".join(dependencies))
        if auto_remove:
            self._check_obsoleted_dependencies()
        #FIXME: Should support only_trusted
        self._commit_changes(install_start=10, install_end=90)
        self._open_cache(start=90, end=99)
        for pkg_name in pkgs:
            if pkg_name in self._cache and self._cache[pkg_name].is_installed:
                self.error(enums.ERROR_PACKAGE_FAILED_TO_INSTALL,
                           "%s is still installed" % pkg_name)
        self.percentage(100)

    def _check_obsoleted_dependencies(self):
        """Check for no longer required dependencies which should be removed
        too.
        """
        installed_deps = set()
        with self._cache.actiongroup():
            for pkg in self._cache:
                if pkg.marked_delete:
                    installed_deps = self._installed_dependencies(pkg.name,
                                                                 installed_deps)
            for dep_name in installed_deps:
                if dep_name in self._cache:
                    pkg = self._cache[dep_name]
                    if pkg.is_installed and pkg.is_auto_removeable:
                        pkg.mark_delete(False)

    def _installed_dependencies(self, pkg_name, all_deps=None):
        """Recursivly return all installed dependencies of a given package."""
        #FIXME: Should be part of python-apt
        #       apt.packagek.Version.get_dependencies(recursive=True)
        if not all_deps:
            all_deps = set()
        if not pkg_name in self._cache:
            return all_deps
        cur = self._cache[pkg_name]._pkg.current_ver
        if not cur:
            return all_deps
        for sec in ("PreDepends", "Depends", "Recommends"):
            try:
                for dep in cur.depends_list[sec]:
                    dep_name = dep[0].target_pkg.name
                    if not dep_name in all_deps:
                        all_deps.add(dep_name)
                        all_deps |= self._installed_dependencies(dep_name,
                                                                 all_deps)
            except KeyError:
                pass
        return all_deps

    def simulate_remove_packages(self, ids):
        """Emit the change required for the removal of the given packages."""
        pklog.info("Simulating removal of package with id %s" % ids)
        self.status(enums.STATUS_DEP_RESOLVE)
        self.allow_cancel(True)
        self.percentage(None)
        self._check_init(progress=False)
        pkgs = self._mark_for_removal(ids)
        self._emit_changes(pkgs)

    def _mark_for_removal(self, ids):
        """Resolve the given package ids and mark the packages for removal."""
        pkgs = []
        with self._cache.actiongroup():
            resolver = apt.cache.ProblemResolver(self._cache)
            for id in ids:
                version = self._get_version_by_id(id)
                pkg = version.package
                if not pkg.isInstalled:
                    self.error(enums.ERROR_PACKAGE_NOT_INSTALLED,
                               "Package %s isn't installed" % pkg.name)
                if pkg.installed != version:
                    self.error(enums.ERROR_PACKAGE_NOT_INSTALLED,
                               "Version %s of %s isn't installed" % \
                               (version.version, pkg.name))
                if pkg.essential == True:
                    self.error(enums.ERROR_CANNOT_REMOVE_SYSTEM_PACKAGE,
                               "Package %s cannot be removed." % pkg.name)
                pkgs.append(pkg.name[:])
                pkg.markDelete(False, False)
                resolver.clear(pkg)
                resolver.protect(pkg)
                resolver.remove(pkg)
            try:
                resolver.resolve()
            except SystemError as error:
                broken = [pkg.name for pkg in self._cache if pkg.is_inst_broken]
                self.error(enums.ERROR_DEP_RESOLUTION_FAILED,
                           "The following packages would break and so block"
                           "the removal: %s" % " ".join(broken))
        return pkgs

    def get_repo_list(self, filters):
        """
        Implement the {backend}-get-repo-list functionality

        FIXME: should we use the abstration of software-properties or provide
               low level access using pure aptsources?
        """
        pklog.info("Getting repository list: %s" % filters)
        self.status(enums.STATUS_INFO)
        self.allow_cancel(False)
        self.percentage(0)
        if REPOS_SUPPORT == False:
            if "python-software-properties" in self._cache and \
               self._cache["python-software-properties"].isInstalled == False:
                self.error(enums.ERROR_INTERNAL_ERROR,
                           "Please install the package "
                           "python-software-properties to handle repositories")
            else:
                self.error(enums.ERROR_INTERNAL_ERROR,
                           "Please make sure that python-software-properties is"
                           "correctly installed.")
        repos = PackageKitSoftwareProperties()
        # Emit distro components as virtual repositories
        for comp in repos.distro.source_template.components:
            repo_id = "%s_comp_%s" % (repos.distro.id, comp.name)
            description = "%s %s - %s (%s)" % (repos.distro.id,
                                               repos.distro.release,
                                               comp.get_description().decode("UTF-8"),
                                               comp.name)
            #FIXME: There is no inconsitent state in PackageKit
            enabled = repos.get_comp_download_state(comp)[0]
            if not enums.FILTER_DEVELOPMENT in filters:
                self.repo_detail(repo_id,
                                 format_string(description),
                                 enabled)
        # Emit distro's virtual update repositories
        for template in repos.distro.source_template.children:
            repo_id = "%s_child_%s" % (repos.distro.id, template.name)
            description = "%s %s - %s (%s)" % (repos.distro.id,
                                               repos.distro.release,
                                               template.description.decode("UTF-8"),
                                               template.name)
            #FIXME: There is no inconsitent state in PackageKit
            enabled = repos.get_comp_child_state(template)[0]
            if not enums.FILTER_DEVELOPMENT in filters:
                self.repo_detail(repo_id,
                                 format_string(description),
                                 enabled)
        # Emit distro's cdrom sources
        for source in repos.get_cdrom_sources():
            if enums.FILTER_NOT_DEVELOPMENT in filters and \
               source.type in ("deb-src", "rpm-src"):
                continue
            enabled = not source.disabled
            # Remove markups from the description
            description = re.sub(r"</?b>", "", repos.render_source(source))
            repo_id = "cdrom_%s_%s" % (source.uri, source.dist)
            repo_id.join(["_%s" % c for c in source.comps])
            self.repo_detail(repo_id, format_string(description), enabled)
        # Emit distro's virtual source code repositoriy
        if not enums.FILTER_NOT_DEVELOPMENT in filters:
            repo_id = "%s_source" % repos.distro.id
            enabled = repos.get_source_code_state() or False
            #FIXME: no translation :(
            description = "%s %s - Source code" % (repos.distro.id,
                                                   repos.distro.release)
            self.repo_detail(repo_id, format_string(description), enabled)
        # Emit third party repositories
        for source in repos.get_isv_sources():
            if enums.FILTER_NOT_DEVELOPMENT in filters and \
               source.type in ("deb-src", "rpm-src"):
                continue
            enabled = not source.disabled
            # Remove markups from the description
            description = re.sub(r"</?b>", "", repos.render_source(source))
            repo_id = "isv_%s_%s" % (source.uri, source.dist)
            repo_id.join(["_%s" % c for c in source.comps])
            self.repo_detail(repo_id,
                             format_string(description.decode("UTF-8")),
                             enabled)

    def repo_enable(self, repo_id, enable):
        """
        Implement the {backend}-repo-enable functionality

        FIXME: should we use the abstration of software-properties or provide
               low level access using pure aptsources?
        """
        pklog.info("Enabling repository: %s %s" % (repo_id, enable))
        self.status(enums.STATUS_RUNNING)
        self.allow_cancel(False)
        self.percentage(0)
        if REPOS_SUPPORT == False:
            if "python-software-properties" in self._cache and \
               self._cache["python-software-properties"].isInstalled == False:
                self.error(enums.ERROR_INTERNAL_ERROR,
                           "Please install the package "
                           "python-software-properties to handle repositories")
            else:
                self.error(enums.ERROR_INTERNAL_ERROR,
                           "Please make sure that python-software-properties is"
                           "correctly installed.")
            return
        repos = PackageKitSoftwareProperties()

        found = False
        # Check if the repo_id matches a distro component, e.g. main
        if repo_id.startswith("%s_comp_" % repos.distro.id):
            for comp in repos.distro.source_template.components:
                if repo_id == "%s_comp_%s" % (repos.distro.id, comp.name):
                    if enable == repos.get_comp_download_state(comp)[0]:
                        pklog.debug("Repository is already enabled")
                        pass
                    if enable == True:
                        repos.enable_component(comp.name)
                    else:
                        repos.disable_component(comp.name)
                    found = True
                    break
        # Check if the repo_id matches a distro child repository, e.g.
        # hardy-updates
        elif repo_id.startswith("%s_child_" % repos.distro.id):
            for template in repos.distro.source_template.children:
                if repo_id == "%s_child_%s" % (repos.distro.id, template.name):
                    if enable == repos.get_comp_child_state(template)[0]:
                        pklog.debug("Repository is already enabled")
                        pass
                    elif enable == True:
                        repos.enable_child_source(template)
                    else:
                        repos.disable_child_source(template)
                    found = True
                    break
        # Check if the repo_id matches a cdrom repository
        elif repo_id.startswith("cdrom_"):
            for source in repos.get_isv_sources():
                source_id = "cdrom_%s_%s" % (source.uri, source.dist)
                source_id.join(["_%s" % c for c in source.comps])
                if repo_id == source_id:
                    if source.disabled == enable:
                        source.disabled = not enable
                        repos.save_sourceslist()
                    else:
                        pklog.debug("Repository is already enabled")
                    found = True
                    break
        # Check if the repo_id matches an isv repository
        elif repo_id.startswith("isv_"):
            for source in repos.get_isv_sources():
                source_id = "isv_%s_%s" % (source.uri, source.dist)
                source_id.join(["_%s" % c for c in source.comps])
                if repo_id == source_id:
                    if source.disabled == enable:
                        source.disabled = not enable
                        repos.save_sourceslist()
                    else:
                        pklog.debug("Repository is already enabled")
                    found = True
                    break
        if found == False:
            self.error(enums.ERROR_REPO_NOT_AVAILABLE,
                       "The repository of the id %s isn't available" % repo_id)

    @lock_cache
    def update_packages(self, only_trusted, ids):
        """Update packages."""
        pklog.info("Updating package with id %s" % ids)
        self.status(enums.STATUS_UPDATE)
        self.allow_cancel(False)
        self.percentage(0)
        self._check_init()
        pkgs = self._mark_for_upgrade(ids)
        self._check_trusted(only_trusted)
        self._commit_changes()
        self._open_cache(start=90, end=100)
        self.percentage(100)
        pklog.debug("Checking success of operation")
        for pkg_name in pkgs:
            if (pkg_name not in self._cache or
                not self._cache[pkg_name].is_installed or
                self._cache[pkg_name].is_upgradable):
                self.error(enums.ERROR_PACKAGE_FAILED_TO_INSTALL,
                           "%s was not updated" % pkg_name)
        pklog.debug("Sending success signal")

    def simulate_update_packages(self, ids):
        """Emit the changes required for the upgrade of the given packages."""
        pklog.info("Simulating update of package with id %s" % ids)
        self.status(enums.STATUS_DEP_RESOLVE)
        self.allow_cancel(True)
        self.percentage(None)
        self._check_init(progress=False)
        pkgs = self._mark_for_upgrade(ids)
        self._emit_changes(pkgs)

    def _mark_for_upgrade(self, ids):
        """Resolve the given package ids and mark the packages for upgrade."""
        pkgs = []
        with self._cache.actiongroup():
            resolver = apt.cache.ProblemResolver(self._cache)
            for id in ids:
                version = self._get_version_by_id(id)
                pkg = version.package
                if not pkg.is_installed:
                    self.error(enums.ERROR_PACKAGE_NOT_INSTALLED,
                               "%s isn't installed" % pkg.name)
                # Check if the specified version is an update
                if apt_pkg.version_compare(pkg.installed.version,
                                           version.version) >= 0:
                    self.error(enums.ERROR_UPDATE_NOT_FOUND,
                               "The version %s of %s isn't an update to the "
                               "current %s" % (version.version, pkg.name,
                                               pkg.installed.version))
                pkg.candidate = version
                pkgs.append(pkg.name[:])
                pkg.mark_install(False, True)
                resolver.clear(pkg)
                resolver.protect(pkg)
            try:
                resolver.resolve()
            except SystemError as error:
                broken = [pkg.name for pkg in self._cache if pkg.is_inst_broken]
                self.error(enums.ERROR_DEP_RESOLUTION_FAILED,
                           "The following packages block the installation: "
                           "%s" % " ".join(broken))
        return pkgs

    def download_packages(self, dest, ids):
        """Download packages to the given destination."""
        def get_download_details(ids):
            """Calculate the start and end point of a package download
            progress.
            """
            total = 0
            downloaded = 0
            versions = []
            # Check if all ids are vaild and calculate the total download size
            for id in ids:
                pkg_ver = self._get_pkg_version_by_id(id)
                if not pkg_ver.downloadable:
                    self.error(enums.ERROR_PACKAGE_DOWNLOAD_FAILED,
                               "package %s isn't downloadable" % id)
                total += pkg_ver.size
                versions.append((id, pkg_ver))
            for id, ver in versions:
                start = downloaded * 100 / total
                end = start + ver.size * 100 / total
                yield id, ver, start, end
                downloaded += ver.size
        pklog.info("Downloading packages: %s" % ids)
        self.status(enums.STATUS_DOWNLOAD)
        self.allow_cancel(True)
        self.percentage(0)
        # Check the destination directory
        if not os.path.isdir(dest) or not os.access(dest, os.W_OK):
            self.error(enums.ERROR_INTERNAL_ERROR,
                       "The directory '%s' is not writable" % dest)
        # Setup the fetcher
        self._check_init()
        # Start the download
        for id, ver, start, end in get_download_details(ids):
            progress = PackageKitAcquireProgress(self, start, end)
            self._emit_pkg_version(ver, enums.INFO_DOWNLOADING)
            try:
                ver.fetch_binary(dest, progress)
            except Exception as error:
                self.error(enums.ERROR_PACKAGE_DOWNLOAD_FAILED,
                           format_string(str(error)))
            else:
                self.files(id, os.path.join(dest,
                                            os.path.basename(ver.filename)))
                self._emit_pkg_version(ver, enums.INFO_FINISHED)
        self.percentage(100)

    @lock_cache
    def install_packages(self, only_trusted, ids):
        """Install the given packages."""
        pklog.info("Installing package with id %s" % ids)
        self.status(enums.STATUS_INSTALL)
        self.allow_cancel(False)
        self.percentage(0)
        self._check_init()
        pkgs = self._mark_for_installation(ids)
        self._check_trusted(only_trusted)
        self._commit_changes()
        self._open_cache(start=90, end=100)
        self.percentage(100)
        pklog.debug("Checking success of operation")
        for p in pkgs:
            if p not in self._cache or not self._cache[p].is_installed:
                self.error(enums.ERROR_PACKAGE_FAILED_TO_INSTALL,
                           "%s was not installed" % p)

    def simulate_install_packages(self, ids):
        """Emit the changes required for the installation of the given
        packages.
        """
        pklog.info("Simulating installing package with id %s" % ids)
        self.status(enums.STATUS_DEP_RESOLVE)
        self.allow_cancel(True)
        self.percentage(None)
        self._check_init(progress=False)
        pkgs = self._mark_for_installation(ids)
        self._emit_changes(pkgs)

    def _mark_for_installation(self, ids):
        """Resolve the given package ids and mark the packages for
        installation.
        """
        pkgs = []
        with self._cache.actiongroup():
            resolver = apt.cache.ProblemResolver(self._cache)
            for id in ids:
                version = self._get_version_by_id(id)
                pkg = version.package
                pkg.candidate = version
                if pkg.installed == version:
                    self.error(enums.ERROR_PACKAGE_ALREADY_INSTALLED,
                               "Package %s is already installed" % pkg.name)
                pkgs.append(pkg.name[:])
                pkg.markInstall(False, True, True)
                resolver.clear(pkg)
                resolver.protect(pkg)
            try:
                resolver.resolve()
            except SystemError as error:
                broken = [pkg.name for pkg in self._cache if pkg.is_inst_broken]
                self.error(enums.ERROR_DEP_RESOLUTION_FAILED,
                           "The following packages block the installation: "
                           "%s" % " ".join(broken))
        return pkgs

    @lock_cache
    def install_files(self, only_trusted, inst_files):
        """Install local Debian package files."""
        pklog.info("Installing package files: %s" % inst_files)
        self.status(enums.STATUS_INSTALL)
        self.allow_cancel(False)
        self.percentage(0)
        self._check_init()
        packages = []
        # Collect all dependencies which need to be installed
        self.status(enums.STATUS_DEP_RESOLVE)
        for path in inst_files:
            deb = apt.debfile.DebPackage(path, self._cache)
            packages.append(deb)
            if not deb.check():
                self.error(enums.ERROR_LOCAL_INSTALL_FAILED,
                           format_string(deb._failure_string))
            (install, remove, unauthenticated) = deb.required_changes
            pklog.debug("Changes: Install %s, Remove %s, Unauthenticated "
                        "%s" % (install, remove, unauthenticated))
            if len(remove) > 0:
                self.error(enums.ERROR_DEP_RESOLUTION_FAILED,
                           "Remove the following packages "
                           "before: %s" % remove)
            if (deb.compare_to_version_in_cache() ==
                apt.debfile.DebPackage.VERSION_OUTDATED):
                self.message(enums.MESSAGE_NEWER_PACKAGE_EXISTS,
                             "There is a later version of %s "
                             "available in the repositories." % deb.pkgname)
        if self._cache.get_changes():
            self._check_trusted(only_trusted)
            self._commit_changes(fetch_start=10, fetch_end=25,
                                 install_start=25, install_end=50)
        # Install the Debian package files
        progress = PackageKitDpkgInstallProgress(self, start=50, end=90)
        try:
            progress.start_update()
            progress.install(inst_files)
            progress.finish_update()
        except InstallTimeOutPKError as error:
            self._recover()
            #FIXME: should provide more information
            msg = "Transaction was cancelled since the installation " \
                  "of a package hung.\n" \
                  "This can be caused by maintainer scripts which " \
                  "require input on the terminal:\n%s" % str(error)
            self.error(enums.ERROR_INTERNAL_ERROR, format_string(msg))
        except PackageManagerFailedPKError as error:
            self._recover()
            self.error(enums.ERROR_INTERNAL_ERROR,
                       format_string("%s\n%s" % (error.message, error.output)))
        except Exception as error:
            self._recover()
            self.error(enums.ERROR_INTERNAL_ERROR, format_string(str(error)))
        self.percentage(100)

    def simulate_install_files(self, inst_files):
        """Emit the change required for the installation of the given package
        files.
        """
        pklog.info("Simulating installation of the package files: "
                   "%s" % inst_files)
        self.status(enums.STATUS_DEP_RESOLVE)
        self.allow_cancel(True)
        self.percentage(None)
        self._check_init(progress=False)
        pkgs = []
        for path in inst_files:
            deb = apt.debfile.DebPackage(path, self._cache)
            pkgs.append(deb.pkgname)
            if not deb.check():
                self.error(enums.ERROR_LOCAL_INSTALL_FAILED,
                           format_string(deb._failure_string))
        self._emit_changes(pkgs)

    @lock_cache
    def refresh_cache(self, force):
        """Update the package cache."""
        # TODO: use force ?
        pklog.info("Refresh cache")
        self.status(enums.STATUS_REFRESH_CACHE)
        self.last_action_time = time.time()
        self.allow_cancel(False);
        self.percentage(0)
        self._check_init()
        progress = PackageKitAcquireRepoProgress(self, start=10, end=95)
        try:
            ret = self._cache.update(progress)
        except Exception as error:
            # FIXME: Unluckily python-apt doesn't provide a real good error
            #        reporting. We only receive a failure string.
            # FIXME: Doesn't detect if all downloads failed - bug in python-apt
            self.message(enums.MESSAGE_REPO_METADATA_DOWNLOAD_FAILED,
                         format_string(str(error)))
        self._open_cache(start=95, end=100)
        self.percentage(100)

    def get_packages(self, filters):
        """Get packages."""
        pklog.info("Get all packages")
        self.status(enums.STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)

        total = len(self._cache)
        for count, pkg in enumerate(self._cache):
            self.percentage(count / 100 * total)
            if self._is_package_visible(pkg, filters):
                self._emit_package(pkg)

    def resolve(self, filters, names):
        """
        Implement the apt2-resolve functionality
        """
        pklog.info("Resolve")
        self.status(enums.STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(False)

        for name in names:
            try:
                self._emit_visible_package(filters, self._cache[name])
            except KeyError:
                self.error(enums.ERROR_PACKAGE_NOT_FOUND,
                           "Package name %s could not be resolved" % name)

    def get_depends(self, filters, ids, recursive):
        """Emit all dependencies of the given package ids.

        Doesn't support recursive dependency resolution.
        """
        def emit_blocked_dependency(base_dependency, pkg=None,
                                    filters=[]):
            """Send a blocked package signal for the given
            apt.package.BaseDependency.
            """
            if enums.FILTER_INSTALLED in filters:
                return
            if pkg:
                summary = pkg.summary
                try:
                    filters.remove(enums.FILTER_NOT_INSTALLED)
                except ValueError:
                    pass
                if not self._is_package_visible(pkg, filters):
                    return
            else:
                summary = ""
            if base_dependency.relation:
                version = "%s%s" % (base_dependency.relation,
                                    base_dependency.version)
            else:
                version = base_dependency.version
            self.package("%s;%s;;" % (base_dependency.name, version),
                         enums.INFO_BLOCKED, summary)

        def check_dependency(pkg, base_dep):
            """Check if the given apt.package.Package can satisfy the
            BaseDepenendcy and emit the corresponding package signals.
            """
            if not self._is_package_visible(pkg, filters):
                return
            if base_dep.version:
                satisfied = False
                # Sort the version list to check the installed
                # and candidate before the other ones
                ver_list = list(pkg.versions)
                if pkg.installed:
                    ver_list.remove(pkg.installed)
                    ver_list.insert(0, pkg.installed)
                if pkg.candidate:
                    ver_list.remove(pkg.candidate)
                    ver_list.insert(0, pkg.candidate)
                for dep_ver in ver_list:
                    if apt_pkg.CheckDep(dep_ver.version,
                                        base_dep.relation,
                                        base_dep.version):
                        self._emit_pkg_version(dep_ver)
                        satisfied = True
                        break
                if not satisfied:
                    emit_blocked_dependency(base_dep, pkg, filters)
            else:
                self._emit_package(pkg)

        # Setup the transaction
        pklog.info("Get depends (%s,%s,%s)" % (filter, ids, recursive))
        self.status(enums.STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)

        dependency_types = ["PreDepends", "Depends"]
        if apt_pkg.config["APT::Install-Recommends"]:
            dependency_types.append("Recommends")
        total = len(ids)
        for count, id in enumerate(ids):
            self.percentage(count / 100 * total)
            version = self._get_version_by_id(id)
            for dependency in version.get_dependencies(*dependency_types):
                # Walk through all or_dependencies
                for base_dep in dependency.or_dependencies:
                    if self._cache.is_virtual_package(base_dep.name):
                        # Check each proivider of a virtual package
                        for provider in \
                              self._cache.get_providing_packages(base_dep.name):
                            check_dependency(provider, base_dep)
                    elif base_dep.name in self._cache:
                        check_dependency(self._cache[base_dep.name], base_dep)
                    else:
                        # The dependency does not exist
                        emit_blocked_dependency(base_dep, filters=filters)

    def get_requires(self, filters, ids, recursive):
        """Emit all packages which depend on the given ids.

        Recursive searching is not supported.
        """
        pklog.info("Get requires (%s,%s,%s)" % (filter, ids, recursive))
        self.status(enums.STATUS_DEP_RESOLVE)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)
        total = len(ids)
        for count, id in enumerate(ids):
            self.percentage(count / 100 * total)
            version = self._get_version_by_id(id)
            provided = [pro[0] for pro in version._cand.ProvidesList]
            for pkg in self._cache:
                if not self._is_package_visible(pkg, filters):
                    continue
                if pkg.is_installed:
                    pkg_ver = pkg.installed
                elif pkg.candidate:
                    pkg_ver = pkg.candidate
                for dependency in pkg_ver.dependencies:
                    satisfied = False
                    for base_dep in dependency.or_dependencies:
                        if version.package.name == base_dep.name or \
                           base_dep.name in provided:
                            satisfied = True
                            break
                    if satisfied:
                        self._emit_package(pkg)
                        break

    def what_provides(self, filters, provides_type, search):
        def get_mapping_db(path):
            """
            Return the gdbm database at the given path or send an
            appropriate error message
            """
            if not os.access(path, os.R_OK):
                if ("app-install-data" in self._cache and
                    not self._cache["app-install-data"].is_installed):
                    self.error(enums.ERROR_INTERNAL_ERROR,
                               "Please install the package "
                               "app-install data for a list of "
                               "applications that can handle files of "
                               "the given type")
                else:
                    self.error(enums.ERROR_INTERNAL_ERROR,
                               "The list of applications that can handle "
                               "files of the given type cannot be opened.\n"
                               "Try to reinstall the package "
                               "app-install-data.")
                return
            try:
                db = gdbm.open(path)
            except:
                self.error(enums.ERROR_INTERNAL_ERROR,
                           "The list of applications that can handle "
                           "files of the given type cannot be opened.\n"
                           "Try to reinstall the package "
                           "app-install-data.")
            else:
                return db
        def extract_gstreamer_request(search):
            # The search term from PackageKit daemon:
            # gstreamer0.10(urisource-foobar)
            # gstreamer0.10(decoder-audio/x-wma)(wmaversion=3)
            match = re.match("^gstreamer(?P<version>[0-9\.]+)"
                             "\((?P<kind>.+?)-(?P<data>.+?)\)"
                             "(\((?P<opt>.*)\))?",
                             search)
            caps = None
            if not match:
                self.error(enums.ERROR_INTERNAL_ERROR,
                           "The search term is invalid: %s" % search)
            if match.group("opt"):
                caps_str = "%s, %s" % (match.group("data"), match.group("opt"))
                # gst.Caps.__init__ cannot handle unicode instances
                caps = gst.Caps(str(caps_str))
            record = GSTREAMER_RECORD_MAP[match.group("kind")]
            return match.group("version"), record, match.group("data"), caps
        self.status(enums.STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(False)
        if provides_type == enums.PROVIDES_CODEC:
            # Search for privided gstreamer plugins using the package
            # metadata
            import gst
            GSTREAMER_RECORD_MAP = {"encoder": "Gstreamer-Encoders",
                                    "decoder": "Gstreamer-Decoders",
                                    "urisource": "Gstreamer-Uri-Sources",
                                    "urisink": "Gstreamer-Uri-Sinks",
                                    "element": "Gstreamer-Elements"}
            for pkg in self._cache:
                if pkg.installed:
                    version = pkg.installed
                elif pkg.candidate:
                    version = pkg.candidate
                else:
                    continue
                if not "Gstreamer-Version" in version.record:
                    continue
                gst_version, gst_record, gst_data, gst_caps = \
                        extract_gstreamer_request(search)
                if version.record["Gstreamer-Version"] != gst_version:
                    continue
                if gst_caps:
                    try:
                        pkg_caps = gst.Caps(version.record[gst_record])
                    except KeyError:
                        continue
                    if gst_caps.intersect(pkg_caps):
                        self._emit_visible_package(filters, pkg)
                else:
                    try:
                        elements = version.record[gst_record]
                    except KeyError:
                        continue
                    if gst_data in elements:
                        self._emit_visible_package(filters, pkg)

        elif provides_type == enums.PROVIDES_MIMETYPE:
            # Emit packages that contain an application that can handle
            # the given mime type
            handlers = set()
            db = get_mapping_db("/var/lib/PackageKit/mime-map.gdbm")
            if db == None:
                return
            if search in db:
                pklog.debug("Mime type is registered: %s" % db[search])
                # The mime type handler db stores the packages as a string
                # separated by spaces. Each package has its section
                # prefixed and separated by a slash
                # FIXME: Should make use of the section and emit a 
                #        RepositoryRequired signal if the package does not exist
                handlers = [s.split("/")[1] for s in db[search].split(" ")]
                self._emit_visible_packages_by_name(filters, handlers)
        else:
            self.error(enums.ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")

    def get_files(self, package_ids):
        """Emit the Files signal which includes the files included in a package
        Apt only supports this for installed packages
        """
        self.status(enums.STATUS_INFO)
        total = len(package_ids)
        for count, id in enumerate(package_ids):
            self.percentage(count / 100 * total)
            pkg = self._get_package_by_id(id)
            files = ";".join(pkg.installed_files)
            self.files(id, files)

    # Helpers

    def _unlock_cache(self):
        """Unlock the system package cache."""
        try:
            apt_pkg.pkgsystem_unlock()
        except SystemError:
            return False
        return True

    def _open_cache(self, start=0, end=100, progress=True):
        """(Re)Open the APT cache."""
        pklog.debug("Open APT cache")
        self.status(enums.STATUS_LOADING_CACHE)
        try:
            self._cache = apt.Cache(PackageKitOpProgress(self, start, end,
                                                         progress))
        except Exception as error:
            self.error(enums.ERROR_NO_CACHE,
                       "Package cache could not be opened:%s" % error)
        if self._cache.broken_count > 0:
            self.error(enums.ERROR_DEP_RESOLUTION_FAILED,
                       "There are broken dependecies on your system. "
                       "Please use an advanced package manage e.g. "
                       "Synaptic or aptitude to resolve this situation.")
        self._last_cache_refresh = time.time()

    def _recover(self, start=95, end=100):
        """Try to recover from a package manager failure."""
        self.status(enums.STATUS_CLEANUP)
        self.percentage(None)
        try:
            d = PackageKitDpkgInstallProgress(self)
            d.start_update()
            d.recover()
            d.finish_update()
        except:
            pass
        self._open_cache(start=95, end=100)

    def _check_trusted(self, only_trusted):
        """Check if only trusted packages are allowed and fail if
        untrusted packages would be installed in this case.
        """
        untrusted = []
        if only_trusted:
            for pkg in self._cache:
                if (pkg.marked_install or pkg.marked_upgrade or
                    pkg.marked_downgrade or pkg.marked_reinstall):
                     trusted = False
                     for origin in pkg.candidate.origins:
                          trusted |= origin.trusted
                     if not trusted:
                         untrusted.append(pkg.name)
            if untrusted:
                self.error(enums.ERROR_MISSING_GPG_SIGNATURE,
                           " ".join(untrusted))

    def _commit_changes(self, fetch_start=10, fetch_end=50,
                        install_start=50, install_end=90):
        """Commit changes to the system."""
        try:
            self._cache.commit(PackageKitAcquireProgress(self, fetch_start,
                                                         fetch_end),
                               PackageKitInstallProgress(self, install_start,
                                                         install_end))
        except apt.cache.FetchFailedException as err:
            self._open_cache(start=95, end=100)
            pklog.critical(format_string(err.message))
            self.error(enums.ERROR_PACKAGE_DOWNLOAD_FAILED,
                       format_string(err.message))
        except apt.cache.FetchCancelledException:
            self._open_cache(start=95, end=100)
        except InstallTimeOutPKError as err:
            self._recover()
            self._open_cache(start=95, end=100)
            #FIXME: should provide more information
            msg = "Transaction was cancelled since the installation " \
                  "of a package hung.\n" \
                  "This can be caused by maintainer scripts which " \
                  "require input on the terminal:\n%s" % err.message
            self.error(enums.ERROR_INTERNAL_ERROR, format_string(msg))
        except PackageManagerFailedPKError as err:
            self._recover()
            self.error(enums.ERROR_INTERNAL_ERROR,
                       format_string("%s\n%s" % (err.message, err.output)))
        else:
            return True
        return False

    def _get_id_from_version(self, version):
        """Return the package id of an apt.package.Version instance."""
        if version.origins:
            origin = version.origins[0].label
        else:
            origin = ""
        id = "%s;%s;%s;%s" % (version.package.name, version.version,
                              version.architecture, origin)
        return id
 
    def _check_init(self, start=0, end=10, progress=True):
        """Check if the backend was initialized well and try to recover from
        a broken setup.
        """
        pklog.debug("Checking apt cache and xapian database")
        pkg_cache = os.path.join(apt_pkg.config["Dir"],
                                 apt_pkg.config["Dir::Cache"],
                                 apt_pkg.config["Dir::Cache::pkgcache"])
        src_cache = os.path.join(apt_pkg.config["Dir"],
                                 apt_pkg.config["Dir::Cache"],
                                 apt_pkg.config["Dir::Cache::srcpkgcache"])
        # Check if the cache instance is of the coorect class type, contains
        # any broken packages and if the dpkg status or apt cache files have 
        # been changed since the last refresh
        if not isinstance(self._cache, apt.cache.Cache) or \
           (self._cache.broken_count > 0) or \
           (os.stat(apt_pkg.config["Dir::State::status"])[stat.ST_MTIME] > \
            self._last_cache_refresh) or \
           (os.stat(pkg_cache)[stat.ST_MTIME] > self._last_cache_refresh) or \
           (os.stat(src_cache)[stat.ST_MTIME] > self._last_cache_refresh):
            pklog.debug("Reloading the cache is required")
            self._open_cache(start, end, progress)
        else:
            pass
        # Read the pin file of Synaptic if available
        self._cache._depcache.ReadPinFile()
        if os.path.exists(SYNAPTIC_PIN_FILE):
            self._cache._depcache.ReadPinFile(SYNAPTIC_PIN_FILE)
        # Reset the depcache
        self._cache.clear()

    def _emit_package(self, pkg, info=None, force_candidate=False):
        """Send the Package signal for a given APT package."""
        if (not pkg.is_installed or force_candidate) and pkg.candidate:
            self._emit_pkg_version(pkg.candidate, info)
        elif pkg.is_installed:
            self._emit_pkg_version(pkg.installed, info)
        else:
            pklog.debug("Package %s hasn't got any version." % pkg.name)

    def _emit_pkg_version(self, version, info=None):
        """Emit the Package signal of the given apt.package.Version."""
        id = self._get_id_from_version(version)
        section = version.section.split("/")[-1]
        if not info:
            if version == version.package.installed:
                if section == "metapackages":
                    info = enums.INFO_COLLECTION_INSTALLED
                else:
                    info = enums.INFO_INSTALLED
            else:
                if section == "metapackages":
                    info = enums.INFO_COLLECTION_AVAILABLE
                else:
                    info = enums.INFO_AVAILABLE
        self.package(id, info, version.summary)

    def _emit_all_visible_pkg_versions(self, filters, pkg):
        """Emit all available versions of a package."""
        if self._is_package_visible(pkg, filters):
            if enums.FILTER_NEWEST in filters:
                if pkg.candidate:
                    self._emit_pkg_version(pkg.candidate)
                elif pkg.installed:
                    self._emit_pkg_version(pkg.installed)
            else:
                for version in pkg.versions:
                    self._emit_pkg_version(version)

    def _emit_visible_package(self, filters, pkg, info=None):
        """Filter and emit a package."""
        if self._is_package_visible(pkg, filters):
            self._emit_package(pkg, info)

    def _emit_visible_packages(self, filters, pkgs, info=None):
        """Filter and emit packages."""
        for p in pkgs:
            if self._is_package_visible(p, filters):
                self._emit_package(p, info)

    def _emit_visible_packages_by_name(self, filters, pkgs, info=None):
        """Find the packages with the given namens. Afterwards filter and emit
        them.
        """
        for name in pkgs:
            pkg = self._cache[name]
            if self._is_package_visible(pkg, filters):
                self._emit_package(pkg, info)

    def _emit_changes(self, ignore_pkgs=[]):
        """Emit all changed packages."""
        for pkg in self._cache:
            if pkg.name in ignore_pkgs:
                continue
            if pkg.marked_delete:
                self._emit_package(pkg, enums.INFO_REMOVING, False)
            elif pkg.marked_install:
                self._emit_package(pkg, enums.INFO_INSTALLING, True)
            elif pkg.marked_upgrade:
                self._emit_package(pkg, enums.INFO_UPDATING, True)
            elif pkg.marked_downgrade:
                self._emit_package(pkg, enums.INFO_DOWNGRADING, True)
            elif pkg.marked_reinstall:
                self._emit_package(pkg, enums.INFO_REINSTALLING, True)

    def _is_package_visible(self, pkg, filters):
        """Return True if the package should be shown in the user
        interface.
        """
        if filters == [enums.FILTER_NONE]:
            return True
        for filter in filters:
            if ((filter == enums.FILTER_INSTALLED and not pkg.is_installed) or
                (filter == enums.FILTER_NOT_INSTALLED and pkg.is_installed) or
                (filter == enums.FILTER_SUPPORTED and not
                 self._is_package_supported(pkg)) or
                (filter == enums.FILTER_NOT_SUPPORTED and
                 self._is_package_supported(pkg)) or
                (filter == enums.FILTER_FREE and not
                 not self._is_package_free(pkg)) or
                (filter == enums.FILTER_NOT_FREE and
                 not self._is_package_not_free(pkg)) or
                (filter == enums.FILTER_GUI and
                 not self._has_package_gui(pkg)) or
                (filter == enums.FILTER_NOT_GUI and
                 self._has_package_gui(pkg)) or
                (filter == enums.FILTER_COLLECTIONS and not
                 self._is_package_collection(pkg)) or
                (filter == enums.FILTER_NOT_COLLECTIONS and
                 self._is_package_collection(pkg)) or
                 (filter == enums.FILTER_DEVELOPMENT and not
                 self._is_package_devel(pkg)) or
                (filter == enums.FILTER_NOT_DEVELOPMENT and
                 self._is_package_devel(pkg))):
                return False
        return True

    def _is_package_not_free(self, pkg):
        """Return True if we can be sure that the package's license isn't any 
        free one
        """
        #FIXME: Should check every origin
        origins = pkg.candidate.origins
        return (origins != None and \
                ((origins[0].origin == "Ubuntu" and
                  origins[0].component in ["multiverse", "restricted"]) or
                 (origins[0].origin == "Debian" and
                  origins[0].component in ["contrib", "non-free"])) and
                origins[0].trusted == True)

    def _is_package_collection(self, pkg):
        """Return True if the package is a metapackge."""
        section = pkg.section.split("/")[-1]
        return section == "metapackages"

    def _is_package_free(self, pkg):
        """Return True if we can be sure that the package has got a free
        license.
        """
        #FIXME: Should check every origin
        origins = pkg.candidate.origins
        return (origins[0] != None and
                ((origins[0].origin == "Ubuntu" and
                  origins[0].component in ["main", "universe"]) or
                 (origins[0].origin == "Debian" and
                  origins[0].component == "main")) and
                origins[0].trusted == True)

    def _has_package_gui(self, pkg):
        #FIXME: should go to a modified Package class
        #FIXME: take application data into account. perhaps checking for
        #       property in the xapian database
        return pkg.section.split('/')[-1].lower() in ['x11', 'gnome', 'kde']

    def _is_package_devel(self, pkg):
        #FIXME: should go to a modified Package class
        return pkg.name.endswith("-dev") or pkg.name.endswith("-dbg") or \
               pkg.section.split('/')[-1].lower() in ['devel', 'libdevel']

    def _is_package_supported(self, pkg):
        origins = pkg.candidate.origins
        #FIXME: iterate on all origins
        return (origins != None and
                origins[0].origin == "Ubuntu" and
                origins[0].component in ["main", "restricted"] and
                origins[0].trusted == True)

    def _get_pkg_version_by_id(self, id):
        """Return a package version matching the given package id or None."""
        name, version, arch, data = id.split(";", 4)
        try:
            for pkg_ver in self._cache[name].versions:
                if pkg_ver.version == version and \
                   pkg_ver.architecture == arch:
                    return pkg_ver
        except KeyError:
            pass
        return None

    def _get_package_by_id(self, id):
        """Return the apt.package.Package corresponding to the given
        package id.

        If the package isn't available error out.
        """
        version = self._get_version_by_id(id)
        return version.package

    def _get_version_by_id(self, id):
        """Return the apt.package.Version corresponding to the given
        package id.

        If the version isn't available error out.
        """
        name, version_string, arch, data = id.split(";", 4)
        try:
            pkg = self._cache[name]
        except:
            self.error(enums.ERROR_PACKAGE_NOT_FOUND,
                       "There isn't any package named %s" % name)
        try:
            version = pkg.versions[version_string]
        except:
            self.error(enums.ERROR_PACKAGE_NOT_FOUND,
                       "There isn't any verion %s of %s" % (version_string,
                                                            name))
        if version.architecture != arch:
            self.error(enums.ERROR_PACKAGE_NOT_FOUND,
                       "Version %s of %s isn't available for architecture "
                       "%s" % (pkg.name, version.version, arch))
        return version

    def _get_package_group(self, pkg):
        """
        Return the packagekit group corresponding to the package's section
        """
        section = pkg.section.split("/")[-1]
        if section in SECTION_GROUP_MAP:
            return SECTION_GROUP_MAP[section]
        else:
            pklog.debug("Unkown package section %s of %s" % (pkg.section,
                                                             pkg.name))
            return enums.GROUP_UNKNOWN

    def _sigquit(self, signum, frame):
        self._unlock_cache()
        sys.exit(1)

def debug_exception(type, value, tb):
    """Provides an interactive debugging session on unhandled exceptions
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
    """Start the apt backend."""
    backend = PackageKitAptBackend("")
    if single == True:
        backend.dispatch_command(args[0], args[1:])
    else:
        backend.dispatcher(args)

def main():
    parser = optparse.OptionParser(description="APT backend for PackageKit")
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

    if options.root:
        config = apt_pkg.config
        config.set("Dir", options.root)
        config.set("Dir::State::status",
                   os.path.join(options.root, "/var/lib/dpkg/status"))

    if options.profile:
        import hotshot
        prof = hotshot.Profile(options.profile)
        prof.runcall(run, args, options.single)
        prof.close()
    else:
        run(args, options.single)

if __name__ == '__main__':
    main()

# vim: ts=4 et sts=4
