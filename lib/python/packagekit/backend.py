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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# Copyright (C) 2007 Tim Lauridsen <timlau@fedoraproject.org>
# Copyright (C) 2007-2010 Richard Hughes <richard@hughsie.com>
#
# This file contain the base classes to implement a PackageKit python backend
#


# imports
from __future__ import print_function

import sys
import traceback
import os.path

from .enums import *

PACKAGE_IDS_DELIM = ','
FILENAME_DELIM = '|'

def split_package_ids(package_ids_str):
    package_ids=[]
    package_id=''

    for i in range(0, len(package_ids_str)):
        if i == 0 and package_ids_str[i] == PACKAGE_IDS_DELIM:
            continue

        if package_ids_str[i] == PACKAGE_IDS_DELIM and package_ids_str[i-1] != '\\' and i > 0:
            package_ids.append(package_id)
            package_id=''
        else:
            package_id=package_id + package_ids_str[i]

    if len(package_id) > 0:
        package_ids.append(package_id)

    return package_ids

def _to_unicode(txt, encoding='utf-8'):
    if isinstance(txt, str):
        if not isinstance(txt, str):
            txt = str(txt, encoding, errors='replace')
    return txt

def _to_utf8(txt, errors='replace'):
    if isinstance(txt, str):
        return txt
    if isinstance(txt, unicode):
        return txt.encode('utf-8', errors=errors)
    return str(txt)

class PkError(Exception):
    def __init__(self, code, details):
        self.code = code
        self.details = details
    def __str__(self):
        return repr("%s: %s" % (self.code, self.details))

