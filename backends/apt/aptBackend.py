#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Provides an apt backend to PackageKit

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
import httplib
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

from packagekit.backend import *
from packagekit.progress import *
from packagekit.package import *
from packagekit.enums import *

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

# Map Debian sections to the PackageKit group name space
SECTION_GROUP_MAP = {
    "admin" : GROUP_ADMIN_TOOLS,
    "base" : GROUP_SYSTEM,
    "comm" : GROUP_COMMUNICATION,
    "devel" : GROUP_PROGRAMMING,
    "doc" : GROUP_DOCUMENTATION,
    "editors" : GROUP_PUBLISHING,
    "electronics" : GROUP_ELECTRONICS,
    "embedded" : GROUP_SYSTEM,
    "games" : GROUP_GAMES,
    "gnome" : GROUP_DESKTOP_GNOME,
    "graphics" : GROUP_GRAPHICS,
    "hamradio" : GROUP_COMMUNICATION,
    "interpreters" : GROUP_PROGRAMMING,
    "kde" : GROUP_DESKTOP_KDE,
    "libdevel" : GROUP_PROGRAMMING,
    "libs" : GROUP_SYSTEM,
    "mail" : GROUP_INTERNET,
    "math" : GROUP_SCIENCE,
    "misc" : GROUP_OTHER,
    "net" : GROUP_NETWORK,
    "news" : GROUP_INTERNET,
    "oldlibs" : GROUP_LEGACY,
    "otherosfs" : GROUP_SYSTEM,
    "perl" : GROUP_PROGRAMMING,
    "python" : GROUP_PROGRAMMING,
    "science" : GROUP_SCIENCE,
    "shells" : GROUP_SYSTEM,
    "sound" : GROUP_MULTIMEDIA,
    "tex" : GROUP_PUBLISHING,
    "text" : GROUP_PUBLISHING,
    "utils" : GROUP_ACCESSORIES,
    "web" : GROUP_INTERNET,
    "x11" : GROUP_DESKTOP_OTHER,
    "unknown" : GROUP_UNKNOWN,
    "alien" : GROUP_UNKNOWN,
    "translations" : GROUP_LOCALIZATION,
    "metapackages" : GROUP_COLLECTIONS }

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

DEFAULT_ENCODING = "UTF-8"

# Required to get translated descriptions
try:
    locale.setlocale(locale.LC_ALL, "")
except locale.Error:
    pklog.debug("Failed to unset LC_ALL")

# Allows to write unicode to stdout
import codecs
sys.stdout = codecs.getwriter(DEFAULT_ENCODING)(sys.stdout)

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
        backend.status(STATUS_WAITING_FOR_LOCK)
        while True:
            try:
                # see if the lock for the download dir can be acquired
                # (work around bug in python-apt/apps that call _fetchArchives)
                lockfile = apt_pkg.Config.FindDir("Dir::Cache::Archives") + \
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


class DpkgInstallProgress(apt.progress.InstallProgress):
    """
    Class to initiate and monitor installation of local package files with dpkg
    """
    #FIXME: Use the merged DpkgInstallProgress of python-apt
    def recover(self):
        """
        Run "dpkg --configure -a"
        """
        cmd = ["/usr/bin/dpkg", "--status-fd", str(self.writefd),
               "--root", apt_pkg.Config["Dir"],
               "--force-confdef", "--force-confold", 
               "--configure", "-a"]
        self.run(cmd)

    def install(self, filenames):
        """
        Install the given package using a dpkg command line call
        """
        cmd = ["/usr/bin/dpkg", "--force-confdef", "--force-confold",
               "--status-fd", str(self.writefd), 
               "--root", apt_pkg.Config["Dir"], "-i"]
        cmd.extend(map(lambda f: str(f), filenames))
        self.run(cmd)

    def run(self, cmd):
        """
        Run and monitor a dpkg command line call
        """
        pklog.debug("Executing: %s" % cmd)
        (self.master_fd, slave) = pty.openpty()
        fcntl.fcntl(self.master_fd, fcntl.F_SETFL, os.O_NONBLOCK)
        p = subprocess.Popen(cmd, stdout=slave, stdin=slave)
        self.child_pid = p.pid
        res = self.waitChild()
        return res

    def updateInterface(self):
        """
        Process status messages from dpkg
        """
        if self.statusfd == None:
            return
        try:
            while not self.read.endswith("\n"):
                self.read += os.read(self.statusfd.fileno(), 1)
        except OSError, (error_no, error_str):
            # resource temporarly unavailable is ignored
            if error_no not in [errno.EAGAIN, errno.EWOULDBLOCK]:
                pklog.warn(error_str)
        if self.read.endswith("\n"):
            statusl = string.split(self.read, ":")
            if len(statusl) < 3:
                pklog.warn("got garbage from dpkg: '%s'" % self.read)
                self.read = ""
            status = statusl[2].strip()
            pkg = statusl[1].strip()
            #print status
            if status == "error":
                self.error(pkg, format_string(status))
            elif status == "conffile-prompt":
                # we get a string like this:
                # 'current-conffile' 'new-conffile' useredited distedited
                match = re.search(".+conffile-prompt : '(.+)' '(.+)'",
                                  self.read)
                self.conffile(match.group(1), match.group(2))
            else:
                pklog.debug("Dpkg status: %s" % status)
                self.status = status
            self.read = ""


class PackageKitOpProgress(apt.progress.OpProgress):
    """
    Handle the cache opening process
    """
    def __init__(self, backend, prange=(0,100), progress=True):
        self._backend = backend
        apt.progress.OpProgress.__init__(self)
        self.steps = []
        for v in [0.12, 0.25, 0.50, 0.75, 1.00]:
            s = prange[0] + (prange[1] - prange[0]) * v
            self.steps.append(s)
        self.pstart = float(prange[0])
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


class PackageKitFetchProgress(apt.progress.FetchProgress):
    """
    Handle the package download process
    """
    def __init__(self, backend, prange=(0,100), status=STATUS_DOWNLOAD):
        self._backend = backend
        apt.progress.FetchProgress.__init__(self)
        self.start_progress = prange[0]
        self.end_progress = prange[1]
        self.last_progress = None
        self.last_sub_progress = None
        self.status = status
        self.package_states = {}

    def pulse_items(self, items):
        apt.progress.FetchProgress.pulse(self)
        progress = int(self.start_progress + self.percent/100 * \
                       (self.end_progress - self.start_progress))
        # A backwards running progress is reported as a not available progress
        if self.last_progress > progress:
            self._backend.percentage()
        else:
            self._backend.percentage(progress)
            self.last_progress = progress
        for item in items:
            uri, desc, shortdesc, file_size, partial_size = item
            try:
                pkg = self._backend._cache[shortdesc]
            except KeyError:
                pass
            else:
                self._backend._emit_package(pkg, INFO_DOWNLOADING, True)
                sub_progress = partial_size * 100 / file_size
                if sub_progress > self.last_sub_progress:
                    self._last_sub_progress = sub_progress
                    self._backend.sub_percentage(sub_progress)
        return True

    def updateStatus(self, uri, descr, pkg_name, status):
        """Callback for a fetcher status update."""
        # Emit a Package signal for the currently processed package
        try:
            pkg = self._backend._cache[pkg_name]
        except KeyError:
            pass
        else:
            if not pkg_name in self.package_states or \
               self.package_states[pkg_name] != status:
                if status == 0:
                    info = INFO_FINISHED
                else:
                    info = INFO_DOWNLOADING
                self.package_states[pkg_name] = status
                self._backend._emit_package(pkg, info, True)

    def start(self):
        self._backend.status(self.status)
        self._backend.allow_cancel(True)

    def stop(self):
        self._backend.percentage(self.end_progress)
        self._backend.allow_cancel(False)

    def mediaChange(self, medium, drive):
        #FIXME: Perhaps use hal to show a nicer drive name
        self._backend.media_change_required(MEDIA_TYPE_DISC, medium, drive)
        # FIXME: We cannot call sys.exit() here. APT module would procduce
        #        a backend error message otherwise. This way the backend
        #        sends another error message in the FetchFailedError handling
        #        later, but this one will be skipped by the daemon
        self._backend.error(ERROR_MEDIA_CHANGE_REQUIRED,
                            "Insert the CDROM or DVD labeled '%s' "
                            "into drive '%s'" % (medium, drive),
                            exit=False)
        return False


