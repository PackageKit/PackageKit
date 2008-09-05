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
import codecs
import traceback
import locale
import os.path
sys.stdout = codecs.getwriter('utf-8')(sys.stdout)

from enums import *

# Classes

class PackageKitBaseBackend:

    def __init__(self,cmds):
        # Setup a custom exception handler
        installExceptionHandler(self)
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
            print "percentage\t%i" % (percent)
        else:
            print "no-percentage-updates"
        sys.stdout.flush()

    def sub_percentage(self,percent=None):
        '''
        send 'subpercentage' signal : subprogress percentage
        @param percent: subprogress percentage
        '''
        print "subpercentage\t%i" % (percent)
        sys.stdout.flush()

    def error(self,err,description,exit=True):
        '''
        send 'error'
        @param err: Error Type (ERROR_NO_NETWORK, ERROR_NOT_SUPPORTED, ERROR_INTERNAL_ERROR)
        @param description: Error description
        @param exit: exit application with rc=1, if true
        '''
        print "error\t%s\t%s" % (err,description)
        sys.stdout.flush()
        if exit:
            if self.isLocked():
                self.unLock()
            sys.exit(1)

    def message(self,typ,msg):
        '''
        send 'message' signal
        @param typ: MESSAGE_BROKEN_MIRROR
        '''
        print "message\t%s\t%s" % (typ,msg)
        sys.stdout.flush()

    def package(self,id,status,summary):
        '''
        send 'package' signal
        @param info: the enumerated INFO_* string
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param summary: The package Summary
        '''
        print >> sys.stdout,"package\t%s\t%s\t%s" % (status,id,summary)
        sys.stdout.flush()

    def distro_upgrade(self,dtype,name,summary):
        '''
        send 'distro-upgrade' signal
        @param dtype: the enumerated DISTRO_UPGRADE_* string
        @param name: The distro name, e.g. "fedora-9"
        @param summary: The localised distribution name and description
        '''
        print >> sys.stdout,"distro-upgrade\t%s\t%s\t%s" % (dtype,name,summary)
        sys.stdout.flush()

    def status(self,state):
        '''
        send 'status' signal
        @param state: STATUS_DOWNLOAD, STATUS_INSTALL, STATUS_UPDATE, STATUS_REMOVE, STATUS_WAIT
        '''
        print "status\t%s" % (state)
        sys.stdout.flush()

    def repo_detail(self,repoid,name,state):
        '''
        send 'repo-detail' signal
        @param repoid: The repo id tag
        @param state: false is repo is disabled else true.
        '''
        print >> sys.stdout,"repo-detail\t%s\t%s\t%s" % (repoid,name,state)
        sys.stdout.flush()

    def data(self,data):
        '''
        send 'data' signal:
        @param data:  The current worked on package
        '''
        print "data\t%s" % (data)
        sys.stdout.flush()

    def details(self,id,license,group,desc,url,bytes):
        '''
        Send 'details' signal
        @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param license: The license of the package
        @param group: The enumerated group
        @param desc: The multi line package description
        @param url: The upstream project homepage
        @param bytes: The size of the package, in bytes
        '''
        print >> sys.stdout,"details\t%s\t%s\t%s\t%s\t%s\t%ld" % (id,license,group,desc,url,bytes)
        sys.stdout.flush()

    def files(self,id,file_list):
        '''
        Send 'files' signal
        @param file_list: List of the files in the package, separated by ';'
        '''
        print >> sys.stdout,"files\t%s\t%s" % (id,file_list)
        sys.stdout.flush()

    def finished(self):
        '''
        Send 'finished' signal
        '''
        print >> sys.stdout,"finished"
        sys.stdout.flush()

    def update_detail(self,id,updates,obsoletes,vendor_url,bugzilla_url,cve_url,restart,update_text,changelog,state,issued,updated):
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
        @param changelog:
        @param state:
        @param issued:
        @param updated:
        '''
        print >> sys.stdout,"updatedetail\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s" % (id,updates,obsoletes,vendor_url,bugzilla_url,cve_url,restart,update_text,changelog,state,issued,updated)
        sys.stdout.flush()

    def require_restart(self,restart_type,details):
        '''
        Send 'requirerestart' signal
        @param restart_type: RESTART_SYSTEM, RESTART_APPLICATION,RESTART_SESSION
        @param details: Optional details about the restart
        '''
        print "requirerestart\t%s\t%s" % (restart_type,details)
        sys.stdout.flush()

    def allow_cancel(self,allow):
        '''
        send 'allow-cancel' signal:
        @param allow:  Allow the current process to be aborted.
        '''
        if allow:
            data = 'true'
        else:
            data = 'false'
        print "allow-cancel\t%s" % (data)
        sys.stdout.flush()

    def repo_signature_required(self,id,repo_name,key_url,key_userid,key_id,key_fingerprint,key_timestamp,type):
        '''
        send 'repo-signature-required' signal:
        @param id:           Id of the package needing a signature
        @param repo_name:       Name of the repository
        @param key_url:         URL which the user can use to verify the key
        @param key_userid:      Key userid
        @param key_id:          Key ID
        @param key_fingerprint: Full key fingerprint
        @param key_timestamp:   Key timestamp
        @param type:            Key type (GPG)
        '''
        print "repo-signature-required\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s" % (
            id,repo_name,key_url,key_userid,key_id,key_fingerprint,key_timestamp,type
            )
        sys.stdout.flush()

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

    def get_update_detail(self,package_ids_ids):
        '''
        Implement the {backend}-get-update-detail functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_depends(self,filters,package_ids,recursive):
        '''
        Implement the {backend}-get-depends functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_packages(self,filters):
        '''
        Implement the {backend}-get-packages functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_requires(self,filters,package_ids,recursive):
        '''
        Implement the {backend}-get-requires functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def what_provides(self,filters,provides_type,search):
        '''
        Implement the {backend}-what-provides functionality
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

    def install_packages(self,package_ids):
        '''
        Implement the {backend}-install functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def install_files (self,trusted,inst_files):
        '''
        Implement the {backend}-install_files functionality
        Install the package containing the inst_file file
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def service_pack (self,location):
        '''
        Implement the {backend}-service-pack functionality
        Update the computer from a service pack in location
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def resolve(self,name):
        '''
        Implement the {backend}-resolve functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def remove_packages(self,allowdep,package_ids):
        '''
        Implement the {backend}-remove functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def update_packages(self,package):
        '''
        Implement the {backend}-update functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_details(self,package):
        '''
        Implement the {backend}-get-details functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_files(self,package):
        '''
        Implement the {backend}-get-files functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_updates(self,filter):
        '''
        Implement the {backend}-get-updates functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_distro_upgrades(self):
        '''
        Implement the {backend}-get-distro-upgrades functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def repo_enable(self,repoid,enable):
        '''
        Implement the {backend}-repo-enable functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def repo_set_data(self,repoid,parameter,value):
        '''
        Implement the {backend}-repo-set-data functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def get_repo_list(self,filters):
        '''
        Implement the {backend}-get-repo-list functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def repo_signature_install(self,package):
        '''
        Implement the {backend}-repo-signature-install functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def download_packages(self,directory,packages):
        '''
        Implement the {backend}-download-packages functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED,"This function is not implemented in this backend")

    def customTracebackHandler(self,tb):
        '''
        Custom Traceback Handler
        this is called by the ExceptionHandler
        return True if the exception is handled in the method.
        return False if to do the default action an signal an error
        to packagekit.
        Overload this method if you what handle special Tracebacks
        '''
        return False

    def run_command(self):
        '''
        interprete the command from the calling args (self.cmds)
        '''
        fname = os.path.split(self.cmds[0])[1]
        cmd = fname.split('.')[0] # get the helper filename wo ext
        args = self.cmds[1:]
        self.dispatch_command(cmd,args)

    def dispatch_command(self,cmd,args):
        if cmd == 'download-packages':
            dir = args[0]
            pkgs = args[1:]
            self.download_packages(dir,pkgs)
            self.finished();
        elif cmd == 'get-depends':
            filters = args[0]
            pkgs = args[1].split('|')
            recursive = args[2]
            self.get_depends(filters,pkgs,recursive)
            self.finished();
        elif cmd == 'get-details':
            pkgs = args[0].split('|')
            self.get_details(pkgs)
            self.finished();
        elif cmd == 'get-files':
            pkgs = args[0].split('|')
            self.get_files(pkgs)
            self.finished();
        elif cmd == 'get-packages':
            filters = args[0]
            self.get_packages(filters)
            self.finished();
        elif cmd == 'get-repo-list':
            filters = args[0]
            self.get_repo_list(filters)
            self.finished();
        elif cmd == 'get-requires':
            filters = args[0]
            pkgs = args[1].split('|')
            recursive = args[2]
            self.get_requires(filters,pkgs,recursive)
            self.finished();
        elif cmd == 'get-update-detail':
            pkgs = args[0].split('|')
            self.get_update_detail(pkgs)
            self.finished();
        elif cmd == 'get-updates':
            filters = args[0]
            self.get_updates(filters)
            self.finished();
        elif cmd == 'install-files':
            trusted = args[0]
            files_to_inst = args[1:]
            self.install_files(trusted,files_to_inst)
            self.finished();
        elif cmd == 'install-packages':
            pkgs = args[0:]
            self.install_packages(pkgs)
            self.finished();
        elif cmd == 'install-signature':
            sigtype = args[0]
            key_id = args[1]
            package_id = args[2]
            self.install_signature(sigtype,key_id,package_id)
            self.finished();
        elif cmd == 'refresh-cache':
            self.refresh_cache()
            self.finished();
        elif cmd == 'remove-packages':
            allowdeps = args[0]
            packages = args[1:]
            self.remove_packages(allowdeps,packages)
            self.finished();
        elif cmd == 'repo-enable':
            repoid = args[0]
            state=args[1]
            self.repo_enable(repoid,state)
            self.finished();
        elif cmd == 'repo-set-data':
            repoid = args[0]
            para = args[1]
            value=args[2]
            self.repo_set_data(repoid,para,value)
            self.finished();
        elif cmd == 'resolve':
            filters = args[0]
            packages = args[1:]
            self.resolve(filters,packages)
            self.finished();
        elif cmd == 'search-details':
            options = args[0]
            searchterms = args[1]
            self.search_details(options,searchterms)
            self.finished();
        elif cmd == 'search-file':
            options = args[0]
            searchterms = args[1]
            self.search_file(options,searchterms)
            self.finished();
        elif cmd == 'search-group':
            options = args[0]
            searchterms = args[1]
            self.search_group(options,searchterms)
            self.finished();
        elif cmd == 'search-name':
            options = args[0]
            searchterms = args[1]
            self.search_name(options,searchterms)
            self.finished();
        elif cmd == 'signature-install':
            package = args[0]
            self.repo_signature_install(package)
            self.finished();
        elif cmd == 'update-packages':
            packages = args[0:]
            self.update_packages(packages)
            self.finished();
        elif cmd == 'update-system':
            self.update_system()
            self.finished();
        elif cmd == 'what-provides':
            filters = args[0]
            provides_type = args[1]
            search = args[2]
            self.what_provides(filters,provides_type,search)
            self.finished();
        else:
            print "command [%s] is not known" % cmd



def exceptionHandler(typ,value,tb,base):
    # Restore original exception handler
    sys.excepthook = sys.__excepthook__
    # Call backend custom Traceback handler
    if not base.customTracebackHandler(typ):
        etb = traceback.extract_tb(tb)
        errmsg = 'Error Type: %s;' % str(typ)
        errmsg += 'Error Value: %s;' % str(value)
        for tub in etb:
            f,l,m,c = tub # file,lineno,function,codeline
            errmsg += '  File : %s, line %s, in %s;' % (f,str(l),m)
            errmsg += '    %s;' % c
        # send the traceback to PackageKit
        base.error(ERROR_INTERNAL_ERROR,errmsg,exit=True)


def installExceptionHandler(base):
    sys.excepthook = lambda typ,value,tb: exceptionHandler(typ,value,tb,base)

