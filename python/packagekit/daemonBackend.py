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
import sys
import traceback
import types
from enums import *
import gobject
import os
from pkdbus import PackageKitDbusInterface

# Classes

class PackageKitBaseBackend(PackageKitDbusInterface):

    def __init__(self,cmds):
        self.daemonize()
        PackageKitDbusInterface.__init__(self, 'org.freedesktop.PackageKit')
        self.cmds = cmds
        self._locked = False

        self.loop = gobject.MainLoop()
        self.loop.run()
       
    def daemonize(self):
        """
        forking code stolen from yum-updatesd
        """
        pid = os.fork()
        if pid:
            sys.exit()
        os.chdir("/")
        fd = os.open("/dev/null", os.O_RDWR)
        os.dup2(fd, 0)
        os.dup2(fd, 1)
        os.dup2(fd, 2)
        os.close(fd)
     
    def doLock(self):
        ''' Generic locking, overide and extend in child class'''
        self._locked = True

    def unLock(self):
        ''' Generic unlocking, overide and extend in child class'''
        self._locked = False
        self.tid = None

    def isLocked(self):
        return self._locked

    def catchall_signal_handler(self,*args,**kwargs):
        self.tid = args[0]

        if kwargs['member'] == "quit":
            self.loop.quit()
            self.quit()
        if kwargs['member'] == "search_name":
            self.search_name(args[1],args[2])
        if kwargs['member'] == "search_details":
            self.search_details(args[1],args[2])
        if kwargs['member'] == "search_group":
            self.search_group(args[1],args[2])
        if kwargs['member'] == "search_file":
            self.search_file(args[1],args[2])
        if kwargs['member'] == "get_update_detail":
            self.get_update_detail(args[1])
        if kwargs['member'] == "get_depends":
            self.get_depends(args[1],args[2])
        if kwargs['member'] == "get_requires":
            self.get_requires(args[1],args[2])
        if kwargs['member'] == "update_system":
            self.update_system()
        if kwargs['member'] == "refresh_cache":
            self.refresh_cache()
        if kwargs['member'] == "install":
            self.install(args[1])
        if kwargs['member'] == "install_file":
            self.install_file(args[1])
        if kwargs['member'] == "resolve":
            self.resolve(args[1],args[2])
        if kwargs['member'] == "remove":
            self.remove(args[1],args[2])
        if kwargs['member'] == "update":
            self.update(args[1])
        if kwargs['member'] == "get_description":
            self.get_description(args[1])
        if kwargs['member'] == "get_files":
            self.get_files(args[1])
        if kwargs['member'] == "get_updates":
            self.get_updates()
        if kwargs['member'] == "repo_enable":
            self.repo_enable(args[1],args[2])
        if kwargs['member'] == "repo_set_data":
            self.repo_set_data(args[1],args[2],args[3])
        if kwargs['member'] == "get_repo_list":
            self.get_repo_list()
        else:
            print "Caught unhandled signal %s"% kwargs['member']
            print "  args:"
            for arg in args:
                print "		" + str(arg)