class PackageKitInstallProgress(apt.progress.InstallProgress):
    """
    Handle the installation and removal process. Bits taken from
    DistUpgradeViewNonInteractive.
    """
    def __init__(self, backend, prange=(0,100)):
        apt.progress.InstallProgress.__init__(self)
        self._backend = backend
        self.pstart = prange[0]
        self.pend = prange[1]
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

    def statusChange(self, pkg_name, percent, status):
        self.last_activity = time.time()
        progress = self.pstart + percent/100 * (self.pend - self.pstart)
        if self.pprev < progress:
            self._backend.percentage(int(progress))
            self.pprev = progress
        # Emit a Package signal for the currently processed package
        if pkg_name != self.last_pkg and self._backend._cache.has_key(pkg_name):
            pkg = self._backend._cache[pkg_name]
            if pkg.markedInstall or pkg.markedReinstall:
                self._backend._emit_package(pkg, INFO_INSTALLING, True)
            elif pkg.markedDelete:
                self._backend._emit_package(pkg, INFO_REMOVING, False)
            elif pkg.markedUpgrade:
                self._backend._emit_package(pkg, INFO_UPDATING, True)
            elif pkg.markedDowngrade:
                self._backend._emit_package(pkg, INFO_DOWNGRADING, True)
            self.last_pkg = pkg_name
        pklog.debug("APT status: %s" % status)

    def startUpdate(self):
        # The apt system lock was set by _lock_cache() before
        self._backend._unlock_cache()
        self._backend.status(STATUS_COMMIT)
        self.last_activity = time.time()
        self.start_time = time.time()

    def fork(self):
        pklog.debug("fork()")
        (pid, self.master_fd) = pty.fork()
        if pid != 0:
            fcntl.fcntl(self.master_fd, fcntl.F_SETFL, os.O_NONBLOCK)
        return pid

    def updateInterface(self):
        apt.progress.InstallProgress.updateInterface(self)
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

    def finishUpdate(self):
        pklog.debug("finishUpdate()")
        if self.conffile_prompts:
            self._backend.message(MESSAGE_CONFIG_FILES_CHANGED, 
                                  "The following conffile prompts were found "
                                  "and need investigation: %s" % \
                                  "\n".join(self.conffile_prompts))
        # Check for required restarts
        if os.path.exists("/var/run/reboot-required") and \
           os.path.getmtime("/var/run/reboot-required") > self.start_time:
            self._backend.require_restart(RESTART_SYSTEM, "")


