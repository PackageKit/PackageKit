#!/usr/bin/python
'''
The module provides a client to the PackageKit DBus interface. It allows to
perform basic package manipulation tasks in a cross distribution way, e.g.
to search for packages, install packages or codecs.
'''
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
#
# (c) 2008
#    Canonical Ltd.
#    Aidan Skinner <aidan@skinner.me.uk>
#    Martin Pitt <martin.pitt@ubuntu.com>
#    Tim Lauridsen <timlau@fedoraproject.org>
#    Sebastian Heinlein <devel@glatzor.de>

import locale
import os

import dbus
import dbus.mainloop.glib
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
import gobject

from enums import *
from misc import *

__api_version__ = '0.1.2'

class PackageKitError(Exception):
    '''PackageKit error.

    This class mainly wraps a PackageKit "error enum". See
    http://www.packagekit.org/pk-reference.html#introduction-errors for details
    and possible values.
    '''
    def __init__(self, code, details=None):
        self.code = code
        self.details = details

    def __str__(self):
        return "%s: %s" % (self.code, self.details)

class PackageKitTransaction:
    '''
    This class represents a PackageKit transaction. It allows asynchronous and
    synchronous processing
    '''
    def __init__(self, tid, iface):
        self.tid = tid
        self._error_code = None
        self._error_details = None
        self._exit_status = None
        self._allow_cancel = False
        self._method = None
        self._exit_handler = None
        self.messages = []
        self.result = []
        # Connect the signal handlers to the DBus iface
        self._iface = iface
        for sig, cb in [('Finished', self._on_finished),
                        ('ErrorCode', self._on_error),
                        ('StatusChanged', self._on_status),
                        ('AllowCancel', self._on_allow_cancel),
                        ('Package', self._on_package),
                        ('Details', self._on_details),
                        ('Category', self._on_category),
                        ('UpdateDetail', self._on_update_detail),
                        ('DistroUpgrade', self._on_distro_upgrade),
                        ('Message', self._on_message),
                        ('RepoDetail', self._on_repo_detail)]:
            self._iface.connect_to_signal(sig, cb)
        self._main_loop = gobject.MainLoop()

    def connect_to_signal(self, sig, cb):
        '''Connect to a signal of the transaction's DBus interface'''
        return self._iface.connect_to_signal(sig, cb)

    def _on_package(self, i, id, summary):
        '''Callback for Package signal'''
        self.result.append(PackageKitPackage(i, id, summary))

    def _on_distro_upgrade(self, typ, name, summary):
        '''Callback for DistroUpgrade signal'''
        self.result.append(PackageKitDistroUpgrade(typ, name, summary))

    def _on_details(self, id, license, group, detail, url, size):
        '''Callback for Details signal'''
        self.result.append(PackageKitDetails(id, license, group, detail,
                                             url, size))

    def _on_category(self, parent_id, cat_id, name, summary, icon):
        '''Callback for Category signal'''
        self.result.append(PackageKitCategory(parent_id, cat_id, name,
                                              summary, icon))

    def _on_update_detail(self, id, updates, obsoletes, vendor_url,
                          bugzilla_url, cve_url, restart, update_text,
                          changelog, state, issued, updated):
        '''Callback for UpdateDetail signal'''
        self.result.append(PackageKitUpdateDetails(id, updates, obsoletes,
                                                   vendor_url, bugzilla_url,
                                                   cve_url, restart,
                                                   update_text, changelog,
                                                   state, issued, updated))
    def _on_repo_detail(self, id, description, enabled):
        '''Callback for RepoDetail signal'''
        self.result.append(PackageKitRepos(id, description, enabled))

    def _on_files(self, id, files):
        '''Callback for Files signal'''
        self.result.append(PackageKitFiles(id, files))

    def _on_status(self, status):
        '''Callback for StatusChanged signal'''
        self._status = status

    def _on_allow_cancel(self, allow):
        '''Callback for AllowCancel signal'''
        self._allow_cancel = allow

    def _on_error(self, code, details):
        '''Callback for ErrorCode signal'''
        self._error_code = code
        self._error_details = details

    def _on_message(self, code, details):
        '''Callback for Message signal'''
        self.messages.append(PackageKitMessage(code, details))

    def _on_finished(self, exit, runtime):
        '''Callback for Finished signal'''
        self._exit = exit
        self._main_loop.quit()
        if self._exit_handler:
            self._exit_handler(self, exit, runtime)

    def set_method(self, method, *args):
        '''Setup the method of the DBus interface which should be handled'''
        self._method = self._iface.get_dbus_method(method)
        self._args = args

    def run(self):
        '''Start processing the transaction'''
        # avoid blocking the user interface
        context = gobject.main_context_default()
        while context.pending():
            context.iteration()
        polkit_auth_wrapper(self._method, *self._args)
        if not self._exit_handler:
            self._main_loop.run()
            if self._error_code:
                raise PackageKitError(self._error_code, self._error_details)
            return self.result

    def set_locale(self, code):
        '''Set the language to the given locale code'''
        return self._iface.SetHints(['locale=%s' % code])

    def cancel(self):
        '''Cancel the transaction'''
        return self._iface.Cancel()

    def get_status(self):
        '''Get the status of the transaction'''
        return self._status

    def get_progress(self):
        '''Get the progress of the transaction'''
        return self._iface.GetProgress()

    def get_finished_state(self):
        '''Return the finished status'''
        return self._finished_status

    def is_caller_active(self):
        '''
        This method allows us to find if the original caller of the method is
        still connected to the session bus. This is usually an indication that
        the client can handle it's own error handling and EULA callbacks rather
        than another program taking over.
        '''
        return self._iface.IsCallerActive()

    def get_error(self):
        '''Returns the PackageKitError of a failed transaction'''
        if self._error_code:
            return PackageKitError(self._error_code, self._error_details)
        else:
            return None

