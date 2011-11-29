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
from __future__ import print_function

# imports
import sys
import codecs
import traceback
import os.path

from .enums import *

PACKAGE_IDS_DELIM = '&'
FILENAME_DELIM = '|'

def _to_unicode(txt, encoding='utf-8'):
    if isinstance(txt, basestring):
        if not isinstance(txt, unicode):
            txt = unicode(txt, encoding, errors='replace')
    return txt

def _to_utf8(txt, errors='replace'):
    '''convert practically anything to a utf-8-encoded byte string'''

    # convert to unicode object
    if isinstance(txt, str):
        txt = txt.decode('utf-8', errors=errors)
    if not isinstance(txt, basestring):
        # try to convert non-string objects like exceptions
        try:
            # if txt.__unicode__() exists, or txt.__str__() returns ASCII
            txt = unicode(txt)
        except UnicodeDecodeError:
            # if txt.__str__() exists
            txt = str(txt).decode('utf-8', errors=errors)
        except:
            # no __str__(), __unicode__() methods, use representation
            txt = unicode(repr(txt))

    # return encoded as UTF-8
    return txt.encode('utf-8', errors=errors)

# Classes

class _UTF8Writer(codecs.StreamWriter):

    encoding = 'utf-8'

    def __init__(self, stream, errors='replace'):
        codecs.StreamWriter.__init__(self, stream, errors)

    def encode(self, inp, errors='strict'):
        try:
            l = len(inp)
        except TypeError:
            try:
                l = len(unicode(inp))
            except:
                try:
                    l = len(str(inp))
                except:
                    l = 1
        return (_to_utf8(inp, errors=errors), l)

class PkError(Exception):
    def __init__(self, code, details):
        self.code = code
        self.details = details
    def __str__(self):
        return repr("%s: %s" % (self.code, self.details))