class PackageKitBaseBackend:

    def __init__(self, cmds):
        # Setup a custom exception handler
        installExceptionHandler(self)
        self.cmds = cmds
        self._locked = False
        self.lang = "C"
        self.has_network = False
        self.uid = 0
        self.background = False
        self.interactive = False
        self.cache_age = 0
        self.percentage_old = 0

        # try to get LANG
        try:
            self.lang = os.environ['LANG']
        except KeyError as e:
            print("Error: No LANG envp")

        # try to get NETWORK state
        try:
            if os.environ['NETWORK'] == 'TRUE':
                self.has_network = True
        except KeyError as e:
            print("Error: No NETWORK envp")

        # try to get UID of running user
        try:
            self.uid = int(os.environ['UID'])
        except KeyError as e:
            print("Error: No UID envp")

        # try to get BACKGROUND state
        try:
            if os.environ['BACKGROUND'] == 'TRUE':
                self.background = True
        except KeyError as e:
            print("Error: No BACKGROUND envp")

        # try to get INTERACTIVE state
        try:
            if os.environ['INTERACTIVE'] == 'TRUE':
                self.interactive = True
        except KeyError as e:
            print("Error: No INTERACTIVE envp")

        # try to get CACHE_AGE state
        try:
            self.cache_age = int(os.environ['CACHE_AGE'])
        except KeyError as e:
            pass

    def doLock(self):
        ''' Generic locking, overide and extend in child class'''
        self._locked = True

    def unLock(self):
        ''' Generic unlocking, overide and extend in child class'''
        self._locked = False

    def isLocked(self):
        return self._locked

    def percentage(self, percent=None):
        '''
        Write progress percentage
        @param percent: Progress percentage (int preferred)
        '''
        if percent == None:
            sys.stdout.write(_to_utf8("no-percentage-updates\n"))
        elif percent == 0 or percent > self.percentage_old:
            sys.stdout.write(_to_utf8("percentage\t%i\n" % percent))
            self.percentage_old = percent
        sys.stdout.flush()

    def speed(self, bps=0):
        '''
        Write progress speed
        @param bps: Progress speed (int, bytes per second)
        '''
        sys.stdout.write(_to_utf8("speed\t%i\n" % bps))
        sys.stdout.flush()

    def item_progress(self, package_id, status, percent=None):
        '''
        send 'itemprogress' signal
        @param package_id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param percent: percentage of the current item (int preferred)
        '''
        sys.stdout.write(_to_utf8("item-progress\t%s\t%s\t%i\n" % (package_id, status, percent)))
        sys.stdout.flush()

    def error(self, err, description, exit=True):
        '''
        send 'error'
        @param err: Error Type (ERROR_NO_NETWORK, ERROR_NOT_SUPPORTED, ERROR_INTERNAL_ERROR)
        @param description: Error description
        @param exit: exit application with rc = 1, if true
        '''
        # unlock before we emit if we are going to exit
        if exit and self.isLocked():
            self.unLock()

        # this should be fast now
        sys.stdout.write(_to_utf8("error\t%s\t%s\n" % (err, description)))
        sys.stdout.flush()
        if exit:
            # Paradoxically, we don't want to print "finished" to stdout here.
            # Python takes an _enormous_ amount of time to exit, and leaves a
            # huge race when you try to change a dispatcher because of an error.
            #
            # Leave PackageKit to clean up for us in this case.
            sys.exit(254)

    def message(self, typ, msg):
        '''
        send 'message' signal
        @param typ: MESSAGE_BROKEN_MIRROR
        '''
        sys.stdout.write(_to_utf8("message\t%s\t%s\n" % (typ, msg)))
        sys.stdout.flush()

    def package(self, package_id, status, summary):
        '''
        send 'package' signal
        @param info: the enumerated INFO_* string
        @param package_id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param summary: The package Summary
        '''
        sys.stdout.write(_to_utf8("package\t%s\t%s\t%s\n" % (status, package_id, summary)))
        sys.stdout.flush()

    def media_change_required(self, mtype, id, text):
        '''
        send 'media-change-required' signal
        @param mtype: the enumerated MEDIA_TYPE_* string
        @param id: the localised label of the media
        @param text: the localised text describing the media
        '''
        sys.stdout.write(_to_utf8("media-change-required\t%s\t%s\t%s\n" % (mtype, id, text)))
        sys.stdout.flush()

    def distro_upgrade(self, dtype, name, summary):
        '''
        send 'distro-upgrade' signal
        @param dtype: the enumerated DISTRO_UPGRADE_* string
        @param name: The distro name, e.g. "fedora-9"
        @param summary: The localised distribution name and description
        '''
        sys.stdout.write(_to_utf8("distro-upgrade\t%s\t%s\t%s\n" % (dtype, name, summary)))
        sys.stdout.flush()

    def status(self, state):
        '''
        send 'status' signal
        @param state: STATUS_DOWNLOAD, STATUS_INSTALL, STATUS_UPDATE, STATUS_REMOVE, STATUS_WAIT
        '''
        sys.stdout.write(_to_utf8("status\t%s\n" % state))
        sys.stdout.flush()

    def repo_detail(self, repoid, name, state):
        '''
        send 'repo-detail' signal
        @param repoid: The repo id tag
        @param state: false is repo is disabled else true.
        '''
        sys.stdout.write(_to_utf8("repo-detail\t%s\t%s\t%s\n" % (repoid, name, _bool_to_string(state))))
        sys.stdout.flush()

    def data(self, data):
        '''
        send 'data' signal:
        @param data:  The current worked on package
        '''
        sys.stdout.write(_to_utf8("data\t%s\n" % data))
        sys.stdout.flush()

    def details(self, package_id, summary, package_license, group, desc, url, bytes):
        '''
        Send 'details' signal
        @param package_id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param summary: The package summary
        @param package_license: The license of the package
        @param group: The enumerated group
        @param desc: The multi line package description
        @param url: The upstream project homepage
        @param bytes: The size of the package, in bytes
        '''
        sys.stdout.write(_to_utf8("details\t%s\t%s\t%s\t%s\t%s\t%s\t%ld\n" % (package_id, summary, package_license, group, desc, url, bytes)))
        sys.stdout.flush()

    def files(self, package_id, file_list):
        '''
        Send 'files' signal
        @param file_list: List of the files in the package, separated by ';'
        '''
        sys.stdout.write(_to_utf8("files\t%s\t%s\n" % (package_id, file_list)))
        sys.stdout.flush()

    def category(self, parent_id, cat_id, name, summary, icon):
        '''
        Send 'category' signal
        parent_id : A parent id, e.g. "admin" or "" if there is no parent
        cat_id    : a unique category id, e.g. "admin;network"
        name      : a verbose category name in current locale.
        summery   : a summary of the category in current locale.
        icon      : an icon name to represent the category
        '''
        sys.stdout.write(_to_utf8("category\t%s\t%s\t%s\t%s\t%s\n" % (parent_id, cat_id, name, summary, icon)))
        sys.stdout.flush()

    def finished(self):
        '''
        Send 'finished' signal
        '''
        sys.stdout.write(_to_utf8("finished\n"))
        sys.stdout.flush()

    def update_detail(self, package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text, changelog, state, issued, updated):
        '''
        Send 'updatedetail' signal
        @param package_id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
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
        sys.stdout.write(_to_utf8("updatedetail\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" % (package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text, changelog, state, issued, updated)))
        sys.stdout.flush()

    def require_restart(self, restart_type, details):
        '''
        Send 'requirerestart' signal
        @param restart_type: RESTART_SYSTEM, RESTART_APPLICATION, RESTART_SESSION
        @param details: Optional details about the restart
        '''
        sys.stdout.write(_to_utf8("requirerestart\t%s\t%s\n" % (restart_type, details)))
        sys.stdout.flush()

    def allow_cancel(self, allow):
        '''
        send 'allow-cancel' signal:
        @param allow:  Allow the current process to be aborted.
        '''
        if allow:
            data = 'true'
        else:
            data = 'false'
        sys.stdout.write(_to_utf8("allow-cancel\t%s\n" % data))
        sys.stdout.flush()

    def repo_signature_required(self, package_id, repo_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, sig_type):
        '''
        send 'repo-signature-required' signal:
        @param package_id:      Id of the package needing a signature
        @param repo_name:       Name of the repository
        @param key_url:         URL which the user can use to verify the key
        @param key_userid:      Key userid
        @param key_id:          Key ID
        @param key_fingerprint: Full key fingerprint
        @param key_timestamp:   Key timestamp
        @param sig_type:        Key type (GPG)
        '''
        sys.stdout.write(_to_utf8("repo-signature-required\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" % (
            package_id, repo_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, sig_type
            )))
        sys.stdout.flush()

    def eula_required(self, eula_id, package_id, vendor_name, license_agreement):
        '''
        send 'eula-required' signal:
        @param eula_id:         Id of the EULA
        @param package_id:      Id of the package needing a signature
        @param vendor_name:     Name of the vendor that wrote the EULA
        @param license_agreement: The license text
        '''
        sys.stdout.write(_to_utf8("eula-required\t%s\t%s\t%s\t%s\n" % (
            eula_id, package_id, vendor_name, license_agreement
            )))
        sys.stdout.flush()

#
# Backend Action Methods
#

    def search_name(self, filters, values):
        '''
        Implement the {backend}-search-name functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def search_details(self, filters, values):
        '''
        Implement the {backend}-search-details functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def search_group(self, filters, values):
        '''
        Implement the {backend}-search-group functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def search_file(self, filters, values):
        '''
        Implement the {backend}-search-file functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def get_update_detail(self, package_ids):
        '''
        Implement the {backend}-get-update-detail functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def depends_on(self, filters, package_ids, recursive):
        '''
        Implement the {backend}-depends-on functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def get_packages(self, filters):
        '''
        Implement the {backend}-get-packages functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def required_by(self, filters, package_ids, recursive):
        '''
        Implement the {backend}-required-by functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def what_provides(self, filters, provides_type, values):
        '''
        Implement the {backend}-what-provides functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def upgrade_system(self, distro_id):
        '''
        Implement the {backend}-update-system functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def refresh_cache(self, force):
        '''
        Implement the {backend}-refresh_cache functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def install_packages(self, transaction_flags, package_ids):
        '''
        Implement the {backend}-install functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def install_signature(self, sigtype, key_id, package_id):
        '''
        Implement the {backend}-install-signature functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def install_files(self, transaction_flags, inst_files):
        '''
        Implement the {backend}-install_files functionality
        Install the package containing the inst_file file
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def resolve(self, filters, values):
        '''
        Implement the {backend}-resolve functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def remove_packages(self, transaction_flags, package_ids, allowdep, autoremove):
        '''
        Implement the {backend}-remove functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def update_packages(self, transaction_flags, package_ids):
        '''
        Implement the {backend}-update functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def get_details(self, package_ids):
        '''
        Implement the {backend}-get-details functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def get_details_local(self, files):
        '''
        Implement the {backend}-get-details-local functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def get_files(self, package_ids):
        '''
        Implement the {backend}-get-files functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def get_updates(self, filters):
        '''
        Implement the {backend}-get-updates functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def get_distro_upgrades(self):
        '''
        Implement the {backend}-get-distro-upgrades functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def repo_enable(self, repoid, enable):
        '''
        Implement the {backend}-repo-enable functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def repo_set_data(self, repoid, parameter, value):
        '''
        Implement the {backend}-repo-set-data functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def get_repo_list(self, filters):
        '''
        Implement the {backend}-get-repo-list functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def repo_signature_install(self, package_id):
        '''
        Implement the {backend}-repo-signature-install functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def download_packages(self, directory, package_ids):
        '''
        Implement the {backend}-download-packages functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def set_locale(self, code):
        '''
        Implement the {backend}-set-locale functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def get_categories(self):
        '''
        Implement the {backend}-get-categories functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def repair_system(self, transaction_flags):
        '''
        Implement the {backend}-repair-system functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend",
                   exit=False)

    def customTracebackHandler(self, tb):
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
        self.dispatch_command(cmd, args)

    def dispatch_command(self, cmd, args):
        if cmd == 'download-packages':
            directory = args[0]
            package_ids = split_package_ids(args[1])
            self.download_packages(directory, package_ids)
            self.finished()
        elif cmd == 'depends-on':
            filters = args[0].split(';')
            package_ids = split_package_ids(args[1])
            recursive = _text_to_bool(args[2])
            self.depends_on(filters, package_ids, recursive)
            self.finished()
        elif cmd == 'get-details':
            package_ids = split_package_ids(args[0])
            self.get_details(package_ids)
            self.finished()
        elif cmd == 'get-details-local':
            package_ids = split_package_ids(args[0])
            self.get_details_local(files)
            self.finished()
        elif cmd == 'get-files':
            package_ids = split_package_ids(args[0])
            self.get_files(package_ids)
            self.finished()
        elif cmd == 'get-packages':
            filters = args[0].split(';')
            self.get_packages(filters)
            self.finished()
        elif cmd == 'get-repo-list':
            filters = args[0].split(';')
            self.get_repo_list(filters)
            self.finished()
        elif cmd == 'required-by':
            filters = args[0].split(';')
            package_ids = split_package_ids(args[1])
            recursive = _text_to_bool(args[2])
            self.required_by(filters, package_ids, recursive)
            self.finished()
        elif cmd == 'get-update-detail':
            package_ids = split_package_ids(args[0])
            self.get_update_detail(package_ids)
            self.finished()
        elif cmd == 'get-distro-upgrades':
            self.get_distro_upgrades()
            self.finished()
        elif cmd == 'get-updates':
            filters = args[0].split(';')
            self.get_updates(filters)
            self.finished()
        elif cmd == 'install-files':
            transaction_flags = args[0].split(';')
            files_to_inst = args[1].split(FILENAME_DELIM)
            self.install_files(transaction_flags, files_to_inst)
            self.finished()
        elif cmd == 'install-packages':
            transaction_flags = args[0].split(';')
            package_ids = split_package_ids(args[1])
            self.install_packages(transaction_flags, package_ids)
            self.finished()
        elif cmd == 'install-signature':
            sigtype = args[0]
            key_id = args[1]
            package_id = args[2]
            self.install_signature(sigtype, key_id, package_id)
            self.finished()
        elif cmd == 'refresh-cache':
            force = _text_to_bool(args[0])
            self.refresh_cache(force)
            self.finished()
        elif cmd == 'remove-packages':
            transaction_flags = args[0].split(';')
            package_ids = split_package_ids(args[1])
            allowdeps = _text_to_bool(args[2])
            autoremove = _text_to_bool(args[3])
            self.remove_packages(transaction_flags, package_ids, allowdeps, autoremove)
            self.finished()
        elif cmd == 'repo-enable':
            repoid = args[0]
            state = _text_to_bool(args[1])
            self.repo_enable(repoid, state)
            self.finished()
        elif cmd == 'repo-set-data':
            repoid = args[0]
            para = args[1]
            value = args[2]
            self.repo_set_data(repoid, para, value)
            self.finished()
        elif cmd == 'resolve':
            filters = args[0].split(';')
            package_ids = split_package_ids(args[1])
            self.resolve(filters, package_ids)
            self.finished()
        elif cmd == 'search-details':
            filters = args[0].split(';')
            values = split_package_ids(_to_unicode(args[1]))
            self.search_details(filters, values)
            self.finished()
        elif cmd == 'search-file':
            filters = args[0].split(';')
            package_ids = split_package_ids(args[1])
            self.search_file(filters, values)
            self.finished()
        elif cmd == 'search-group':
            filters = args[0].split(';')
            package_ids = split_package_ids(args[1])
            self.search_group(filters, values)
            self.finished()
        elif cmd == 'search-name':
            filters = args[0].split(';')
            values = split_package_ids(_to_unicode(args[1]))
            self.search_name(filters, values)
            self.finished()
        elif cmd == 'signature-install':
            package = args[0]
            self.repo_signature_install(package)
            self.finished()
        elif cmd == 'update-packages':
            transaction_flags = args[0].split(';')
            package_ids = split_package_ids(args[1])
            self.update_packages(transaction_flags, package_ids)
            self.finished()
        elif cmd == 'what-provides':
            filters = args[0].split(';')
            provides_type = args[1]
            values = split_package_ids(_to_unicode(args[2]))
            self.what_provides(filters, provides_type, values)
            self.finished()
        elif cmd == 'set-locale':
            code = args[0]
            self.set_locale(code)
            self.finished()
        elif cmd == 'get-categories':
            self.get_categories()
            self.finished()
        elif cmd == 'upgrade-system':
            self.upgrade_system(args[0])
            self.finished()
        elif cmd == 'repair-system':
            self.repair_system(args[0])
            self.finished()
        else:
            errmsg = "command '%s' is not known" % cmd
            self.error(ERROR_INTERNAL_ERROR, errmsg, exit=False)
            self.finished()

    def dispatcher(self, args):
        if len(args) > 0:
            self.dispatch_command(args[0], args[1:])
        while True:
            try:
                line = sys.stdin.readline().strip('\n')
            except IOError as e:
                self.error(ERROR_TRANSACTION_CANCELLED, 'could not read from stdin: %s' % str(e))
            except KeyboardInterrupt as e:
                self.error(ERROR_PROCESS_KILL, 'process was killed by ctrl-c: %s' % str(e))
            if not line or line == 'exit':
                break
            args = line.split('\t')
            self.dispatch_command(args[0], args[1:])

        # unlock backend and exit with success
        if self.isLocked():
            self.unLock()
        sys.exit(0)


def format_string(text, encoding='utf-8'):
    '''
    Format a string to be used on stdout for communication with the daemon.
    '''
    if not isinstance(text, str):
        text = str(text, encoding, errors='replace')
    return text.replace("\n", ";")

def _text_to_bool(text):
    '''Convert a string to a boolean value.'''
    if text.lower() in ["yes", "true"]:
        return True
    return False

def _bool_to_string(value):
    if value:
        return "true"
    return "false"

def get_package_id(name, version, arch, data):
    """Returns a package id."""
    return ";".join((name, version, arch, data))

def split_package_id(id):
    """
    Returns a tuple with the name, version, arch and data component of a
    package id.
    """
    return id.split(";", 4)

def exceptionHandler(typ, value, tb, base):
    # Restore original exception handler
    sys.excepthook = sys.__excepthook__
    # Call backend custom Traceback handler
    if not base.customTracebackHandler(typ):
        etb = traceback.extract_tb(tb)
        errmsg = 'Error Type: %s;' % str(typ)
        errmsg += 'Error Value: %s;' % str(value)
        for tub in etb:
            f, l, m, c = tub # file, lineno, function, codeline
            errmsg += '  File : %s, line %s, in %s;' % (f, str(l), m)
            errmsg += '    %s;' % c
        # send the traceback to PackageKit
        base.error(ERROR_INTERNAL_ERROR, errmsg, exit=True)

def installExceptionHandler(base):
    sys.excepthook = lambda typ, value, tb: exceptionHandler(typ, value, tb, base)

