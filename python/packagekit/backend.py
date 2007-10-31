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

#
# This file contain the base classes to implement a PackageKit python backend
#

# imports
import sys
import types


# Constants

ERROR_OOM = "out-of-memory"
ERROR_NO_NETWORK = "no-network"
ERROR_NOT_SUPPORTED = "not-supported"
ERROR_INTERNAL_ERROR = "internal-error"
ERROR_GPG_FAILURE = "gpg-failure"
ERROR_SIGNATURE_NOT_IMPORTED = "signature-not-imported"
ERROR_PACKAGE_NOT_INSTALLED = "package-not-installed"
ERROR_PACKAGE_ALREADY_INSTALLED = "package-already-installed"
ERROR_PACKAGE_DOWNLOAD_FAILED = "package-download-failed"
ERROR_DEP_RESOLUTION_FAILED = "dep-resolution-failed"
ERROR_CREATE_THREAD_FAILED = "create-thread-failed"
ERROR_FILTER_INVALID = "filter-invalid"
ERROR_TRANSACTION_ERROR = "transaction-error"
ERROR_TRANSACTION_ERROR = "transaction-error"
ERROR_NO_CACHE = "no-cache"
ERROR_REPO_NOT_FOUND = "repo-not-found"
ERROR_CANNOT_REMOVE_SYSTEM_PACKAGE = "cannot-remove-system-package"
ERROR_PROCESS_QUIT="process-quit"
ERROR_PROCESS_KILL="process-kill"

STATE_DOWNLOAD = "download"
STATE_INSTALL = "install"
STATE_UPDATE = "update"
STATE_REMOVE = "remove"
STATE_WAIT = "wait"
STATE_CLEANUP = "cleanup"
STATE_OBSOLETE = "obsolete"

RESTART_SYSTEM = "system"
RESTART_APPLICATION = "application"
RESTART_SESSION = "session"

INFO_INSTALLED = "installed"
INFO_AVAILABLE = "available"
INFO_LOW = "low"
INFO_NORMAL = "normal"
INFO_IMPORTANT = "important"
INFO_SECURITY = "security"
INFO_DOWNLOADING = "downloading"
INFO_UPDATING = "updating"
INFO_INSTALLING = "installing"
INFO_REMOVING = "removing"
INFO_CLEANUP = "cleanup"
INFO_OBSOLETE = "obsoleting"

FILTER_INSTALLED = "installed"
FILTER_NON_INSTALLED = "~installed"
FILTER_GUI = "gui"
FILTER_NON_GUI = "~gui"
FILTER_DEVEL = "devel"
FILTER_NON_DEVEL = "~devel"

GROUP_ACCESSIBILITY     = "accessibility"
GROUP_ACCESSORIES       = "accessories"
GROUP_EDUCATION         = "education"
GROUP_GAMES             = "games"
GROUP_GRAPHICS          = "graphics"
GROUP_INTERNET          = "internet"
GROUP_OFFICE            = "office"
GROUP_OTHER             = "other"
GROUP_PROGRAMMING       = "programming"
GROUP_MULTIMEDIA        = "multimedia"
GROUP_SYSTEM            = "system"
GROUP_DESKTOPS          = "desktops"
GROUP_PUBLISHING        = "publishing"
GROUP_SERVERS           = "servers"
GROUP_FONTS             = "fonts"
GROUP_ADMIN_TOOLS       = "admin-tools"
GROUP_LEGACY            = "legacy"
GROUP_LOCALIZATION      = "localization"
GROUP_UNKNOWN           = "unknown"

# Classes