#
# Signals ( backend -> engine -> client )
#

    def percentage(self,percent=None):
        '''
        Write progress percentage
        @param percent: Progress percentage
        '''
        if percent != None:
            self.pk_iface.percentage(self.tid,percent)
        else:
            self.pk_iface.no_percentage_updates(self.tid)

    def sub_percentage(self,percent=None):
        '''
        send 'subpercentage' signal : subprogress percentage
        @param percent: subprogress percentage
        '''
        self.pk_iface.subpercentage(self.tid,percent)

    def error(self,err,description,exit=True):
        '''
        send 'error'
        @param err: Error Type (ERROR_NO_NETWORK,ERROR_NOT_SUPPORTED,ERROR_INTERNAL_ERROR)
        @param description: Error description
        @param exit: exit application with rc=1, if true
        '''
        self.pk_iface.error(self.tid,err,description)
        if exit:
            self.quit()

    def package(self,id,status,summary):
        '''
        send 'package' signal
        @param info: the enumerated INFO_* string
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param summary: The package Summary
        '''
        self.pk_iface.package(self.tid,status,id,summary)

    def status(self,state):
        '''
        send 'status' signal
        @param state: STATUS_DOWNLOAD,STATUS_INSTALL,STATUS_UPDATE,STATUS_REMOVE,STATUS_WAIT
        '''
        self.pk_iface.status(self.tid,state)

    def repo_detail(self,repoid,name,state):
        '''
        send 'repo-detail' signal
        @param repoid: The repo id tag
        @param state: false is repo is disabled else true.
        '''
        self.pk_iface.repo_detail(self.tid,repoid,name,state)

    def data(self,data_out):
        '''
        send 'data' signal:
        @param data_out:  The current worked on package
        '''
        self.pk_iface.data(self.tid,data_out)

    def metadata(self,typ,fname):
        '''
        send 'metadata' signal:
        @param type:   The type of metadata (repository,package,filelist,changelog,group,unknown)
        @param fname:  The filename being downloaded
        '''
        self.pk_iface.metadata(self.tid,typ,fname)

    def description(self,id,license,group,desc,url,bytes):
        '''
        Send 'description' signal
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param license: The license of the package
        @param group: The enumerated group
        @param desc: The multi line package description
        @param url: The upstream project homepage
        @param bytes: The size of the package, in bytes
        '''
        
        self.pk_iface.description(self.tid,id,license,group,desc,url,bytes)

    def files(self,id,file_list):
        '''
        Send 'files' signal
        @param file_list: List of the files in the package, separated by ';'
        '''
        self.pk_iface.files(self.tid,id,file_list)

    def update_detail(self,id,updates,obsoletes,vendor_url,bugzilla_url,cve_url,restart,update_text):
        '''
        Send 'updatedetail' signal
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param updates:
        @param obsoletes:
        @param vendor_url:
        @param bugzilla_url:
        @param cve_url:
        @param restart:
        @param update_text:
        '''
        self.pk_iface.update_detail(self.tid,id,updates,obsoletes,vendor_url,bugzilla_url,cve_url,restart,update_text)

    def require_restart(self,restart_type,details):
        '''
        Send 'requirerestart' signal
        @param restart_type: RESTART_SYSTEM,RESTART_APPLICATION,RESTART_SESSION
        @param details: Optional details about the restart
        '''
        self.pk_iface.require_restart(self.tid,restart_type,details)

    def allow_interrupt(self,allow):
        '''
        send 'allow-interrupt' signal:
        @param allow:  Allow the current process to be aborted.
        '''
        if allow:
            data = 'true'
        else:
            data = 'false'
        self.pk_iface.allow_interrupt(self.tid,data)

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
        self.pk_iface.repo_signature_required(self.tid,repo_name,key_url,key_userid,key_id,key_fingerprint,key_timestamp,type)

#
# Actions ( client -> engine -> backend )
#
    def quit(self):
        if self.isLocked():
            self.unLock()

        self.loop.quit()

    def search_name(self,tid,filters,key):
        '''
        Implement the {backend}-search-name functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def search_details(self,tid,filters,key):
        '''
        Implement the {backend}-search-details functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def search_group(self,tid,filters,key):
        '''
        Implement the {backend}-search-group functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def search_file(self,tid,filters,key):
        '''
        Implement the {backend}-search-file functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_update_detail(self,tid,package):
        '''
        Implement the {backend}-get-update-detail functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_depends(self,tid,package,recursive):
        '''
        Implement the {backend}-get-depends functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_requires(self,tid,package,recursive):
        '''
        Implement the {backend}-get-requires functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def update_system(self,tid):
        '''
        Implement the {backend}-update-system functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def refresh_cache(self,tid):
        '''
        Implement the {backend}-refresh_cache functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def install(self,tid,package):
        '''
        Implement the {backend}-install functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def install_file (self,tid,inst_file):
        '''
        Implement the {backend}-install_file functionality
        Install the package containing the inst_file file
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def resolve(self,tid,name):
        '''
        Implement the {backend}-resolve functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def remove(self,tid,allowdep,package):
        '''
        Implement the {backend}-remove functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def update(self,tid,package):
        '''
        Implement the {backend}-update functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_description(self,tid,package):
        '''
        Implement the {backend}-get-description functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_files(self,tid,package):
        '''
        Implement the {backend}-get-files functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_updates(self,tid,package):
        '''
        Implement the {backend}-get-updates functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def repo_enable(self,tid,repoid,enable):
        '''
        Implement the {backend}-repo-enable functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def repo_set_data(self,tid,repoid,parameter,value):
        '''
        Implement the {backend}-repo-set-data functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_repo_list(self,tid):
        '''
        Implement the {backend}-get-repo-list functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

#
# Utility methods
#

    def get_package_id(self,name,version,arch,data):
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

