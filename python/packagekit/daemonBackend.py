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
import os
import sys
import time
import traceback
import types

import gobject
import dbus.service

from enums import *
from pkexceptions import *

# Setup Logging

logging.basicConfig()
pklog = logging.getLogger("PackageKitBackend")
pklog.setLevel(logging.DEBUG)

# Classes

# This is common between backends
PACKAGEKIT_DBUS_INTERFACE = 'org.freedesktop.PackageKitBackend'
PACKAGEKIT_DBUS_PATH = '/org/freedesktop/PackageKitBackend'

INACTIVE_CHECK_TIMEOUT = 1000 * 60 * 5 # Check every 5 minutes.
INACTIVE_TIMEOUT = 1000 * 60 * 30 # timeout after 30 minutes of inactivity.

class PackageKitBaseBackend(dbus.service.Object):

    def PKSignalHouseKeeper(func):
        def wrapper(*args,**kwargs):
            self = args[0]
            self.last_action_time = time.time()

            return func(*args,**kwargs)

        return wrapper

# I tried to do the same thing with methods as we do with signals, but
# dbus-python's method decorator uses inspect.getargspec...since the
# signature of the wrapper function is different.  So now each method
# has to call self.last_action_time = time.time()
#
#    def PKMethodHouseKeeper(func):
#        def wrapper(*args,**kwargs):
#            self = args[0]
#            self.last_action_time = time.time()
#
#            return func(*args, **kwargs)
#
#        return wrapper

    def __init__(self, bus_name, dbus_path):
        dbus.service.Object.__init__(self, bus_name, dbus_path)

        self._locked = False

        self.loop = gobject.MainLoop()

        gobject.timeout_add(INACTIVE_CHECK_TIMEOUT, self.check_for_inactivity)
        self.last_action_time = time.time()

        self.loop.run()

    def doLock(self):
        ''' Generic locking, overide and extend in child class'''
        self._locked = True

    def doUnlock(self):
        ''' Generic unlocking, overide and extend in child class'''
        self._locked = False

    def isLocked(self):
        return self._locked

    def check_for_inactivity(self):
        if time.time() - self.last_action_time > INACTIVE_TIMEOUT:
            pklog.error("Exiting due to timeout.")
            self.Exit()

        return True

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
        pklog.info("AllowCancel (%i)" % (allow_cancel))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='sss')
    def Package(self, status, package_id, summary):
        pklog.info("Package (%s, %s, %s)" % (status, package_id, summary))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ssssst')
    def Description(self, package_id, license, group, detail, url, size):
        pklog.info("Description (%s, %s, %s, %s, %s, %u)" % (package_id, license, group, detail, url, size))

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
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='')
    def NoPercentageUpdates(self):
        pklog.info("NoPercentageUpdates")

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='u')
    def PercentageChanged(self, percentage):
        pklog.debug("PercentageChanged (%i)" % (percentage))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='u')
    def SubPercentageChanged(self, percentage):
        pklog.debug("SubPercentageChanged (%i)" % (percentage))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ssssssss')
    def UpdateDetail(self, package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update):
        pklog.info("UpdateDetail (%s, %s, %s, %s, %s, %s, %s, %s)" % (package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ss')
    def ErrorCode(self, code, description):
        '''
        send 'error'
        @param err: Error Type (ERROR_NO_NETWORK,ERROR_NOT_SUPPORTED,ERROR_INTERNAL_ERROR)
        @param description: Error description
        '''
        pklog.info("ErrorCode (%s, %s)" % (code, description))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ss')
    def MetaData(self,typ,fname):
        '''
        send 'metadata' signal:
        @param type:   The type of metadata (repository,package,filelist,changelog,group,unknown)
        @param fname:  The filename being downloaded
        '''
        pklog.info("MetaData (%s, %s)" % (typ,fname))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ss')
    def RequireRestart(self,type,details):
        '''
        send 'require-restart' signal:
        @param type:   The level of restart required (system,application,session)
        @param details:  Optional details about the restart
        '''
        pklog.info("RestartRequired (%s, %s)" % (type,details))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ss')
    def Message(self,type,details):
        '''
        send 'message' signal:
        @param type:   The type of message (warning,notice,daemon)
        @param details:  Required details about the message
        '''
        pklog.info("Message (%s, %s)" % (type,details))

    @PKSignalHouseKeeper
    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='')
    def UpdatesChanged(self,typ,fname):
        '''
        send 'updates-changed' signal:
        '''
        pklog.info("UpdatesChanged ()")


#
# Methods ( client -> engine -> backend )
#
# Python inheritence with decorators makes implementing these in the
# base class and overriding them in child classes very ugly.  So
# they're commented out here.  Just implement the ones you need in
# your class, and don't forget the decorators.
#
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='', out_signature='')
#    def Init(self):
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='', out_signature='')
#    def Exit(self):
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='', out_signature='')
#    def Lock(self):
#        self.doLock()
#
#    def doLock(self):
#        ''' Lock Yum'''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='', out_signature='')
#    def Unlock(self):
#        self.doUnlock()
#
#    def doUnlock(self):
#        ''' Unlock Yum'''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='ss', out_signature='')
#    def SearchName(self, filters, search):
#        '''
#        Implement the {backend}-search-name functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='ss', out_signature='')
#    def SearchDetails(self,filters,key):
#        '''
#        Implement the {backend}-search-details functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='ss', out_signature='')
#    def SearchGroup(self,filters,key):
#        '''
#        Implement the {backend}-search-group functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='ss', out_signature='')
#    def SearchFile(self,filters,key):
#        '''
#        Implement the {backend}-search-file functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='sb', out_signature='')
#    def GetRequires(self,package,recursive):
#        '''
#        Print a list of requires for a given package
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='sb', out_signature='')
#    def GetDepends(self,package,recursive):
#        '''
#        Print a list of depends for a given package
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='', out_signature='')
#    def UpdateSystem(self):
#        '''
#        Implement the {backend}-update-system functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='b', out_signature='')
#    def RefreshCache(self, force):
#        '''
#        Implement the {backend}-refresh_cache functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='ss', out_signature='')
#    def Resolve(self, filters, name):
#        '''
#        Implement the {backend}-resolve functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='s', out_signature='')
#    def InstallPackage(self, package):
#        '''
#        Implement the {backend}-install functionality
#        This will only work with yum 3.2.4 or higher
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='s', out_signature='')
#    def InstallFile (self, inst_file):
#        '''
#        Implement the {backend}-install_file functionality
#        Install the package containing the inst_file file
#        Needed to be implemented in a sub class
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='s', out_signature='')
#    def ServicePack (self, location):
#        '''
#        Implement the {backend}-service-pack functionality
#        Install the package containing the inst_file file
#        Needed to be implemented in a sub class
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='s', out_signature='')
#    def UpdatePackage(self, package):
#        '''
#        Implement the {backend}-update functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='sb', out_signature='')
#    def RemovePackage(self, package, allowdep):
#        '''
#        Implement the {backend}-remove functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='s', out_signature='')
#    def GetDescription(self, package):
#        '''
#        Print a detailed description for a given package
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='s', out_signature='')
#    def GetFiles(self, package):
#        '''
#        Implement the get-files method
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='', out_signature='')
#    def GetUpdates(self):
#        '''
#        Implement the {backend}-get-updates functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='sb', out_signature='')
#    def RepoEnable(self, repoid, enable):
#        '''
#        Implement the {backend}-repo-enable functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='', out_signature='')
#    def GetRepoList(self):
#        '''
#        Implement the {backend}-get-repo-list functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='s', out_signature='')
#    def GetUpdateDetail(self,package):
#        '''
#        Implement the {backend}-get-update_detail functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()
#
#    @PKMethodHouseKeeper
#    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
#                         in_signature='sss', out_signature='')
#    def RepoSetData(self, repoid, parameter, value):
#        '''
#        Implement the {backend}-repo-set-data functionality
#        '''
#        self.ErrorCode(ERROR_NOT_SUPPORTED,"Method not supported")
#        self.Exit()

#
# Utility methods
#

    def _get_package_id(self,name,version,arch,data):
        return "%s;%s;%s;%s" % (name,version,arch,data)

    def get_package_from_id(self,id):
        ''' split up a package id name;ver;arch;data into a tuple
            containing (name,ver,arch,data)
        '''
        return tuple(id.split(';',4))

    def check_license_field(self,license_field):
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
            group = group.replace("(","")
            group = group.replace(")","")
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

class PackagekitProgress:
    '''
    Progress class there controls the total progress of a transaction
    the transaction is divided in n milestones. the class contains a subpercentage
    of the current step (milestone n -> n+1) and the percentage of the whole transaction

    Usage:

    from packagekit.backend import PackagekitProgress

    steps = [10,30,50,70] # Milestones in %
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

    def set_steps(self,steps):
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

    def set_subpercent(self,pct):
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