class PackageKitDpkgInstallProgress(DpkgInstallProgress,
                                    PackageKitInstallProgress):
    """
    Class to integrate the progress of core dpkg operations into PackageKit
    """
    def run(self, filenames):
        return DpkgInstallProgress.run(self, filenames)

    def updateInterface(self):
        DpkgInstallProgress.updateInterface(self)
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
    """
    PackageKit backend for apt
    """
    def __init__(self, args):
        pklog.info("Initializing APT backend")
        signal.signal(signal.SIGQUIT, self._sigquit)
        self._cache = None
        self._last_cache_refresh = None
        apt_pkg.InitConfig()
        apt_pkg.Config.Set("DPkg::Options::", '--force-confdef')
        apt_pkg.Config.Set("DPkg::Options::", '--force-confold')
        PackageKitBaseBackend.__init__(self, args)
        self._open_cache(progress=False)

    # Methods ( client -> engine -> backend )

    def search_file(self, filters, filenames_string):
        """Search for files in packages.

        Works only for installed file if apt-file isn't installed.
        """
        pklog.info("Searching for file: %s" % filenames_string)
        self.status(STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)

        filenames = filenames_string.split("&")
        result_names = set()
        # Optionally make use of apt-file's Contents cache to search for not
        # installed files. But still search for installed files additionally
        # to make sure that we provide up-to-date results
        if os.path.exists("/usr/bin/apt-file") and \
           FILTER_INSTALLED not in filters.split(";"):
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
                self.error(ERROR_INTERNAL_ERROR,
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
            for installed_file in self._get_installed_files(pkg):
                if files_pattern.match(installed_file):
                    self._emit_visible_package(filters, pkg)
                    break

    def search_group(self, filters, group):
        """
        Implement the apt2-search-group functionality
        """
        pklog.info("Searching for group: %s" % group)
        self.status(STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)

        for pkg in self._cache:
            if self._get_package_group(pkg) == group:
                self._emit_visible_package(filters, pkg)

    def search_name(self, filters, values):
        """
        Implement the apt2-search-name functionality
        """
        def matches(searches, text):
            for search in searches:
                if not search in text:
                    return False
            return True
        pklog.info("Searching for package name: %s" % values)
        self.status(STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)

        for pkg_name in self._cache.keys():
            if matches(values, pkg_name):
                self._emit_all_visible_pkg_versions(filters,
                                                    self._cache[pkg_name])

    def search_details(self, filters, values):
        """
        Implement the apt2-search-details functionality
        """
        pklog.info("Searching for package details: %s" % values)
        self.status(STATUS_QUERY)
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
            query = parser.parse_query(u" ".join(values), search_flags)
            enquire = xapian.Enquire(db)
            enquire.set_query(query)
            matches = enquire.get_mset(0, 1000)
            for pkg_name in (match[xapian.MSET_DOCUMENT].get_data() \
                             for match in enquire.get_mset(0,1000)):
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
                if matches(values, txt.decode(DEFAULT_ENCODING, "replace")):
                    self._emit_visible_package(filters, pkg)

    def get_distro_upgrades(self):
        """
        Implement the {backend}-get-distro-upgrades functionality
        """
        pklog.info("Get distro upgrades")
        self.status(STATUS_INFO)
        self.allow_cancel(False)
        self.percentage(None)

        if META_RELEASE_SUPPORT == False:
            if self._cache.has_key("update-manager-core") and \
               self._cache["update-manager-core"].isInstalled == False:
                self.error(ERROR_INTERNAL_ERROR,
                           "Please install the package update-manager-core to "
                           "get notified of the latest distribution releases.")
            else:
                self.error(ERROR_INTERNAL_ERROR,
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
        """
        Implement the {backend}-get-update functionality.

        Only report updates which can be installed safely: Which can depend
        on the installation of additional packages but which don't require
        the removal of already installed packages or block any other update.
        """
        def succeeds_security_update(pkg):
            """
            Return True if an update succeeds a previous security update

            An example would be a package with version 1.1 in the security
            archive and 1.1.1 in the archive of proposed updates or the
            same version in both archives.
            """
            for version in pkg.versions:
                # Only check versions between the installed and the candidate
                if pkg.installed and \
                   apt_pkg.VersionCompare(version.version,
                                          pkg.installed.version) <= 0 and \
                   apt_pkg.VersionCompare(version.version,
                                          pkg.candidate.version) > 0:
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
        self.status(STATUS_QUERY)
        self.allow_cancel(True)
        self.percentage(None)
        self._check_init(progress=False)
        # Start with a safe upgrade
        self._cache.upgrade()
        upgrades_safe = self._cache.getChanges()
        resolver = apt.cache.ProblemResolver(self._cache)
        for upgrade in upgrades_safe:
            resolver.clear(upgrade)
            resolver.protect(upgrade)
        # Search for upgrades which are not already part of the safe upgrade
        # but would only require the installation of additional packages
        for pkg in self._cache:
            if not pkg.isUpgradable:
                continue
            # This may occur on pinned packages which have been updated to
            # later version than the pinned one
            if not pkg.candidateOrigin:
                continue
            pklog.debug("Checking upgrade of %s" % pkg.name)
            if not pkg in upgrades_safe:
                # Check if the upgrade would require the removal of an already
                # installed package. If this is the case it will be skipped
                resolver.clear(pkg)
                resolver.protect(pkg)
                resolver.install_protect()
                try:
                    resolver.resolve()
                except:
                    self._emit_package(pkg, INFO_BLOCKED, force_candidate=True)
                    resolver.clear(pkg)
                    self._cache.clear()
                    continue
                if self._cache.delete_count:
                    self._emit_package(pkg, INFO_BLOCKED, force_candidate=True)
                    resolver.clear(pkg)
                    self._cache.clear()
                    continue
            # The update can be safely installed
            info = INFO_NORMAL
            # Detect the nature of the upgrade (e.g. security, enhancement)
            archive = pkg.candidateOrigin[0].archive
            origin = pkg.candidateOrigin[0].origin
            trusted = pkg.candidateOrigin[0].trusted
            label = pkg.candidateOrigin[0].label
            if origin in ["Debian", "Ubuntu"] and trusted == True:
                if archive.endswith("-security") or \
                    label == "Debian-Security":
                    info = INFO_SECURITY
                elif succeeds_security_update(pkg):
                    pklog.debug("Update of %s succeeds a security update. "
                                "Raising its priority." % pkg.name)
                    info = INFO_SECURITY
                elif archive.endswith("-backports"):
                    info = INFO_ENHANCEMENT
                elif archive.endswith("-updates"):
                    info = INFO_BUGFIX
            if origin in ["Backports.org archive"] and trusted == True:
                info = INFO_ENHANCEMENT
            self._emit_package(pkg, info, force_candidate=True)
        self._cache.clear()

    def get_update_detail(self, pkg_ids):
        """
        Implement the {backend}-get-update-details functionality
        """
        def get_bug_urls(changelog):
            """
            Create a list of urls pointing to closed bugs in the changelog
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
            """
            Create a list of urls pointing to cves referred in the changelog
            """
            return map(lambda c: HREF_CVE % c,
                       re.findall(MATCH_CVE, changelog, re.MULTILINE))

        pklog.info("Get update details of %s" % pkg_ids)
        self.status(STATUS_DOWNLOAD_CHANGELOG)
        self.percentage(0)
        self.allow_cancel(True)
        self._check_init(None)
        total = len(pkg_ids)
        count = 1
        for pkg_id in pkg_ids:
            self.percentage(count * 100 / total)
            count += 1
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
            update_text = u""
            state = ""
            issued = ""
            updated = ""
            #FIXME: make this more configurable. E.g. a dbus update requires
            #       a reboot on Ubuntu but not on Debian
            if pkg.name.startswith("linux-image-") or \
               pkg.name in ["libc6", "dbus"]:
                restart == RESTART_SYSTEM
            changelog_raw = pkg.getChangelog()
            # The internal download error string of python-apt ist not
            # provided as unicode object
            if not isinstance(changelog_raw, unicode):
                changelog_raw = changelog_raw.decode(DEFAULT_ENCODING)
            # Convert the changelog to markdown syntax
            changelog = u""
            for line in changelog_raw.split("\n"):
                if line == "":
                    changelog += " \n"
                else:
                    changelog += u"    %s  \n" % line
                if line.startswith(pkg.candidate.source_name):
                    match = re.match(r"(?P<source>.+) \((?P<version>.*)\) "
                                      "(?P<dist>.+); urgency=(?P<urgency>.+)",
                                     line)
                    update_text += u"%s\n%s\n\n" % (match.group("version"),
                                                    "=" * \
                                                    len(match.group("version")))
                elif line.startswith("  "):
                    update_text += u"  %s  \n" % line
                elif line.startswith(" --"):
                    #FIXME: Add %z for the time zone - requires Python 2.6
                    update_text += u"  \n"
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
        """
        Implement the {backend}-get-details functionality
        """
        pklog.info("Get details of %s" % pkg_ids)
        self.status(STATUS_INFO)
        self.percentage(None)
        self.allow_cancel(True)
        self._check_init(progress=False)
        for pkg_id in pkg_ids:
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
                         pkg.homepage.decode(DEFAULT_ENCODING),
                         pkg.packageSize)

    @lock_cache
    def update_system(self, only_trusted):
        """
        Implement the {backend}-update-system functionality
        """
        pklog.info("Upgrading system")
        self.status(STATUS_UPDATE)
        self.allow_cancel(False)
        self.percentage(0)
        self._check_init(prange=(0,5))
        # Start with protecting all safe upgrades
        self._cache.upgrade()
        upgrades_safe = self._cache.getChanges()
        resolver = apt.cache.ProblemResolver(self._cache)
        for upgrade in upgrades_safe:
            resolver.clear(upgrade)
            resolver.protect(upgrade)
        # Search for upgrades which are not already part of the safe upgrade
        # but would only require the installation of additional packages
        for pkg in self._cache:
            if not pkg.isUpgradable or pkg in upgrades_safe:
                continue
            pklog.debug("Checking upgrade of %s" % pkg.name)
            resolver.clear(pkg)
            resolver.protect(pkg)
            resolver.install_protect()
            try:
                resolver.resolve()
            except:
                resolver.clear(pkg)
                self._cache.clear()
                continue
            if self._cache.delete_count:
                resolver.clear(pkg)
                self._cache.clear()
                continue
        resolver.install_protect()
        resolver.resolve()
        self._check_trusted(only_trusted)
        self._commit_changes()

    @lock_cache
    def remove_packages(self, allow_deps, auto_remove, ids):
        """
        Implement the {backend}-remove functionality
        """
        pklog.info("Removing package(s): id %s" % ids)
        self.status(STATUS_REMOVE)
        self.allow_cancel(False)
        self.percentage(0)
        self._check_init(prange=(0,10))
        if auto_remove:
            auto_removables = [pkg.name for pkg in self._cache \
                               if pkg.isAutoRemovable]
        pkgs = self._mark_for_removal(ids)
        # Error out if the installation would the installation or upgrade of
        # other packages
        if self._cache.install_count:
            installed = [pkg.name for pkg in self._cache.getChanges() if \
                         pkg.markedInstall or pkg.markedUpgrade]
            self.error(ERROR_DEP_RESOLUTION_FAILED,
                       "The following packages would have to upgraded or "
                       "installed and so block the removal: "
                       "%s" % " ".join(installed))
        # Check if the removal would remove further packages
        if not allow_deps and self._cache.delete_count != len(ids):
            dependencies = [pkg.name for pkg in self._cache.getChanges() \
                            if pkg.name not in pkgs]
            self.error(ERROR_DEP_RESOLUTION_FAILED,
                       "The following packages would have also to be removed: "
                       "%s" % " ".join(dependencies))
        # Check for no longer required dependencies which should be removed too
        if auto_remove:
            for pkg in self._cache:
                if pkg.isAutoRemovable and pkg.name not in auto_removables:
                    pkg.markDelete(False)
        #FIXME: Should support only_trusted
        self._commit_changes(fetch_range=(10,10), install_range=(10,90))
        self._open_cache(prange=(90,99))
        for p in pkgs:
            if self._cache.has_key(p) and self._cache[p].isInstalled:
                self.error(ERROR_PACKAGE_FAILED_TO_INSTALL,
                           "%s is still installed" % p)
        self.percentage(100)

    def simulate_remove_packages(self, ids):
        """Emit the change required for the removal of the given packages."""
        pklog.info("Simulating removal of package with id %s" % ids)
        self.status(STATUS_DEP_RESOLVE)
        self.allow_cancel(True)
        self.percentage(None)
        self._check_init(progress=False)
        pkgs = self._mark_for_removal(ids)
        self._emit_changes(pkgs)

    def _mark_for_removal(self, ids):
        """Resolve the given package ids and mark the packages for removal."""
        pkgs = []
        action_group = self._cache.actiongroup()
        resolver = apt.cache.ProblemResolver(self._cache)
        for id in ids:
            version = self._get_version_by_id(id)
            pkg = version.package
            if not pkg.isInstalled:
                self.error(ERROR_PACKAGE_NOT_INSTALLED,
                           "Package %s isn't installed" % pkg.name)
            if pkg.installed != version:
                self.error(ERROR_PACKAGE_NOT_INSTALLED,
                           "Version %s of %s isn't installed" % \
                           (version.version, pkg.name))
            if pkg.essential == True:
                self.error(ERROR_CANNOT_REMOVE_SYSTEM_PACKAGE,
                           "Package %s cannot be removed." % pkg.name)
            pkgs.append(pkg.name[:])
            pkg.markDelete(False, False)
            resolver.clear(pkg)
            resolver.protect(pkg)
            resolver.remove(pkg)
        try:
            resolver.resolve()
        except SystemError, error:
            broken = [pkg.name for pkg in self._cache if pkg.is_inst_broken]
            self.error(ERROR_DEP_RESOLUTION_FAILED,
                       "The following packages would break and so block the "
                       "removal: %s" % " ".join(broken))
        action_group.release()
        return pkgs

    def get_repo_list(self, filters):
        """
        Implement the {backend}-get-repo-list functionality

        FIXME: should we use the abstration of software-properties or provide
               low level access using pure aptsources?
        """
        pklog.info("Getting repository list: %s" % filters)
        self.status(STATUS_INFO)
        self.allow_cancel(False)
        self.percentage(0)
        if REPOS_SUPPORT == False:
            if self._cache.has_key("python-software-properties") and \
               self._cache["python-software-properties"].isInstalled == False:
                self.error(ERROR_INTERNAL_ERROR,
                           "Please install the package "
                           "python-software-properties to handle repositories")
            else:
                self.error(ERROR_INTERNAL_ERROR,
                           "Please make sure that python-software-properties is"
                           "correctly installed.")
        filter_list = filters.split(";")
        repos = PackageKitSoftwareProperties()
        # Emit distro components as virtual repositories
        for comp in repos.distro.source_template.components:
            repo_id = "%s_comp_%s" % (repos.distro.id, comp.name)
            description = "%s %s - %s (%s)" % (repos.distro.id,
                                               repos.distro.release,
                                               comp.get_description(),
                                               comp.name)
            #FIXME: There is no inconsitent state in PackageKit
            enabled = repos.get_comp_download_state(comp)[0]
            if not FILTER_DEVELOPMENT in filter_list:
                self.repo_detail(repo_id,
                                 description.decode(DEFAULT_ENCODING),
                                 enabled)
        # Emit distro's virtual update repositories
        for template in repos.distro.source_template.children:
            repo_id = "%s_child_%s" % (repos.distro.id, template.name)
            description = "%s %s - %s (%s)" % (repos.distro.id,
                                               repos.distro.release,
                                               template.description,
                                               template.name)
            #FIXME: There is no inconsitent state in PackageKit
            enabled = repos.get_comp_child_state(template)[0]
            if not FILTER_DEVELOPMENT in filter_list:
                self.repo_detail(repo_id,
                                 description.decode(DEFAULT_ENCODING),
                                 enabled)
        # Emit distro's cdrom sources
        for source in repos.get_cdrom_sources():
            if FILTER_NOT_DEVELOPMENT in filter_list and \
               source.type in ("deb-src", "rpm-src"):
                continue
            enabled = not source.disabled
            # Remove markups from the description
            description = re.sub(r"</?b>", "", repos.render_source(source))
            repo_id = "cdrom_%s_%s" % (source.uri, source.dist)
            repo_id.join(map(lambda c: "_%s" % c, source.comps))
            self.repo_detail(repo_id, description.decode(DEFAULT_ENCODING),
                             enabled)
        # Emit distro's virtual source code repositoriy
        if not FILTER_NOT_DEVELOPMENT in filter_list:
            repo_id = "%s_source" % repos.distro.id
            enabled = repos.get_source_code_state() or False
            #FIXME: no translation :(
            description = "%s %s - Source code" % (repos.distro.id,
                                                   repos.distro.release)
            self.repo_detail(repo_id, description.decode(DEFAULT_ENCODING),
                             enabled)
        # Emit third party repositories
        for source in repos.get_isv_sources():
            if FILTER_NOT_DEVELOPMENT in filter_list and \
               source.type in ("deb-src", "rpm-src"):
                continue
            enabled = not source.disabled
            # Remove markups from the description
            description = re.sub(r"</?b>", "", repos.render_source(source))
            repo_id = "isv_%s_%s" % (source.uri, source.dist)
            repo_id.join(map(lambda c: "_%s" % c, source.comps))
            self.repo_detail(repo_id, description.decode(DEFAULT_ENCODING),
                             enabled)

    def repo_enable(self, repo_id, enable):
        """
        Implement the {backend}-repo-enable functionality

        FIXME: should we use the abstration of software-properties or provide
               low level access using pure aptsources?
        """
        pklog.info("Enabling repository: %s %s" % (repo_id, enable))
        self.status(STATUS_RUNNING)
        self.allow_cancel(False)
        self.percentage(0)
        if REPOS_SUPPORT == False:
            if self._cache.has_key("python-software-properties") and \
               self._cache["python-software-properties"].isInstalled == False:
                self.error(ERROR_INTERNAL_ERROR,
                           "Please install the package "
                           "python-software-properties to handle repositories")
            else:
                self.error(ERROR_INTERNAL_ERROR,
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
        # Check if the repo_id matches a distro child repository, e.g. hardy-updates
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
                source_id.join(map(lambda c: "_%s" % c, source.comps))
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
                source_id.join(map(lambda c: "_%s" % c, source.comps))
                if repo_id == source_id:
                    if source.disabled == enable:
                        source.disabled = not enable
                        repos.save_sourceslist()
                    else:
                        pklog.debug("Repository is already enabled")
                    found = True
                    break
        if found == False:
            self.error(ERROR_REPO_NOT_AVAILABLE,
                       "The repository of the id %s isn't available" % repo_id)

    @lock_cache
    def update_packages(self, only_trusted, ids):
        """
        Implement the {backend}-update functionality
        """
        pklog.info("Updating package with id %s" % ids)
        self.status(STATUS_UPDATE)
        self.allow_cancel(False)
        self.percentage(0)
        self._check_init(prange=(0,10))
        pkgs = self._mark_for_upgrade(ids)
        # Error out if the updates would require the removal of already
        # installed packages
        if self._cache.delete_count:
            deleted = [pkg.name for pkg in self._cache.getChanges() if \
                       pkg.markedDelete]
            self.error(ERROR_DEP_RESOLUTION_FAILED,
                       "The following packages block the update: "
                       "%s" % " ".join(deleted))
        self._check_trusted(only_trusted)
        self._commit_changes()
        self._open_cache(prange=(90,100))
        self.percentage(100)
        pklog.debug("Checking success of operation")
        for p in pkgs:
            if not self._cache.has_key(p) or not self._cache[p].isInstalled \
               or self._cache[p].isUpgradable:
                self.error(ERROR_PACKAGE_FAILED_TO_INSTALL,
                           "%s was not updated" % p)
        pklog.debug("Sending success signal")

    def simulate_update_packages(self, ids):
        """Emit the changes required for the upgrade of the given packages."""
        pklog.info("Simulating update of package with id %s" % ids)
        self.status(STATUS_DEP_RESOLVE)
        self.allow_cancel(True)
        self.percentage(None)
        self._check_init(progress=False)
        pkgs = self._mark_for_upgrade(ids)
        self._emit_changes(pkgs)

    def _mark_for_upgrade(self, ids):
        """Resolve the given package ids and mark the packages for upgrade."""
        pkgs = []
        ac = self._cache.actiongroup()
        resolver = apt.cache.ProblemResolver(self._cache)
        for id in ids:
            version = self._get_version_by_id(id)
            pkg = version.package
            if not pkg.isInstalled:
                self.error(ERROR_PACKAGE_NOT_INSTALLED,
                           "%s isn't installed" % pkg.name)
            # Check if the specified version is an update
            if apt_pkg.VersionCompare(pkg.installed.version,
                                      version.version) >= 0:
                self.error(ERROR_UPDATE_NOT_FOUND,
                           "The version %s of %s isn't an update to the "
                           "current %s" % (version.version, pkg.name,
                                           pkg.installed.version))
            pkg.candidate = version
            pkgs.append(pkg.name[:])
            # Actually should be fixed in python-apt
            auto = not self._cache._depcache.IsAutoInstalled(pkg._pkg)
            pkg.markInstall(False, True, auto)
            resolver.clear(pkg)
            resolver.protect(pkg)
        try:
            resolver.resolve()
        except SystemError, error:
            broken = [pkg.name for pkg in self._cache if pkg.is_inst_broken]
            self.error(ERROR_DEP_RESOLUTION_FAILED,
                       "The following packages block the installation: "
                       "%s" % " ".join(broken))
        ac.release()
        return pkgs

    def download_packages(self, dest, ids):
        """
        Implement the {backend}-download-packages functionality
        """
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
                    self.error(ERROR_PACKAGE_DOWNLOAD_FAILED,
                               "package %s isn't downloadable" % id)
                total += pkg_ver.size
                versions.append((id, pkg_ver))
            for id, ver in versions:
                start = downloaded * 100 / total
                end = start + ver.size * 100 / total
                yield id, ver, start, end
                downloaded += ver.size
        pklog.info("Downloading packages: %s" % ids)
        self.status(STATUS_DOWNLOAD)
        self.allow_cancel(True)
        self.percentage(0)
        # Check the destination directory
        if not os.path.isdir(dest) or not os.access(dest, os.W_OK):
            self.error(ERROR_INTERNAL_ERROR,
                       "The directory '%s' is not writable" % dest)
        # Setup the fetcher
        self._check_init(prange=(0,10))
        # Start the download
        for id, ver, start, end in get_download_details(ids):
            progress = PackageKitFetchProgress(self, prange=(start, end))
            self._emit_pkg_version(ver, INFO_DOWNLOADING)
            try:
                ver.fetch_binary(dest, progress)
            except Exception, error:
                self.error(ERROR_PACKAGE_DOWNLOAD_FAILED,
                           format_string(error.message))
            else:
                self.files(id, os.path.join(dest,
                                            os.path.basename(ver.filename)))
                self._emit_pkg_version(ver, INFO_FINISHED)
        self.percentage(100)

    @lock_cache
    def install_packages(self, only_trusted, ids):
        """
        Implement the {backend}-install functionality
        """
        pklog.info("Installing package with id %s" % ids)
        self.status(STATUS_INSTALL)
        self.allow_cancel(False)
        self.percentage(0)
        self._check_init(prange=(0,10))
        pkgs = self._mark_for_installation(ids)
        # Error out if the installation would require the removal of already
        # installed packages
        if self._cache.delete_count:
            deleted = [pkg.name for pkg in self._cache.getChanges() if \
                       pkg.markedDelete]
            self.error(ERROR_DEP_RESOLUTION_FAILED,
                       "The following packages block the update: "
                       "%s" % " ".join(deleted))
        self._check_trusted(only_trusted)
        self._commit_changes()
        self._open_cache(prange=(90,100))
        self.percentage(100)
        pklog.debug("Checking success of operation")
        for p in pkgs:
            if not self._cache.has_key(p) or not self._cache[p].isInstalled:
                self.error(ERROR_PACKAGE_FAILED_TO_INSTALL,
                           "%s was not installed" % p)

    def simulate_install_packages(self, ids):
        """Emit the changes required for the installation of the given
        packages.
        """
        pklog.info("Simulating installing package with id %s" % ids)
        self.status(STATUS_DEP_RESOLVE)
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
        ac = self._cache.actiongroup()
        resolver = apt.cache.ProblemResolver(self._cache)
        for id in ids:
            version = self._get_version_by_id(id)
            pkg = version.package
            pkg.candidate = version
            if pkg.installed == version:
                self.error(ERROR_PACKAGE_ALREADY_INSTALLED,
                           "Package %s is already installed" % pkg.name)
            pkgs.append(pkg.name[:])
            pkg.markInstall(False, True, True)
            resolver.clear(pkg)
            resolver.protect(pkg)
        try:
            resolver.resolve()
        except SystemError, error:
            broken = [pkg.name for pkg in self._cache if pkg.is_inst_broken]
            self.error(ERROR_DEP_RESOLUTION_FAILED,
                       "The following packages block the installation: "
                       "%s" % " ".join(broken))
        ac.release()
        return pkgs

    @lock_cache
    def install_files(self, only_trusted, inst_files):
        """
        Implement install-files for the apt backend
        Install local Debian package files
        """
        pklog.info("Installing package files: %s" % inst_files)
        self.status(STATUS_INSTALL)
        self.allow_cancel(False)
        self.percentage(0)
        self._check_init(prange=(0,10))
        packages = []
        # Collect all dependencies which need to be installed
        self.status(STATUS_DEP_RESOLVE)
        for path in inst_files:
            deb = apt.debfile.DebPackage(path, self._cache)
            packages.append(deb)
            if not deb.check():
                self.error(ERROR_LOCAL_INSTALL_FAILED,
                           format_string(deb._failureString))
            (install, remove, unauthenticated) = deb.required_changes
            pklog.debug("Changes: Install %s, Remove %s, Unauthenticated "
                        "%s" % (install, remove, unauthenticated))
            if len(remove) > 0:
                self.error(ERROR_DEP_RESOLUTION_FAILED,
                           "Remove the following packages "
                           "before: %s" % remove)
            if deb.compare_to_version_in_cache() == \
               apt.debfile.VERSION_OUTDATED:
                self.message(MESSAGE_NEWER_PACKAGE_EXISTS,
                             "There is a later version of %s "
                             "available in the repositories." % deb.pkgname)
        if self._cache.getChanges():
            self._check_trusted(only_trusted)
            self._commit_changes((10,25), (25,50))
        # Install the Debian package files
        d = PackageKitDpkgInstallProgress(self)
        try:
            d.startUpdate()
            d.install([inst.encode(DEFAULT_ENCODING) for inst in inst_files])
            d.finishUpdate()
        except InstallTimeOutPKError, e:
            self._recover()
            #FIXME: should provide more information
            msg = "Transaction was cancelled since the installation " \
                  "of a package hung.\n" \
                  "This can be caused by maintainer scripts which " \
                  "require input on the terminal:\n%s" % e.message
            self.error(ERROR_INTERNAL_ERROR, format_string(msg))
        except PackageManagerFailedPKError, e:
            self._recover()
            self.error(ERROR_INTERNAL_ERROR,
                       format_string("%s\n%s" % (e.message, e.output)))
        except Exception, e:
            self._recover()
            self.error(ERROR_INTERNAL_ERROR, format_string(e.message))
        self.percentage(100)

    def simulate_install_files(self, inst_files):
        """Emit the change required for the installation of the given package
        files.
        """
        pklog.info("Simulating installation of the package files: "
                   "%s" % inst_files)
        self.status(STATUS_DEP_RESOLVE)
        self.allow_cancel(True)
        self.percentage(None)
        self._check_init(progress=False)
        pkgs = []
        for path in inst_files:
            deb = apt.debfile.DebPackage(path, self._cache)
            pkgs.append(deb.pkgname)
            if not deb.check():
                self.error(ERROR_LOCAL_INSTALL_FAILED,
                           format_string(deb._failureString))
        self._emit_changes(pkgs)

    @lock_cache
    def refresh_cache(self, force):
        """
        Implement the {backend}-refresh_cache functionality
        """
        # TODO: use force ?
        pklog.info("Refresh cache")
        self.status(STATUS_REFRESH_CACHE)
        self.last_action_time = time.time()
        self.allow_cancel(False);
        self.percentage(0)
        self._check_init((0,10))
        progress = PackageKitFetchProgress(self, prange=(10,95),
                                           status=STATUS_DOWNLOAD_REPOSITORY)
        try:
            ret = self._cache.update(progress)
        except Exception, error:
            # FIXME: Unluckily python-apt doesn't provide a real good error
            #        reporting. We only receive a failure string.
            # FIXME: Doesn't detect if all downloads failed - bug in python-apt
            self.message(MESSAGE_REPO_METADATA_DOWNLOAD_FAILED,
                         format_string(error.message))
        self._open_cache(prange=(95,100))
        self.percentage(100)

    def get_packages(self, filters):
        """
        Implement the apt2-get-packages functionality
        """
        pklog.info("Get all packages")
        self.status(STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)

        for pkg in self._cache:
            if self._is_package_visible(pkg, filters):
                self._emit_package(pkg)

    def resolve(self, filters, names):
        """
        Implement the apt2-resolve functionality
        """
        pklog.info("Resolve")
        self.status(STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(False)

        for name_raw in names:
            #FIXME: Python-apt doesn't allow unicode as key. See #542965
            name = str(name_raw)
            if self._cache.has_key(name):
                self._emit_visible_package(filters, self._cache[name])
            else:
                self.error(ERROR_PACKAGE_NOT_FOUND,
                           "Package name %s could not be resolved" % name)

    def get_depends(self, filters, ids, recursive):
        """Emit all dependencies of the given package ids.

        Doesn't support recursive dependency resolution.
        """
        def emit_blocked_dependency(base_dependency, pkg=None,
                                    filters=""):
            """Send a blocked package signal for the given
            apt.package.BaseDependency.
            """
            filters_lst = filters.split(";")
            if FILTER_INSTALLED in filters_lst:
                return
            if pkg:
                summary = pkg.summary
                try:
                    filters.remove(FILTER_NOT_INSTALLED)
                except ValueError:
                    pass
                if not self._is_package_visible(pkg, filters):
                    return
            else:
                summary = u""
            if base_dependency.relation:
                version = "%s%s" % (base_dependency.relation,
                                    base_dependency.version)
            else:
                version = base_dependency.version
            self.package("%s;%s;;" % (base_dependency.name, version),
                         INFO_BLOCKED, unicode(summary, DEFAULT_ENCODING))

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
        self.status(STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)

        dependency_types = ["PreDepends", "Depends"]
        if apt_pkg.Config["APT::Install-Recommends"]:
            dependency_types.append("Recommends")
        for id in ids:
            version = self._get_version_by_id(id)
            for dependency in version.get_dependencies(*dependency_types):
                # Walk through all or_dependencies
                for base_dep in dependency.or_dependencies:
                    if self._cache.isVirtualPackage(base_dep.name):
                        # Check each proivider of a virtual package
                        for provider in \
                                self._cache.getProvidingPackages(base_dep.name):
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
        self.status(STATUS_DEP_RESOLVE)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(True)
        for id in ids:
            version = self._get_version_by_id(id)
            provided = [pro[0] for pro in version._cand.ProvidesList]
            for pkg in self._cache:
                if not self._is_package_visible(pkg, filters):
                    continue
                if pkg.isInstalled:
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
                if self._cache.has_key("app-install-data") and \
                   self._cache["app-install-data"].isInstalled == False:
                    self.error(ERROR_INTERNAL_ERROR,
                               "Please install the package "
                               "app-install data for a list of "
                               "applications that can handle files of "
                               "the given type")
                else:
                    self.error(ERROR_INTERNAL_ERROR,
                               "The list of applications that can handle "
                               "files of the given type cannot be opened.\n"
                               "Try to reinstall the package "
                               "app-install-data.")
                return
            try:
                db = gdbm.open(path)
            except:
                self.error(ERROR_INTERNAL_ERROR,
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
                self.error(ERROR_INTERNAL_ERROR,
                           "The search term is invalid: %s" % search)
            if match.group("opt"):
                caps_str = "%s, %s" % (match.group("data"), match.group("opt"))
                # gst.Caps.__init__ cannot handle unicode instances
                caps = gst.Caps(str(caps_str))
            record = GSTREAMER_RECORD_MAP[match.group("kind")]
            return match.group("version"), record, match.group("data"), caps
        self.status(STATUS_QUERY)
        self.percentage(None)
        self._check_init(progress=False)
        self.allow_cancel(False)
        if provides_type == PROVIDES_CODEC:
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

        elif provides_type == PROVIDES_MIMETYPE:
            # Emit packages that contain an application that can handle
            # the given mime type
            handlers = set()
            db = get_mapping_db("/var/lib/PackageKit/mime-map.gdbm")
            if db == None:
                return
            if db.has_key(search):
                pklog.debug("Mime type is registered: %s" % db[search])
                # The mime type handler db stores the packages as a string
                # separated by spaces. Each package has its section
                # prefixed and separated by a slash
                # FIXME: Should make use of the section and emit a 
                #        RepositoryRequired signal if the package does not exist
                handlers = map(lambda s: s.split("/")[1],
                               db[search].split(" "))
                self._emit_visible_packages_by_name(filters, handlers)
        else:
            self.error(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")

    def get_files(self, package_ids):
        """
        Emit the Files signal which includes the files included in a package
        Apt only supports this for installed packages
        """
        self.status(STATUS_INFO)
        for id in package_ids:
            pkg = self._get_package_by_id(id)
            files = string.join(self._get_installed_files(pkg), ";")
            self.files(id, files)

    # Helpers

    def _unlock_cache(self):
        """
        Unlock the system package cache
        """
        try:
            apt_pkg.PkgSystemUnLock()
        except SystemError:
            return False
        return True

    def _open_cache(self, prange=(0,100), progress=True):
        """
        (Re)Open the APT cache
        """
        pklog.debug("Open APT cache")
        self.status(STATUS_LOADING_CACHE)
        try:
            self._cache = apt.Cache(PackageKitOpProgress(self, prange,
                                                         progress))
        except:
            self.error(ERROR_NO_CACHE, "Package cache could not be opened")
        if self._cache.broken_count > 0:
            self.error(ERROR_DEP_RESOLUTION_FAILED,
                       "There are broken dependecies on your system. "
                       "Please use an advanced package manage e.g. "
                       "Synaptic or aptitude to resolve this situation.")
        self._last_cache_refresh = time.time()

    def _recover(self, prange=(95,100)):
        """
        Try to recover from a package manager failure
        """
        self.status(STATUS_CLEANUP)
        self.percentage(None)
        try:
            d = PackageKitDpkgInstallProgress(self)
            d.startUpdate()
            d.recover()
            d.finishUpdate()
        except:
            pass
        self._open_cache(prange)

    def _check_trusted(self, only_trusted):
        """Check if only trusted packages are allowed and fail if 
        untrusted packages would be installed in this case.
        """
        untrusted = []
        if only_trusted:
            for pkg in self._cache:
                if (pkg.markedInstall or pkg.markedUpgrade or
                    pkg.markedDowngrade or pkg.markedReinstall):
                     trusted = False
                     for origin in pkg.candidate.origins:
                          trusted |= origin.trusted
                     if not trusted:
                         untrusted.append(pkg.name)
            if untrusted:
                self.error(ERROR_MISSING_GPG_SIGNATURE, " ".join(untrusted))

    def _commit_changes(self, fetch_range=(5,50), install_range=(50,90)):
        """
        Commit changes to the cache and handle errors
        """
        try:
            self._cache.commit(PackageKitFetchProgress(self, fetch_range), 
                               PackageKitInstallProgress(self, install_range))
        except apt.cache.FetchFailedException, err:
            self._open_cache(prange=(95,100))
            pklog.critical(format_string(err.message))
            self.error(ERROR_PACKAGE_DOWNLOAD_FAILED,
                       format_string(err.message))
        except apt.cache.FetchCancelledException:
            self._open_cache(prange=(95,100))
        except InstallTimeOutPKError, err:
            self._recover()
            self._open_cache(prange=(95,100))
            #FIXME: should provide more information
            msg = "Transaction was cancelled since the installation " \
                  "of a package hung.\n" \
                  "This can be caused by maintainer scripts which " \
                  "require input on the terminal:\n%s" % err.message
            self.error(ERROR_INTERNAL_ERROR, format_string(msg))
        except PackageManagerFailedPKError, err:
            self._recover()
            self.error(ERROR_INTERNAL_ERROR,
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
 
    def _check_init(self, prange=(0,10), progress=True):
        """
        Check if the backend was initialized well and try to recover from
        a broken setup
        """
        pklog.debug("Checking apt cache and xapian database")
        pkg_cache = os.path.join(apt_pkg.Config["Dir"],
                                 apt_pkg.Config["Dir::Cache"],
                                 apt_pkg.Config["Dir::Cache::pkgcache"])
        src_cache = os.path.join(apt_pkg.Config["Dir"],
                                 apt_pkg.Config["Dir::Cache"],
                                 apt_pkg.Config["Dir::Cache::srcpkgcache"])
        # Check if the cache instance is of the coorect class type, contains
        # any broken packages and if the dpkg status or apt cache files have 
        # been changed since the last refresh
        if not isinstance(self._cache, apt.cache.Cache) or \
           (self._cache.broken_count > 0) or \
           (os.stat(apt_pkg.Config["Dir::State::status"])[stat.ST_MTIME] > \
            self._last_cache_refresh) or \
           (os.stat(pkg_cache)[stat.ST_MTIME] > self._last_cache_refresh) or \
           (os.stat(src_cache)[stat.ST_MTIME] > self._last_cache_refresh):
            pklog.debug("Reloading the cache is required")
            self._open_cache(prange, progress)
        else:
            pass
        # Read the pin file of Synaptic if available
        self._cache._depcache.ReadPinFile()
        if os.path.exists(SYNAPTIC_PIN_FILE):
            self._cache._depcache.ReadPinFile(SYNAPTIC_PIN_FILE)
        # Reset the depcache
        self._cache.clear()

    def _emit_package(self, pkg, info=None, force_candidate=False):
        """
        Send the Package signal for a given apt package
        """
        if (not pkg.isInstalled or force_candidate) and pkg.candidate:
            self._emit_pkg_version(pkg.candidate, info)
        elif pkg.isInstalled:
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
                    info = INFO_COLLECTION_INSTALLED
                else:
                    info = INFO_INSTALLED
            else:
                if section == "metapackages":
                    info = INFO_COLLECTION_AVAILABLE
                else:
                    info = INFO_AVAILABLE
        self.package(id, info, unicode(version.summary, DEFAULT_ENCODING))

    def _emit_all_visible_pkg_versions(self, filters, pkg):
        """Emit all available versions of a package."""
        if self._is_package_visible(pkg, filters):
            if FILTER_NEWEST in filters:
                if pkg.candidate:
                    self._emit_pkg_version(pkg.candidate)
                elif pkg.installed:
                    self._emit_pkg_version(pkg.installed)
            else:
                for version in pkg.versions:
                    self._emit_pkg_version(version)

    def _emit_visible_package(self, filters, pkg, info=None):
        """
        Filter and emit a package
        """
        if self._is_package_visible(pkg, filters):
            self._emit_package(pkg, info)

    def _emit_visible_packages(self, filters, pkgs, info=None):
        """
        Filter and emit packages
        """
        for p in pkgs:
            if self._is_package_visible(p, filters):
                self._emit_package(p, info)

    def _emit_visible_packages_by_name(self, filters, pkgs, info=None):
        """
        Find the packages with the given namens. Afterwards filter and emit
        them
        """
        for name_raw in pkgs:
            #FIXME: Python-apt doesn't allow unicode as key. See #542965
            name = str(name_raw)
            if self._cache.has_key(name) and \
               self._is_package_visible(self._cache[name], filters):
                self._emit_package(self._cache[name], info)

    def _emit_changes(self, ignore_pkgs=[]):
        """Emit all changed packages."""
        for pkg in self._cache:
            if pkg.name in ignore_pkgs:
                continue
            if pkg.markedDelete:
                self._emit_package(pkg, INFO_REMOVING, False)
            elif pkg.markedInstall:
                self._emit_package(pkg, INFO_INSTALLING, True)
            elif pkg.markedUpgrade:
                self._emit_package(pkg, INFO_UPDATING, True)
            elif pkg.markedDowngrade:
                self._emit_package(pkg, INFO_DOWNGRADING, True)
            elif pkg.markedReinstall:
                self._emit_package(pkg, INFO_REINSTALLING, True)

    def _is_package_visible(self, pkg, filters):
        """
        Return True if the package should be shown in the user interface
        """
        if filters == FILTER_NONE:
            return True
        for filter in filters.split(";"):
            if (filter == FILTER_INSTALLED and not pkg.isInstalled) or \
               (filter == FILTER_NOT_INSTALLED and pkg.isInstalled) or \
               (filter == FILTER_SUPPORTED and not \
                self._is_package_supported(pkg)) or \
               (filter == FILTER_NOT_SUPPORTED and \
                self._is_package_supported(pkg)) or \
               (filter == FILTER_FREE and not self._is_package_free(pkg)) or \
               (filter == FILTER_NOT_FREE and \
                not self._is_package_not_free(pkg)) or \
               (filter == FILTER_GUI and not self._has_package_gui(pkg)) or \
               (filter == FILTER_NOT_GUI and self._has_package_gui(pkg)) or \
               (filter == FILTER_COLLECTIONS and not \
                self._is_package_collection(pkg)) or \
               (filter == FILTER_NOT_COLLECTIONS and \
                self._is_package_collection(pkg)) or\
                (filter == FILTER_DEVELOPMENT and not \
                self._is_package_devel(pkg)) or \
               (filter == FILTER_NOT_DEVELOPMENT and \
                self._is_package_devel(pkg)):
                return False
        return True

    def _is_package_not_free(self, pkg):
        """
        Return True if we can be sure that the package's license isn't any 
        free one
        """
        candidate = pkg.candidateOrigin
        return candidate != None and \
               ((candidate[0].origin == "Ubuntu" and \
                 candidate[0].component in ["multiverse", "restricted"]) or \
                (candidate[0].origin == "Debian" and \
                 candidate[0].component in ["contrib", "non-free"])) and \
               candidate[0].trusted == True

    def _is_package_collection(self, pkg):
        """
        Return True if the package is a metapackge
        """
        section = pkg.section.split("/")[-1]
        return section == "metapackages"

    def _is_package_free(self, pkg):
        """
        Return True if we can be sure that the package has got a free license
        """
        candidate = pkg.candidateOrigin
        return candidate != None and \
               ((candidate[0].origin == "Ubuntu" and \
                 candidate[0].component in ["main", "universe"]) or \
                (candidate[0].origin == "Debian" and \
                 candidate[0].component == "main")) and\
               candidate[0].trusted == True

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
        candidate = pkg.candidateOrigin[0]
        return candidate != None and \
               candidate[0].origin == "Ubuntu" and \
               candidate[0].component in ["main", "restricted"] and \
               candidate[0].trusted == True

    def _get_pkg_version_by_id(self, id):
        """
        Return a package version matching the given package id or None.
        """
        name_raw, version, arch, data = id.split(";", 4)
        #FIXME: Python-apt doesn't allow unicode as key. See #542965
        name = str(name_raw)
        if self._cache.has_key(name):
            for pkg_ver in self._cache[name].versions:
                if pkg_ver.version == version and \
                   pkg_ver.architecture == arch:
                    return pkg_ver
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
            self.error(ERROR_PACKAGE_NOT_FOUND,
                       "There isn't any package named %s" % name)
        #FIXME:This requires a not yet released fix in python-apt
        try:
            version = pkg.versions[version_string]
        except:
            self.error(ERROR_PACKAGE_NOT_FOUND,
                       "There isn't any verion %s of %s" % (version_string,
                                                            name))
        if version.architecture != arch:
            self.error(ERROR_PACKAGE_NOT_FOUND,
                       "Version %s of %s isn't available for architecture "
                       "%s" % (pkg.name, version.version, arch))
        return version

    def _get_installed_files(self, pkg):
        """
        Return the list of unicode names of the files which have
        been installed by the package

        This method should be obsolete by the apt.package.Package.installedFiles
        attribute as soon as the consolidate branch of python-apt gets merged
        """
        path = os.path.join(apt_pkg.Config["Dir"],
                            "var/lib/dpkg/info/%s.list" % pkg.name)
        try:
            list = open(path)
            files = list.read().decode().split("\n")
            list.close()
        except:
            return []
        return files

    def _get_package_group(self, pkg):
        """
        Return the packagekit group corresponding to the package's section
        """
        section = pkg.section.split("/")[-1]
        if SECTION_GROUP_MAP.has_key(section):
            return SECTION_GROUP_MAP[section]
        else:
            pklog.debug("Unkown package section %s of %s" % (pkg.section,
                                                             pkg.name))
            return GROUP_UNKNOWN

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
        print
        pdb.pm()

def run(args, single=False):
    """
    Start the apt backend
    """
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
        config = apt_pkg.Config
        config.Set("Dir", options.root)
        config.Set("Dir::State::status",
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
