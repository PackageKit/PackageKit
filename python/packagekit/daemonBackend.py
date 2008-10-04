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

# Copyright (C) 2007 Tim Lauridsen <timlau@fedoraproject.org>
#                    Robin Norwood <rnorwood@redhat.com>

#
# This file contain the base classes to implement a PackageKit python backend
#

# imports
import logging
import logging.handlers
import os
import sys
import threading
import time
import traceback

import gobject
import dbus.service

from enums import *
from pkexceptions import *

# Setup Logging

logging.basicConfig(format="%(levelname)s:%(message)s")
pklog = logging.getLogger("PackageKitBackend")
pklog.setLevel(logging.WARN)

syslog = logging.handlers.SysLogHandler(facility=logging.handlers.SysLogHandler.LOG_DAEMON, address='/dev/log')
formatter = logging.Formatter('PackageKit: %(levelname)s: %(message)s')
syslog.setFormatter(formatter)
pklog.addHandler(syslog)

# Classes

# This is common between backends
PACKAGEKIT_DBUS_INTERFACE = 'org.freedesktop.PackageKitBackend'
PACKAGEKIT_DBUS_PATH = '/org/freedesktop/PackageKitBackend'

INACTIVE_CHECK_INTERVAL = 1000 * 60 * 5 # Check every 5 minutes.
INACTIVE_TIMEOUT = 60 * 10 # timeout after 10 minutes of inactivity.

def forked(func):
    '''
    Decorator to fork a worker process.

    Use a custom doCancel method in your backend to cancel forks:

    def doCancel(self):
        if self._child_pid:
            os.kill(self._child_pid, signal.SIGQUIT)
            self._child_pid = None
            self.Finished(EXIT_SUCCESS)
            return
        self.Finished(EXIT_FAILED)
    '''
    def wrapper(*args, **kwargs):
        self = args[0]
        self.AllowCancel(True)
        # Make sure that we are not in the worker process
        if self._child_pid == 0:
            pklog.debug("forkme() called from child thread.")
            raise Exception("forkme() called from child thread.")
        # Make sure that there is no another child process running
        retries = 0
        while self._is_child_running() and retries < 5:
            pklog.warning("Method called, but a child is already running")
            time.sleep(0.1)
            retries += 1
        if self._is_child_running():
            self.ErrorCode(ERROR_INTERNAL_ERROR,
                           "Method called while child process is still "
                           "running.")
            raise Exception("Method called while child process is "
                            "still running")
        # Fork a worker process
        self.last_action_time = time.time()
        self._child_pid = os.fork()
        if self._child_pid > 0:
            gobject.child_watch_add(self._child_pid, self.on_child_exit)
            return
        self.loop.quit()
        sys.exit(func(*args, **kwargs))
    return wrapper

class PackageKitThread(threading.Thread):
    '''
    Threading class which can handle crashes. Inspired by
    http://spyced.blogspot.com/2007/06/workaround-for-sysexcepthook-bug.html
    '''
    def run(self):
        try:
            threading.Thread.run(self)
        except (KeyboardInterrupt, SystemExit):
            raise
        except:
            sys.excepthook(*sys.exc_info())

def threaded(func):
    '''
    Decorator to run a PackageKitBaseBackend method in a separate thread
    '''
    def wrapper(*args, **kwargs):
        backend = args[0]
        backend.last_action_time = time.time()
        thread = PackageKitThread(target=func, args=args, kwargs=kwargs)
        thread.start()
    wrapper.__name__ = func.__name__
    return wrapper

def serialize(func):
    '''
    Decorator which makes sure no other threads are running before executing function.
    '''
    def wrapper(*args, **kwargs):
        backend = args[0]
        backend._lock.acquire()
        func(*args, **kwargs)
        backend._lock.release()
    wrapper.__name__ = func.__name__
    return wrapper

class PackageKitBaseBackend(dbus.service.Object):

    def PKSignalHouseKeeper(func):
        def wrapper(*args, **kwargs):
            self = args[0]
            self.last_action_time = time.time()

            return func(*args, **kwargs)

        return wrapper

