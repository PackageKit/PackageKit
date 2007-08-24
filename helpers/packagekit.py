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

ERROR_NO_NETWORK = "no-network"
ERROR_NOT_SUPPORTED = "not-supported"
ERROR_INTERNAL_ERROR = "internal-error"

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
        if percent:
            print >> sys.stderr, "percentage\t%i" % (percent)
        else:
            print >> sys.stderr, "no-percentage-updates"

    def sub_percentage(self,percent=None):
        ''' 
        send 'subpercentage' signal : subprogress percentage
        @param percent: subprogress percentage
        '''
        print >> sys.stderr, "subpercentage\t%i" % (percent)

    def error(self,err,description):
        '''
        send 'error' 
        @param err: Error Type (ERROR_NO_NETWORK, ERROR_NOT_SUPPORTED, ERROR_INTERNAL_ERROR) 
        @param description: Error description
        '''
        print >> sys.stderr,"error\t%s\t%s" % (err,description)
        
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
        print >> sys.stderr,"status\t%s" % (status)
        
    def data(self,data):
        '''
        send 'data' signal:
        @param data:  The current worked on package
        '''
        print >> sys.stderr,"data\t%s" % (data)
        
    def description(self,id,version,desc,url):
        '''
        Send 'description' signal
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param version: The full distro package version
        @param desc: The multi line package description
        @param url: The upstream project homepage
        '''
        print >> sys.stdout,"description\t%s\t%s\t%s\t%s" % (id,version,desc,url)
        
    def require_restart(self,restart_type,details):
        '''
        Send 'requirerestart' signal
        @param restart_type: RESTART_SYSTEM, RESTART_APPLICATION,RESTART_SESSION
        @param details: Optional details about the restart
        '''
        print >> sys.stderr,"requirerestart\t%s\t%s" % (restart_type,details)
    
    def get_package_id(self,name,version,arch,data):
        return "%s;%s;%s;%s" % (name,version,arch,data)

#
# Backend Action Methods
#    
    
    def search_name(self,opt,key):
        '''
        Implement the {backend}-search-name functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def search_details(self,opt,key):
        '''
        Implement the {backend}-search-details functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")
        
    def get_deps(self,package):
        '''
        Implement the {backend}-get-deps functionality
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

    def get_description(self, package):
        '''
        Implement the {backend}-get-description functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")
        
        