class PackageKitBaseBackend:

    def __init__(self,cmds):
        self.cmds = cmds
        self._locked = False

    def doLock(self):
        ''' Generic locking, overide and extend in child class'''
        self._locked = True

    def unLock(self):
        ''' Generic unlocking, overide and extend in child class'''
        self._locked = False

    def isLocked(self):
        return self._locked

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
            if self.isLocked():
                self.unLock()
            sys.exit(1)

    def package(self,id,status,summary):
        '''
        send 'package' signal
        @param info: the enumerated INFO_* string
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param summary: The package Summary
        '''
        summary = self._toUTF(summary)
        print >> sys.stdout,"package\t%s\t%s\t%s" % (status,id,summary)

    def status(self,state):
        '''
        send 'status' signal
        @param state: STATE_DOWNLOAD, STATE_INSTALL, STATE_UPDATE, STATE_REMOVE, STATE_WAIT
        '''
        print >> sys.stderr,"status\t%s" % (state)

    def repo_detail(self,repoid,name,state):
        '''
        send 'repo-detail' signal
        @param repoid: The repo id tag
        @param state: false is repo is disabled else true.
        '''
        print >> sys.stdout,"repo-detail\t%s\t%s\t%s" % (repoid,name,state)

    def data(self,data):
        '''
        send 'data' signal:
        @param data:  The current worked on package
        '''
        print >> sys.stderr,"data\t%s" % (data)

    def description(self,id,licence,group,desc,url,bytes,file_list):
        '''
        Send 'description' signal
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param licence: The licence of the package
        @param group: The enumerated group
        @param desc: The multi line package description
        @param url: The upstream project homepage
        @param bytes: The size of the package, in bytes
        @param file_list: List of the files in the package, separated by ';'
        '''
        desc = self._toUTF(desc)
        print >> sys.stdout,"description\t%s\t%s\t%s\t%s\t%s\t%ld\t%s" % (id,licence,group,desc,url,bytes,file_list)

    def files(self, id, file_list):
        '''
        Send 'files' signal
        @param file_list: List of the files in the package, separated by ';'
        '''
        print >> sys.stdout,"files\t%s\t%s" % (id, file_list)

    def update_detail(self,id,updates,obsoletes,url,restart,update_text):
        '''
        Send 'updatedetail' signal
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param updates:
        @param obsoletes:
        @param url:
        @param restart:
        @param update_text:
        '''
        print >> sys.stdout,"updatedetail\t%s\t%s\t%s\t%s\t%s\t%s" % (id,updates,obsoletes,url,restart,update_text)

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

    def repo_signature_required(self,repo_name,key_url,key_userid,key_id,key_fingerprint,key_timestamp,type):
        '''
        send 'repo-signature-required' signal:
        @param repo_name:       Name of the repository
        @param key_url:         URL which the user can use to verify the key
        @param key_userid:      Key userid
        @param key_id:          Key ID
        @param key_fingerprint: Full key fingerprint
        @param key_timestamp:   Key timestamp
        @param type:            Key type (GPG)
        '''
        print >> sys.stderr,"repo-signature-required\t%s\t%s\t%s\t%s\t%s\t%s\t%s" % (
            repo_name,key_url,key_userid,key_id,key_fingerprint,key_timestamp,type
            )

    def get_package_id(self,name,version,arch,data):
        return "%s;%s;%s;%s" % (name,version,arch,data)

    def get_package_from_id(self,id):
        ''' split up a package id name;ver;arch;data into a tuple
            containing (name,ver,arch,data)
        '''
        return tuple(id.split(';', 4))
        

    def _toUTF( self, txt ):
        rc=""
        if isinstance(txt,types.UnicodeType):
            return txt
        else:
            try:
                rc = unicode( txt, 'utf-8' )
            except UnicodeDecodeError, e:
                rc = unicode( txt, 'iso-8859-1' )
            return rc
        
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

    def install_file (self, inst_file):
        '''
        Implement the {backend}-install_file functionality
        Install the package containing the inst_file file
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def resolve(self, name):
        '''
        Implement the {backend}-resolve functionality
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

    def get_files(self, package):
        '''
        Implement the {backend}-get-files functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend")

    def get_updates(self, package):
        '''
        Implement the {backend}-get-updates functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def repo_enable(self, repoid, enable):
        '''
        Implement the {backend}-repo-enable functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def repo_set_data(self, repoid, parameter, value):
        '''
        Implement the {backend}-repo-set-data functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")


    def get_repo_list(self):
        '''
        Implement the {backend}-get-repo-list functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_update_detail(self,package):
        '''
        Implement the {backend}-get-update_detail functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

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

        
