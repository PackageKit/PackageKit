#!/usr/bin/python -tt
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Library General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
# Copyright (C) 2007 Tim Lauridsen <timlau@fedoraproject.org>

#
# This file contain the base classes to implement a PackageKit python backend
#

# imports
import sys

# Constants

ERROR_OOM = "out-of-memory"
ERROR_NO_NETWORK = "no-network"
ERROR_NOT_SUPPORTED = "not-supported"
ERROR_INTERNAL_ERROR = "internal-error"
ERROR_GPG_FAILURE = "gpg-failure"
ERROR_PACKAGE_NOT_INSTALLED = "package-not-installed"
ERROR_PACKAGE_ALREADY_INSTALLED = "package-already-installed"
ERROR_PACKAGE_DOWNLOAD_FAILED = "package-download-failed"
ERROR_DEP_RESOLUTION_FAILED = "dep-resolution-failed"
ERROR_CREATE_THREAD_FAILED = "create-thread-failed"
ERROR_FILTER_INVALID = "filter-invalid"
ERROR_TRANSACTION_ERROR = "transaction-error"

STATE_DOWNLOAD = "download"
STATE_INSTALL = "install"
STATE_UPDATE = "update"
STATE_REMOVE = "remove"

RESTART_SYSTEM = "system"
RESTART_APPLICATION = "application"
RESTART_SESSION = "session"

# Classes

class PackageKitBaseBackend:

    def __init__(self,cmds):
        self.cmds = cmds

    def percentage(self,percent=None):
        ''' 
        Write progress percentage
        @param percent: Progress percentage
        '''
        if percent != None:
            print >> sys.stderr, "percentage\t%i" % (percent)
        else:
            print >> sys.stderr, "no-percentage-updates"

    def sub_percentage(self,percent=None):
        ''' 
        send 'subpercentage' signal : subprogress percentage
        @param percent: subprogress percentage
        '''
        print >> sys.stderr, "subpercentage\t%i" % (percent)

    def error(self,err,description,exit=True):
        '''
        send 'error' 
        @param err: Error Type (ERROR_NO_NETWORK, ERROR_NOT_SUPPORTED, ERROR_INTERNAL_ERROR) 
        @param description: Error description
        @param exit: exit application with rc=1, if true 
        '''
        print >> sys.stderr,"error\t%s\t%s" % (err,description)
        if exit:
            sys.exit(1)

    def package(self,id,status,summary):
        '''
        send 'package' signal
        @param status: 1 = Installed, 0 = not
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param summary: The package Summary 
        '''
        print >> sys.stdout,"package\t%s\t%s\t%s" % (status,id,summary)

    def status(self,state):
        '''
        send 'status' signal
        @param state: STATE_DOWNLOAD, STATE_INSTALL, STATE_UPDATE, STATE_REMOVE 
        '''
        print >> sys.stderr,"status\t%s" % (state)

    def data(self,data):
        '''
        send 'data' signal:
        @param data:  The current worked on package
        '''
        print >> sys.stderr,"data\t%s" % (data)

    def description(self,id,licence,group,desc,url):
        '''
        Send 'description' signal
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param licence: The licence of the package
        @param group: The enumerated group
        @param desc: The multi line package description
        @param url: The upstream project homepage
        '''
        print >> sys.stdout,"description\t%s\t%s\t%s\t%s\t%s" % (id,licence,group,desc,url)

    def require_restart(self,restart_type,details):
        '''
        Send 'requirerestart' signal
        @param restart_type: RESTART_SYSTEM, RESTART_APPLICATION,RESTART_SESSION
        @param details: Optional details about the restart
        '''
        print >> sys.stderr,"requirerestart\t%s\t%s" % (restart_type,details)

    def allow_interrupt(self,allow):
        '''
        send 'allow-interrupt' signal:
        @param allow:  Allow the current process to be aborted.
        '''
        if allow:
            data = 'true'
        else:
            data = 'false'
        print >> sys.stderr,"allow-interrupt\t%s" % (data)

    def get_package_id(self,name,version,arch,data):
        return "%s;%s;%s;%s" % (name,version,arch,data)

    def get_package_from_id(self,id):
        ''' split up a package id name;ver;arch;data into a tuple
            containing (name,ver,arch,data)
        '''
        return tuple(id.split(';'))
#
# Backend Action Methods
#

    def search_name(self,filters,key):
        '''
        Implement the {backend}-search-name functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def search_details(self,filters,key):
        '''
        Implement the {backend}-search-details functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def search_group(self,filters,key):
        '''
        Implement the {backend}-search-group functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def search_file(self,filters,key):
        '''
        Implement the {backend}-search-file functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")


    def get_update_detail(self,package):
        '''
        Implement the {backend}-get-update-detail functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_depends(self,package):
        '''
        Implement the {backend}-get-depends functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_requires(self,package):
        '''
        Implement the {backend}-get-requires functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def update_system(self):
        '''
        Implement the {backend}-update-system functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")


    def refresh_cache(self):
        '''
        Implement the {backend}-refresh_cache functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def install(self, package):
        '''
        Implement the {backend}-install functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def remove(self, allowdep, package):
        '''
        Implement the {backend}-remove functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def update(self, package):
        '''
        Implement the {backend}-update functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_description(self, package):
        '''
        Implement the {backend}-get-description functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_updates(self, package):
        '''
        Implement the {backend}-get-updates functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