# I tried to do the same thing with methods as we do with signals, but
# dbus-python's method decorator uses inspect.getargspec...since the
# signature of the wrapper function is different.  So now each method
# has to call self.last_action_time = time.time()
#
#    def PKMethodHouseKeeper(func):
#        def wrapper(*args, **kwargs):
#            self = args[0]
#            self.last_action_time = time.time()
#
#            return func(*args, **kwargs)
#
#        return wrapper

    def __init__(self, bus_name, dbus_path):
        dbus.service.Object.__init__(self, bus_name, dbus_path)
        sys.excepthook = self._excepthook

        self._allow_cancel = False
        self._child_pid = None
        self._is_child = False

        self.loop = gobject.MainLoop()

        gobject.timeout_add(INACTIVE_CHECK_INTERVAL, self.check_for_inactivity)
        self.last_action_time = time.time()

        self.loop.run()

    def check_for_inactivity(self):
        if time.time() - self.last_action_time > INACTIVE_TIMEOUT:
            pklog.critical("Exiting due to timeout.")
            self.Exit()
        return True

    def on_child_exit(self, pid, condition, data):
        pass

    def _is_child_running(self):
        pklog.debug("in child_is_running")
        if self._child_pid:
            pklog.debug("in child_is_running, pid = %s" % self._child_pid)
            running = True
            try:
                (pid, status) = os.waitpid(self._child_pid, os.WNOHANG)
                if pid:
                    running = False
            except OSError, e:
                pklog.error("OS Error: %s" % str(e))
                running = False

            if not running:
                pklog.debug("child %s is stopped" % self._child_pid)
                self._child_pid = None
                return False

            pklog.debug("child still running")
            return True

        pklog.debug("No child.")
        return False

#
# Signals ( backend -> engine -> client )
#

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='s')
    def Finished(self, exit):
        pklog.info("Finished (%s)" % (exit))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ssb')
    def RepoDetail(self, repo_id, description, enabled):
        pklog.info("RepoDetail (%s, %s, %i)" % (repo_id, description, enabled))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='b')
    def AllowCancel(self, allow_cancel):
        self._allow_cancel = allow_cancel
        pklog.info("AllowCancel (%i)" % allow_cancel)

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='sss')
    def Package(self, status, package_id, summary):
        pklog.info("Package (%s, %s, %s)" % (status, package_id, summary))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ssssst')
    def Details(self, package_id, license, group, detail, url, size):
        pklog.info("Details (%s, %s, %s, %s, %s, %u)" % (package_id, license, group, detail, url, size))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ss')
    def Files(self, package_id, file_list):
        pklog.info("Files (%s, %s)" % (package_id, file_list))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='s')
    def StatusChanged(self, status):
        pklog.info("StatusChanged (%s)" % (status))

    @PKSignalHouseKeeper
    def NoPercentageUpdates(self):
        pklog.info("NoPercentageUpdates ()")
        self.PercentageChanged(101)

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='u')
    def PercentageChanged(self, percentage):
        pklog.info("PercentageChanged (%i)" % (percentage))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='u')
    def SubPercentageChanged(self, percentage):
        pklog.info("SubPercentageChanged (%i)" % (percentage))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ssssssssssss')
    def UpdateDetail(self, package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update, changelog, state, issued, updated):
        pklog.info("UpdateDetail (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)" % (package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update, changelog, state, issued, updated))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ss')
    def ErrorCode(self, code, description):
        '''
        send 'error'
        @param err: Error Type (ERROR_NO_NETWORK, ERROR_NOT_SUPPORTED, ERROR_INTERNAL_ERROR)
        @param description: Error description
        '''
        pklog.info("ErrorCode (%s, %s)" % (code, description))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ss')
    def RequireRestart(self, type, details):
        '''
        send 'require-restart' signal:
        @param type:   The level of restart required (system, application, session)
        @param details:  Optional details about the restart
        '''
        pklog.info("RestartRequired (%s, %s)" % (type, details))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ss')
    def Message(self, type, details):
        '''
        send 'message' signal:
        @param type:   The type of message (warning, notice, daemon)
        @param details:  Required details about the message
        '''
        pklog.info("Message (%s, %s)" % (type, details))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ssss')
    def EulaRequired(self, eula_id, package_id, vendor_name, license_agreement):
        '''
        send 'eula-required' signal:
        @param eula_id: unique identifier of the EULA agreement
        @param package_id: the package affected by this agreement
        @param vendor_name: the freedom hater
        @param license_agreement: the plain text license agreement
        '''
        pklog.info("Eula required (%s, %s, %s, %s)" % (eula_id, package_id,
                                                    vendor_name,
                                                    license_agreement))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='')
    def UpdatesChanged(self, typ, fname):
        '''
        send 'updates-changed' signal:
        '''
        pklog.info("UpdatesChanged ()")

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ssssssss')
    def RepoSignatureRequired(self, id, repo_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, key_type):
        '''
        send 'repo-signature-required' signal:
        '''
        pklog.info("RepoSignatureRequired (%s, %s, %s, %s, %s, %s, %s, %s)" %
                   (id, repo_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, key_type))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='sss')
    def DistroUpgrade(self, type, name, summary):
        '''
        send 'distro-upgrade' signal:
        @param type:   The type of distro upgrade (e.g. stable or unstable)
        @param name: Short name of the distribution e.g. Dapper Drake 6.06 LTS
        @param summary: Multi-line description of the release
        '''
        pklog.info("DistroUpgrade (%s, %s, %s)" % (type, name, summary))