class PackageKitClient:
    '''PackageKit client wrapper class.

    This exclusively uses synchonous calls. Functions which take a long time
    (install/remove packages) have callbacks for progress feedback.
    '''
    def __init__(self, main_loop=None):
        '''Initialize a PackageKit client.

        If main_loop is None, this sets up its own gobject.MainLoop(),
        otherwise it attaches to the specified one.
        '''
        self.pk_control = None
        self.bus = dbus.SystemBus()
        self._locale = locale.getdefaultlocale()[0]

    def suggest_daemon_quit(self):
        '''Ask the PackageKit daemon to shutdown.'''
        try:
            self.pk_control.SuggestDaemonQuit()
        except (AttributeError, dbus.DBusException), e:
            # not initialized, or daemon timed out
            pass

    def resolve(self, names, filters=FILTER_NONE, exit_handler=None):
        '''Resolve package names'''
        names_list = self._to_list(names)
        return self._run_transaction("Resolve", [filters, names_list],
                                     exit_handler)

    def get_details(self, packages, exit_handler=None):
        '''Get details about the given packages'''
        package_ids = self._to_package_id_list(packages)
        return self._run_transaction("GetDetails", [package_ids],
                                     exit_handler)

    def search_name(self, search, filters=FILTER_NONE, exit_handler=None):
        '''Search for packages by name'''
        return self._run_transaction("SearchName", [filters, search],
                                     exit_handler)

    def search_group(self, search, filters=FILTER_NONE, exit_handler=None):
        '''Search for packages by their group'''
        return self._run_transaction("SearchGroup", [filters, search], 
                                     exit_handler)

    def search_details(self, search, filters=FILTER_NONE, exit_handler=None):
        '''Search for packages by their details'''
        return self._run_transaction("SearchDetails", [filters, search],
                                     exit_handler)

    def search_file(self, search, filters=FILTER_NONE, exit_handler=None):
        '''Search for packages by their files'''
        return self._run_transaction("SearchFiles", [filters, search],
                                     exit_handler)

    def install_packages(self, packages, only_trusted=True, exit_handler=None):
        '''Install the packages of the given package ids'''
        package_ids = self._to_package_id_list(packages)
        return self._run_transaction("InstallPackages",
                                     [only_trusted, package_ids], exit_handler)

    def update_packages(self, packages, exit_handler=None):
        '''Update the packages of the given package ids'''
        package_ids = self._to_package_id_list(packages)
        return self._run_transaction("UpdatePackages", [package_ids], 
                                     exit_handler)

    def remove_packages(self, packages, allow_deps=False, auto_remove=True,
                        exit_handler=None):
        '''Remove the packages of the given package ids'''
        package_ids = self._to_package_id_list(packages)
        return self._run_transaction("RemovePackages",
                                     [package_ids, allow_deps, auto_remove],
                                     exit_handler)

    def refresh_cache(self, force=False, exit_handler=None):
        '''
        Refresh the cache, i.e. download new metadata from a
        remote URL so that package lists are up to date. This action
        may take a few minutes and should be done when the session and
        system are idle.
        '''
        return self._run_transaction("RefreshCache", (force,), exit_handler)

    def get_repo_list(self, filters=FILTER_NONE, exit_handler=None):
        '''Get the repositories'''
        return self._run_transaction("GetRepoList", (filters,), exit_handler)

    def repo_enable(self, repo_id, enabled):
        '''
        Enable the repository specified.
        repo_id is a repository identifier, e.g. fedora-development-debuginfo
        enabled true if enabled, false if disabled
        '''
        return self._run_transaction("RepoEnable", (repo_id, enabled),
                                     exit_handler)

    def get_updates(self, filters=FILTER_NONE, exit_handler=None):
        '''
        This method should return a list of packages that are installed and
        are upgradable.

        It should only return the newest update for each installed package.
        '''
        return self._run_transaction("GetUpdates", [filters], exit_handler)

    def get_categories(self, exit_handler=None):
        '''Return available software categories'''
        return self._run_transaction("GetCategories", [], exit_handler)

    def get_packages(self, filters=FILTER_NONE, exit_handler=None):
        '''Return all packages'''
        return self._run_transaction("GetPackages", [filters], exit_handler)

    def update_system(self, exit_handler=None):
        '''Update the system'''
        return self._run_transaction("UpdateSystem", [], exit_handler)

    def download_packages(self, packages, exit_handler=None):
        '''Download package files'''
        package_ids = self._to_package_id_list(packages)
        return self._run_transaction("DownloadPackages", [package_ids], exit_handler)

    def get_depends(self, packages, filters=FILTER_NONE, recursive=False, 
                    exit_handler=None):
        '''Search for dependencies for packages'''
        package_ids = self._to_package_id_list(packages)
        return self._run_transaction("GetDepends",
                                     [filters, package_ids, recursive],
                                     exit_handler)

    def get_files(self, packages, exit_handler=None):
        '''Get files of the given packages'''
        package_ids = self._to_package_id_list(packages)
        return self._run_transaction("GetFiles", [package_ids], exit_handler)

    def get_requires(self, packages, filters=FILTER_NONE, recursive=False, 
                     exit_handler=None):
        '''Search for requirements for packages'''
        package_ids = self._to_package_id_list(packages)
        return self._run_transaction("GetRequires",
                                     [filters, package_ids, recursive],
                                     exit_handler)

    def get_update_detail(self, packages, exit_handler=None):
        '''Get details for updates'''
        package_ids = self._to_package_id_list(packages)
        return self._run_transaction("GetUpdateDetail", [package_ids], 
                                     exit_handler)

    def get_distro_upgrades(self, exit_handler=None):
        '''Query for later distribution releases'''
        return self._run_transaction("GetDistroUpgrades", [],
                                     exit_handler)

    def install_files(self, only_trusted, files, exit_handler=None):
        '''Install the given local packages'''
        return self._run_transaction("InstallFiles", [only_trusted, files],
                                     exit_handler)

    def install_signature(self, sig_type, key_id, package_id, 
                          exit_handler=None):
        '''Install packages signing keys used to validate packages'''
        return self._run_transaction("InstallSignature",
                                     [sig_type, key_id, package_id],
                                     exit_handler)

    def set_repo_data(self, repo_id, parameter, value, exit_handler=None):
        '''Change custom parameter of a repository'''
        return self._run_transaction("RepoSetData",
                                     [repo_id, parameter, value],
                                     exit_handler)

    def rollback(self, transaction_id, exit_handler=None):
        '''Roll back to a previous transaction'''
        return self._run_transaction("Rollback", [transaction_id], 
                                     exit_handler)

    def what_provides(self, filters, enum, search, exit_handler=None):
        '''Search for packages that provide the supplied attributes'''
        return self._run_transaction("WhatProvides", [filters, enum, search], 
                                     exit_handler)

    def set_locale(self, code):
        '''Set the language of the client'''
        self._locale = code

    def accept_eula(self, eula_id, exit_handler=None):
        '''Accept the given end user licence aggreement'''
        return self._run_transaction("AcceptEula", [eula_id], exit_handler)

    #
    # Internal helper functions
    #
    def _to_list(self, obj):
        '''convert obj to list'''
        if not isinstance(obj, list):
            obj = [obj]
        return obj

    def _to_package_id_list(self, obj):
        '''convert to list of package ids'''
        ids = []
        if not isinstance(obj, list):
            obj = [obj]
        for o in obj:
            if isinstance(o, PackageKitPackage):
                ids.append(o.id)
            elif isinstance(o, str) and len(o.split(";")) == 4:
                ids.append(o)
            else:
                raise Exception("Wrong Type: %s has to be a PackageKit id "
                                "string or a PackageKitPackage instance" % o)
        return ids

    def _run_transaction(self, method_name, args, exit_handler):
        '''Run the given method in a new transaction'''
        try:
            tid = self.pk_control.GetTid()
        except (AttributeError, dbus.DBusException), e:
            if self.pk_control == None or (hasattr(e, '_dbus_error_name') and \
                e._dbus_error_name == 'org.freedesktop.DBus.Error.ServiceUnknown'):
                # first initialization (lazy) or timeout
                self.pk_control = dbus.Interface(self.bus.get_object(
                        'org.freedesktop.PackageKit',
                        '/org/freedesktop/PackageKit',
                    False), 'org.freedesktop.PackageKit')
                tid = self.pk_control.GetTid()
            else:
                raise
        iface = dbus.Interface(self.bus.get_object('org.freedesktop.PackageKit',
                                                   tid, False),
                               'org.freedesktop.PackageKit.Transaction')
        trans = PackageKitTransaction(tid, iface)
        if self._locale:
            trans.set_locale(self._locale)
        trans.set_method(method_name, *args)
        if exit_handler:
            trans._exit_handler = exit_handler
            return trans
        else:
            return trans.run()