class PackageKitBaseBackend:

    def __init__(self, cmds):
        # Make sys.stdout/stderr cope with UTF-8
        sys.stdout = _UTF8Writer(sys.stdout)
        sys.stderr = _UTF8Writer(sys.stderr)

        # Setup a custom exception handler
        installExceptionHandler(self)
        self.cmds = cmds
        self._locked = False
        self.lang = "C"
        self.has_network = False
        self.background = False
        self.interactive = False
        self.cache_age = 0
        self.percentage_old = 0
        self.sub_percentage_old = 0

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
            print("no-percentage-updates")
        elif percent == 0 or percent > self.percentage_old:
            print("percentage\t%i" % (percent))
            self.percentage_old = percent
        sys.stdout.flush()

    def speed(self, bps=0):
        '''
        Write progress speed
        @param bps: Progress speed (int, bytes per second)
        '''
        print("speed\t%i" % (bps))
        sys.stdout.flush()

    def item_percentage(self, package_id, percent=None):
        '''
        send 'itemprogress' signal
        @param package_id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param percent: percentage of the current item (int preferred)
        '''
        print("item-percentage\t%s\t%i" % (package_id, percent))
        sys.stdout.flush()

    def sub_percentage(self, percent=None):
        '''
        send 'subpercentage' signal : subprogress percentage
        @param percent: subprogress percentage (int preferred)
        '''
        if percent == 0 or percent > self.sub_percentage_old:
            print("subpercentage\t%i" % (percent))
            self.sub_percentage_old = percent
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
        print("error\t%s\t%s" % (err, description))
        sys.stdout.flush()
        if exit:
            # Paradoxically, we don't want to print "finished" to stdout here.
            # Python takes an _enormous_ amount of time to exit, and leaves a
            # huge race when you try to change a dispatcher because of an error.
            #
            # Leave PackageKit to clean up for us in this case.
            sys.exit(1)

    def message(self, typ, msg):
        '''
        send 'message' signal
        @param typ: MESSAGE_BROKEN_MIRROR
        '''
        print("message\t%s\t%s" % (typ, msg))
        sys.stdout.flush()

    def package(self, package_id, status, summary):
        '''
        send 'package' signal
        @param info: the enumerated INFO_* string
        @param package_id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param summary: The package Summary
        '''
        print("package\t%s\t%s\t%s" % (status, package_id, summary), file=sys.stdout)
        sys.stdout.flush()

    def media_change_required(self, mtype, id, text):
        '''
        send 'media-change-required' signal
        @param mtype: the enumerated MEDIA_TYPE_* string
        @param id: the localised label of the media
        @param text: the localised text describing the media
        '''
        print("media-change-required\t%s\t%s\t%s" % (mtype, id, text), file=sys.stdout)
        sys.stdout.flush()

    def distro_upgrade(self, dtype, name, summary):
        '''
        send 'distro-upgrade' signal
        @param dtype: the enumerated DISTRO_UPGRADE_* string
        @param name: The distro name, e.g. "fedora-9"
        @param summary: The localised distribution name and description
        '''
        print("distro-upgrade\t%s\t%s\t%s" % (dtype, name, summary), file=sys.stdout)
        sys.stdout.flush()

    def status(self, state):
        '''
        send 'status' signal
        @param state: STATUS_DOWNLOAD, STATUS_INSTALL, STATUS_UPDATE, STATUS_REMOVE, STATUS_WAIT
        '''
        print("status\t%s" % (state))
        sys.stdout.flush()

    def repo_detail(self, repoid, name, state):
        '''
        send 'repo-detail' signal
        @param repoid: The repo id tag
        @param state: false is repo is disabled else true.
        '''
        print("repo-detail\t%s\t%s\t%s" % (repoid, name, _bool_to_string(state)), file=sys.stdout)
        sys.stdout.flush()

    def data(self, data):
        '''
        send 'data' signal:
        @param data:  The current worked on package
        '''
        print("data\t%s" % (data))
        sys.stdout.flush()

    def details(self, package_id, package_license, group, desc, url, bytes):
        '''
        Send 'details' signal
        @param package_id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
        @param package_license: The license of the package
        @param group: The enumerated group
        @param desc: The multi line package description
        @param url: The upstream project homepage
        @param bytes: The size of the package, in bytes
        '''
        print("details\t%s\t%s\t%s\t%s\t%s\t%ld" % (package_id, package_license, group, desc, url, bytes), file=sys.stdout)
        sys.stdout.flush()

    def files(self, package_id, file_list):
        '''
        Send 'files' signal
        @param file_list: List of the files in the package, separated by ';'
        '''
        print("files\t%s\t%s" % (package_id, file_list), file=sys.stdout)
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
        print("category\t%s\t%s\t%s\t%s\t%s" % (parent_id, cat_id, name, summary, icon), file=sys.stdout)
        sys.stdout.flush()

    def finished(self):
        '''
        Send 'finished' signal
        '''
        print("finished", file=sys.stdout)
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
        print("updatedetail\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s" % (package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text, changelog, state, issued, updated), file=sys.stdout)
        sys.stdout.flush()

    def require_restart(self, restart_type, details):
        '''
        Send 'requirerestart' signal
        @param restart_type: RESTART_SYSTEM, RESTART_APPLICATION, RESTART_SESSION
        @param details: Optional details about the restart
        '''
        print("requirerestart\t%s\t%s" % (restart_type, details))
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
        print("allow-cancel\t%s" % (data))
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
        print("repo-signature-required\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s" % (
            package_id, repo_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, sig_type
            ))
        sys.stdout.flush()

    def eula_required(self, eula_id, package_id, vendor_name, license_agreement):
        '''
        send 'eula-required' signal:
        @param eula_id:         Id of the EULA
        @param package_id:      Id of the package needing a signature
        @param vendor_name:     Name of the vendor that wrote the EULA
        @param license_agreement: The license text
        '''
        print("eula-required\t%s\t%s\t%s\t%s" % (
            eula_id, package_id, vendor_name, license_agreement
            ))
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

    def get_depends(self, filters, package_ids, recursive):
        '''
        Implement the {backend}-get-depends functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def get_packages(self, filters):
        '''
        Implement the {backend}-get-packages functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def get_requires(self, filters, package_ids, recursive):
        '''
        Implement the {backend}-get-requires functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def what_provides(self, filters, provides_type, values):
        '''
        Implement the {backend}-what-provides functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def update_system(self, only_trusted):
        '''
        Implement the {backend}-update-system functionality
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

    def install_packages(self, only_trusted, package_ids):
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

    def install_files(self, only_trusted, inst_files):
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

    def remove_packages(self, allowdep, autoremove, package_ids):
        '''
        Implement the {backend}-remove functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def update_packages(self, only_trusted, package_ids):
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

    def simulate_install_files (self, inst_files):
        '''
        Implement the {backend}-simulate-install-files functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def simulate_install_packages(self, package_ids):
        '''
        Implement the {backend}-simulate-install-packages functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def simulate_remove_packages(self, package_ids):
        '''
        Implement the {backend}-simulate-remove-packages functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

    def simulate_update_packages(self, package_ids):
        '''
        Implement the {backend}-simulate-update-packages functionality
        Needed to be implemented in a sub class
        '''
        self.error(ERROR_NOT_SUPPORTED, "This function is not implemented in this backend", exit=False)

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
            package_ids = args[1].split(PACKAGE_IDS_DELIM)
            self.download_packages(directory, package_ids)
            self.finished()
        elif cmd == 'get-depends':
            filters = args[0].split(';')
            package_ids = args[1].split(PACKAGE_IDS_DELIM)
            recursive = _text_to_bool(args[2])
            self.get_depends(filters, package_ids, recursive)
            self.finished()
        elif cmd == 'get-details':
            package_ids = args[0].split(PACKAGE_IDS_DELIM)
            self.get_details(package_ids)
            self.finished()
        elif cmd == 'get-files':
            package_ids = args[0].split(PACKAGE_IDS_DELIM)
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
        elif cmd == 'get-requires':
            filters = args[0].split(';')
            package_ids = args[1].split(PACKAGE_IDS_DELIM)
            recursive = _text_to_bool(args[2])
            self.get_requires(filters, package_ids, recursive)
            self.finished()
        elif cmd == 'get-update-detail':
            package_ids = args[0].split(PACKAGE_IDS_DELIM)
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
            only_trusted = _text_to_bool(args[0])
            files_to_inst = args[1].split(FILENAME_DELIM)
            self.install_files(only_trusted, files_to_inst)
            self.finished()
        elif cmd == 'install-packages':
            only_trusted = _text_to_bool(args[0])
            package_ids = args[1].split(PACKAGE_IDS_DELIM)
            self.install_packages(only_trusted, package_ids)
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
            allowdeps = _text_to_bool(args[0])
            autoremove = _text_to_bool(args[1])
            package_ids = args[2].split(PACKAGE_IDS_DELIM)
            self.remove_packages(allowdeps, autoremove, package_ids)
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
            package_ids = args[1].split(PACKAGE_IDS_DELIM)
            self.resolve(filters, package_ids)
            self.finished()
        elif cmd == 'search-details':
            filters = args[0].split(';')
            values = _to_unicode(args[1]).split(PACKAGE_IDS_DELIM)
            self.search_details(filters, values)
            self.finished()
        elif cmd == 'search-file':
            filters = args[0].split(';')
            values = args[1].split(PACKAGE_IDS_DELIM)
            self.search_file(filters, values)
            self.finished()
        elif cmd == 'search-group':
            filters = args[0].split(';')
            values = args[1].split(PACKAGE_IDS_DELIM)
            self.search_group(filters, values)
            self.finished()
        elif cmd == 'search-name':
            filters = args[0].split(';')
            values = _to_unicode(args[1]).split(PACKAGE_IDS_DELIM)
            self.search_name(filters, values)
            self.finished()
        elif cmd == 'signature-install':
            package = args[0]
            self.repo_signature_install(package)
            self.finished()
        elif cmd == 'update-packages':
            only_trusted = _text_to_bool(args[0])
            package_ids = args[1].split(PACKAGE_IDS_DELIM)
            self.update_packages(only_trusted, package_ids)
            self.finished()
        elif cmd == 'update-system':
            only_trusted = _text_to_bool(args[0])
            self.update_system(only_trusted)
            self.finished()
        elif cmd == 'what-provides':
            filters = args[0].split(';')
            provides_type = args[1]
            values = _to_unicode(args[2]).split(PACKAGE_IDS_DELIM)
            self.what_provides(filters, provides_type, values)
            self.finished()
        elif cmd == 'set-locale':
            code = args[0]
            self.set_locale(code)
            self.finished()
        elif cmd == 'get-categories':
            self.get_categories()
            self.finished()
        elif cmd == 'simulate-install-files':
            files_to_inst = args[0].split(FILENAME_DELIM)
            self.simulate_install_files(files_to_inst)
            self.finished()
        elif cmd == 'simulate-install-packages':
            package_ids = args[0].split(PACKAGE_IDS_DELIM)
            self.simulate_install_packages(package_ids)
            self.finished()
        elif cmd == 'simulate-remove-packages':
            package_ids = args[0].split(PACKAGE_IDS_DELIM)
            self.simulate_remove_packages(package_ids)
            self.finished()
        elif cmd == 'simulate-update-packages':
            package_ids = args[0].split(PACKAGE_IDS_DELIM)
            self.simulate_update_packages(package_ids)
            self.finished()
        elif cmd == 'upgrade-system':
            self.upgrade_system(args[0])
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
    if not isinstance(text, unicode):
        text = unicode(text, encoding, errors='replace')
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