#
# Methods ( client -> engine -> backend )
#
# Python inheritence with decorators makes implementing these in the
# base class and overriding them in child classes very ugly.  So
# they're commented out here.  Just implement the ones you need in
# your class, and don't forget the decorators.
#
    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Init(self):
        pklog.info("Init()")
        self.doInit()

    def doInit(self):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    # We have to idle add this from self.Exit() so that DBUS gets a chance to reply
    def _doExitDelay(self):
        pklog.info("ExitDelay()")
        self.loop.quit()
        sys.exit(1)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Exit(self):
        pklog.info("Exit()")
        self.doExit()

    def doExit(self):
        '''
        Should be replaced in the corresponding backend sub class
        
        Call this method at the end to make sure that dbus can still respond
        gobject.idle_add (self._doExitDelay)
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ss', out_signature='')
    def SearchName(self, filters, search):
        '''
        Implement the {backend}-search-name functionality
        '''
        pklog.info("SearchName()")
        self.doSearchName(filters, search)

    def doSearchName(self, filters, search):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='s', out_signature='')
    def GetPackages(self, filters):
        '''
        Implement the {backend}-get-packages functionality
        '''
        pklog.info("GetPackages()")
        self.doGetPackages(filters)

    def doGetPackages(self, filters):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Cancel(self):
        pklog.info("Cancel()")
        if not self._allow_cancel:
            self.ErrorCode(ERROR_CANNOT_CANCEL, "Current action cannot be cancelled")
            self.Finished(EXIT_FAILED)
            return
        self.doCancel()

    def doCancel(self):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ss', out_signature='')
    def DownloadPackages(self, package_ids, directory):
        '''
        Implement the (backend)-download-packages functionality
        '''
        pklog.info("DownloadPackages(%s, %s)" % (package_ids, directory))
        self.doDownloadPackages(package_ids, directory)

    def doDownloadPackages(self, package_ids, directory):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ss', out_signature='')
    def SearchDetails(self, filters, key):
        '''
        Implement the {backend}-search-details functionality
        '''
        pklog.info("SearchDetails(%s, %s)" % (filters, key))
        self.doSearchDetails(filters, key)

    def doSearchDetails(self, filters, key):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ss', out_signature='')
    def SearchGroup(self, filters, key):
        '''
        Implement the {backend}-search-group functionality
        '''
        pklog.info("SearchGroup(%s, %s)" % (filters, key))
        self.doSearchGroup(filters, key)

    def doSearchGroup(self, filters, key):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ss', out_signature='')
    def SearchFile(self, filters, key):
        '''
        Implement the {backend}-search-file functionality
        '''
        pklog.info("SearchFile(%s, %s)" % (filters, key))
        self.doSearchFile(filters, key)

    def doSearchFile(self, filters, key):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ssb', out_signature='')
    def GetRequires(self, filters, package_ids, recursive):
        '''
        Print a list of requires for a given package
        '''
        pklog.info("GetRequires(%s, %s, %s)" % (filters, package_ids, recursive))
        self.doGetRequires(filters, package_ids , recursive)

    def doGetRequires(self, filters, package_ids, recursive):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='sss', out_signature='')
    def WhatProvides(self, filters, provides_type, search):
        '''
        Print a list of packages for a given provide string
        '''
        pklog.info("WhatProvides(%s, %s, %s)" % (filters, provides_type, search))
        self.doWhatProvides(filters, provides_type, search)

    def doWhatProvides(self, filters, provides_type, search):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ssb', out_signature='')
    def GetDepends(self, filters, package_ids, recursive):
        '''
        Print a list of depends for a given package
        '''
        pklog.info("GetDepends(%s, %s, %s)" % (filters, package_ids, recursive))
        self.doGetDepends(filters, package_ids, recursive)

    def doGetDepends(self, filters, package_ids, recursive):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def UpdateSystem(self):
        '''
        Implement the {backend}-update-system functionality
        '''
        pklog.info("UpdateSystem()")
        self.doUpdateSystem()

    def doUpdateSystem(self):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='b', out_signature='')
    def RefreshCache(self, force):
        '''
        Implement the {backend}-refresh_cache functionality
        '''
        pklog.info("RefreshCache(%s)" % force)
        self.doRefreshCache( force)

    def doRefreshCache(self):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='sas', out_signature='')
    def Resolve(self, filters, names):
        '''
        Implement the {backend}-resolve functionality
        '''
        pklog.info("Resolve(%s, %s)" % (filters, names))
        self.doResolve(filters, names)

    def doResolve(self, filters, names):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='as', out_signature='')
    def InstallPackages(self, package_ids):
        '''
        Implement the {backend}-install functionality
        '''
        pklog.info("InstallPackages(%s)" % package_ids)
        self.doInstallPackages(package_ids)

    def doInstallPackages(self, package_ids):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='bas', out_signature='')
    def InstallFiles (self, trusted, full_paths):
        '''
        Implement the {backend}-install_files functionality
        Install the package containing the full_paths file
        '''
        pklog.info("InstallFiles(%i, %s)" % (trusted, full_paths))
        self.doInstallFiles(trusted, full_paths)

    def doInstallFiles(self, full_paths):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='sb', out_signature='')
    def ServicePack (self, location, enabled):
        '''
        Implement the {backend}-service-pack functionality
        '''
        pklog.info("ServicePack(%s, %s)" % (location, enabled))
        self.doServicePack(location, enabled)

    def doServicePack(self, location, enabled):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='as', out_signature='')
    def UpdatePackages(self, package_ids):
        '''
        Implement the {backend}-update-packages functionality
        '''
        pklog.info("UpdatePackages(%s)" % package_ids)
        self.doUpdatePackages(package_ids)

    def doUpdatePackages(self, package_ids):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='asbb', out_signature='')
    def RemovePackages(self, package_ids, allowdep, autoremove):
        '''
        Implement the {backend}-remove functionality
        '''
        pklog.info("RemovePackages(%s, %s, %s)" % (package_ids, allowdep,
                                                 autoremove))
        self.doRemovePackages(package_ids, allowdep, autoremove)

    def doRemovePackages(self, package_ids, allowdep, autoremove):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='s', out_signature='')
    def GetDetails(self, package_ids):
        '''
        Print a detailed details for a given package
        '''
        pklog.info("GetDetails(%s)" % package_ids)
        self.doGetDetails(package_ids)

    def doGetDetails(self, package_ids):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='s', out_signature='')
    def GetFiles(self, package_ids):
        '''
        Implement the get-files method
        '''
        pklog.info("GetFiles(%s)" % package_ids)
        self.doGetFiles(package_ids)

    def doGetFiles(self, package_ids):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def GetDistroUpgrades(self):
        '''
        Implement the {backend}-get-distro-upgrades functionality
        '''
        pklog.info("GetDistroUpgrades()")
        self.doGetDistroUpgrades()

    def doGetDistroUpgrades(self):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='s', out_signature='')
    def GetUpdates(self, filters):
        '''
        Implement the {backend}-get-updates functionality
        '''
        pklog.info("GetUpdates(%s)" % filters)
        self.doGetUpdates(filters)

    def doGetUpdates(self, filters):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='sb', out_signature='')
    def RepoEnable(self, repoid, enable):
        '''
        Implement the {backend}-repo-enable functionality
        '''
        pklog.info("RepoEnable(%s, %s)" % (repoid, enable))
        self.doRepoEnable( repoid, enable)

    def doRepoEnable(self, repoid, enable):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def GetRepoList(self, filters):
        '''
        Implement the {backend}-get-repo-list functionality
        '''
        pklog.info("GetRepoList()")
        self.doGetRepoList(filters)

    def doGetRepoList(self, filters):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='s', out_signature='')
    def GetUpdateDetail(self, package_ids):
        '''
        Implement the {backend}-get-update_detail functionality
        '''
        pklog.info("GetUpdateDetail(%s)" % package_ids)
        self.doGetUpdateDetail(package_ids)

    def doGetUpdateDetail(self, package_ids):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='sss', out_signature='')
    def RepoSetData(self, repoid, parameter, value):
        '''
        Implement the {backend}-repo-set-data functionality
        '''
        pklog.info("RepoSetData(%s, %s, %s)" % (repoid, parameter, value))
        self.doRepoSetData(repoid, parameter, value)

    def doRepoSetData(self, repoid, parameter, value):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ss', out_signature='')
    def SetProxy(self, proxy_http, proxy_ftp):
        '''
        Set the proxy
        '''
        pklog.info("SetProxy(%s, %s)" % (proxy_http, proxy_ftp))
        self.doSetProxy(proxy_http, proxy_ftp)

    def doSetProxy(self, proxy_http, proxy_ftp):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        # do not use Finished() in this method

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='s', out_signature='')
    def InstallPublicKey(self, keyurl):
        '''
        Implement the {backend}-install-public-key functionality
        '''
        pklog.info("InstallPublicKey(%s)" % keyurl)
        self.doInstallPublicKey(keyurl)

    def doInstallPublicKey(self, keyurl):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='s', out_signature='')
    def SetLocale(self, code):
        '''
        Allow to set the locale of the backend.
        '''
        pklog.info("SetLocale(): %s" % code)
        self.doSetLocale(code)

    def doSetLocale(self, code):
        '''
        Should be replaced in the corresponding backend sub class
        '''
        self.ErrorCode(ERROR_NOT_SUPPORTED,
                       "This function is not implemented in this backend")
        self.Finished(EXIT_FAILED)

#
# Utility methods
#

    def _get_package_id(self, name, version, arch, data):
        return "%s;%s;%s;%s" % (name, version, arch, data)

    def get_package_from_id(self, id):
        ''' split up a package id name;ver;arch;data into a tuple
            containing (name, ver, arch, data)
        '''
        return tuple(id.split(';', 4))

    def check_license_field(self, license_field):
        '''
        Check the string license_field for free licenses, indicated by
        their short names as documented at
        http://fedoraproject.org/wiki/Licensing

        Licenses can be grouped by " or " to indicate that the package
        can be redistributed under any of the licenses in the group.
        For instance: GPLv2+ or Artistic or FooLicense.

        Also, if a license ends with "+", the "+" is removed before
        comparing it to the list of valid licenses.  So if license
        "FooLicense" is free, then "FooLicense+" is considered free.

        Groups of licenses can be grouped with " and " to indicate
        that parts of the package are distributed under one group of
        licenses, while other parts of the package are distributed
        under another group.  Groups may be wrapped in parenthesis.
        For instance:
          (GPLv2+ or Artistic) and (GPL+ or Artistic) and FooLicense.

        At least one license in each group must be free for the
        package to be considered Free Software.  If the license_field
        is empty, the package is considered non-free.
        '''

        groups = license_field.split(" and ")

        if len(groups) == 0:
            return False

        one_free_group = False

        for group in groups:
            group = group.replace("(", "")
            group = group.replace(")", "")
            licenses = group.split(" or ")

            group_is_free = False

            for license in licenses:
                license = license.strip()

                if len(license) < 1:
                    continue

                if license[-1] == "+":
                    license = license[0:-1]

                if license in PackageKitEnum.free_licenses:
                    one_free_group = True
                    group_is_free = True
                    break

            if group_is_free == False:
                return False

        if one_free_group == False:
            return False

        return True

    def _customTracebackHandler(self, exctype):
        '''

        '''
        return False

    def _excepthook(self, exctype, excvalue, exctb):
        '''
        Handle a crash: try to submit the message to packagekitd and the logger.
        afterwards shutdown the daemon.
        '''
        if (issubclass(exctype, KeyboardInterrupt) or
            issubclass(exctype, SystemExit)):
            return
        if self._customTracebackHandler(exctype):
            return

        tbtext = ''.join(traceback.format_exception(exctype, excvalue, exctb))
        try:
            self.ErrorCode(ERROR_INTERNAL_ERROR, tbtext)
            self.Finished(EXIT_FAILED)
        except:
            pass
        try:
            pklog.critical(tbtext)
        except:
            pass
        self.loop.quit()
        sys.exit(1)

class PackagekitProgress:
    '''
    Progress class there controls the total progress of a transaction
    the transaction is divided in n milestones. the class contains a subpercentage
    of the current step (milestone n -> n+1) and the percentage of the whole transaction

    Usage:

    from packagekit.backend import PackagekitProgress

    steps = [10, 30, 50, 70] # Milestones in %
    progress = PackagekitProgress()
    progress.set_steps(steps)
    for milestone in range(len(steps)):
        # do the action is this step
        for i in range(100):
            # do some action
            progress.set_subpercent(i+1)
            print "progress : %s " % progress.percent
        progress.step() # step to next milestone

    '''

    #TODO: Add support for elapsed/remaining time

    def __init__(self):
        self.percent = 0
        self.steps = []
        self.current_step = 0
        self.subpercent = 0

    def set_steps(self, steps):
        '''
        Set the steps for the whole transaction
        @param steps: list of int representing the percentage of each step in the transaction
        '''
        self.reset()
        self.steps = steps
        self.current_step = 0

    def reset(self):
        self.percent = 0
        self.steps = []
        self.current_step = 0
        self.subpercent = 0

    def step(self):
        '''
        Step to the next step in the transaction
        '''
        if self.current_step < len(self.steps)-1:
            self.current_step += 1
            self.percent = self.steps[self.current_step]
            self.subpercent = 0
        else:
            self.percent = 100
            self.subpercent = 0

    def set_subpercent(self, pct):
        '''
        Set subpercentage and update percentage
        '''
        self.subpercent = pct
        self._update_percent()

    def _update_percent(self):
        '''
        Increment percentage based on current step and subpercentage
        '''
        if self.current_step == 0:
            startpct = 0
        else:
            startpct = self.steps[self.current_step-1]
        if self.current_step < len(self.steps)-1:
            endpct = self.steps[self.current_step+1]
        else:
            endpct = 100
        deltapct = endpct -startpct
        f = float(self.subpercent)/100.0
        incr = int(f*deltapct)
        self.percent = startpct + incr