#### PolicyKit authentication borrowed wrapper ##
class PermissionDeniedByPolicy(dbus.DBusException):
    _dbus_error_name = 'org.freedesktop.PackageKit.Transaction.RefusedByPolicy'

def polkit_auth_wrapper(fn, *args, **kwargs):
    '''Function call wrapper for PolicyKit authentication.

    Call fn(*args, **kwargs). If it fails with a PermissionDeniedByPolicy
    and the caller can authenticate to get the missing privilege, the PolicyKit
    authentication agent is called, and the function call is attempted again.
    '''
    try:
        return fn(*args, **kwargs)
    except dbus.DBusException, e:
        if e._dbus_error_name == PermissionDeniedByPolicy._dbus_error_name:
            # last words in message are privilege and auth result
            (priv, auth_result) = e.message.split()[-2:]
            if auth_result.startswith('auth_'):
                pk_auth = dbus.SessionBus().get_object(
                    'org.freedesktop.PolicyKit.AuthenticationAgent', '/',
                    'org.gnome.PolicyKit.AuthorizationManager.SingleInstance')
                # TODO: provide xid
                res = pk_auth.ObtainAuthorization(priv, dbus.UInt32(0),
                    dbus.UInt32(os.getpid()), timeout=300)
                print res
                if res:
                    return fn(*args, **kwargs)
            raise PermissionDeniedByPolicy(priv + ' ' + auth_result)
        else:
            raise

if __name__ == '__main__':
    pass
