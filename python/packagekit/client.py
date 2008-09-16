#!/usr/bin/python
#
# (c) 2008
#    Canonical Ltd.
#    Aidan Skinner <aidan@skinner.me.uk>
#    Martin Pitt <martin.pitt@ubuntu.com>
#    Tim Lauridsen <timlau@fedoraproject.org>
# License: LGPL 2.1 or later
#
# Synchronous PackageKit client wrapper API for Python.

import os
import gobject
import dbus
from enums import *

class PackageKitError(Exception):
    '''PackageKit error.

    This class mainly wraps a PackageKit "error enum". See
    http://www.packagekit.org/pk-reference.html#introduction-errors for details
    and possible values.
    '''
    def __init__(self, error):
        self.error = error

    def __str__(self):
        return self.error

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
        if main_loop is None:
            import dbus.mainloop.glib
            main_loop = gobject.MainLoop()
            dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
        self.main_loop = main_loop

        self.bus = dbus.SystemBus()

    def _wrapCall(self, pk_xn, method, callbacks):
        '''
        Wraps a call which emits Finished and ErrorCode on completion
        '''
        pk_xn.connect_to_signal('Finished', self._h_finished)
        pk_xn.connect_to_signal('ErrorCode', self._h_error)
        for cb in callbacks.keys():
            pk_xn.connect_to_signal(cb, callbacks[cb])

        polkit_auth_wrapper(method)
        self._wait()
        if self._error_enum:
            raise PackageKitError(self._error_enum)


    def _wrapBasicCall(self, pk_xn, method):
        return self._wrapCall(pk_xn, method, {})

    def _wrapPackageCall(self, pk_xn, method):
        '''
        Wraps a call which emits Finished, ErrorCode on completion and
        Package for information returns a list of dicts with
        'installed', 'id' and 'summary' keys
        '''

        result = []
        package_cb = lambda i, id, summary: result.append(
            {'installed' : (i == 'installed'),
             'id': str(id),
             'summary' : self._to_utf8(summary)
             })
        self._wrapCall(pk_xn, method, {'Package' : package_cb})
        return result

    def _wrapDetailsCall(self, pk_xn, method):
        '''
        Wraps a call which emits Finished, ErrorCode on completion and
        Details for information returns a list of dicts with 'id',
        'license', 'group', 'description', 'upstream_url', 'size'.keys
        '''
        result = []
        details_cb = lambda id, license, group, detail, url, size: result.append(
        {"id" : (str(id)),
          "license" : (str(license)),
          "group" : (str(group)),
          "detail" : (self._to_utf8(detail)),
          "url" : str(url),
          "size" : int(size)
          })

        self._wrapCall(pk_xn, method, {'Details' : details_cb})
        return result

    def _wrapUpdateDetailsCall(self, pk_xn, method):
        '''
        Wraps a call which emits Finished, ErrorCode on completion and
        Details for information returns a list of dicts with 'id',
        'license', 'group', 'description', 'upstream_url', 'size'.keys
        '''
        result = []
        details_cb =  lambda id, updates, obsoletes, vendor_url, bugzilla_url, \
                             cve_url, restart, update_text, changelog, state, \
                             issued, updated: result.append(
        {"id" : id,
         "updates"      : updates,
         "obsoletes"    : obsoletes,
         "vendor_url"   : vendor_url,
         "bugzilla_url" : bugzilla_url,
         "cve_url"      : cve_url,
         "restart"      : restart,
         "update_text"  : update_text,
         "changelog"    : changelog,
         "state"        : state,
         "issued"       : issued,
         "updated"      : updated})
        self._wrapCall(pk_xn, method, {'UpdateDetail' : details_cb})
        return result

    def _wrapReposCall(self, pk_xn, method):
        '''
        Wraps a call which emits Finished, ErrorCode and RepoDetail
        for information returns a list of dicts with 'id',
        'description', 'enabled' keys
        '''
        result = []
        repo_cb = lambda id, description, enabled: result.append
        ({'id' : str(id),
          'desc' : self._to_utf8(description),
          'enabled' : enabled})
        self._wrapCall(pk_xn, method, {'RepoDetail' : repo_cb})
        return result


    def SuggestDaemonQuit(self):
        '''Ask the PackageKit daemon to shutdown.'''

        try:
            self.pk_control.SuggestDaemonQuit()
        except (AttributeError, dbus.DBusException), e:
            # not initialized, or daemon timed out
            pass

    def Resolve(self, filter, package):
        '''
        Resolve a package name to a PackageKit package_id filter and
        package are directly passed to the PackageKit transaction
        D-BUS method Resolve()

        Return Dict with keys of (installed, id, short_description)
        for all matches, where installed is a boolean and id and
        short_description are strings.
        '''
        package = self._to_list(package) # Make sure we have a list
        xn = self._get_xn()
        return self._wrapPackageCall(xn, lambda : xn.Resolve(filter, package))


    def GetDetails(self, package_ids):
        '''
        Get details about a PackageKit package_ids.

        Return dict with keys (id, license, group, description,
        upstream_url, size).
        '''
        package_ids = self._to_list(package_ids) # Make sure we have a list
        xn = self._get_xn()
        return self._wrapDetailsCall(xn, lambda : xn.GetDetails(package_ids))

    def SearchName(self, filter, name):
        '''
        Search a package by name.
        '''
        xn = self._get_xn()
        return self._wrapPackageCall(xn, lambda : xn.SearchName(filter, name))

    def SearchGroup(self, filter, group_id):
        '''
        Search for a group.
        '''
        xn = self._get_xn()
        return self._wrapPackageCall(xn, lambda : xn.SearchGroup(filter, group_id))

    def SearchDetails(self, filter, name):
        '''
        Search a packages details.
        '''
        xn = self._get_xn()
        return self._wrapPackageCall(xn,
                                     lambda : xn.SearchDetails(filter, name))

    def SearchFile(self, filter, search):
        '''
        Search for a file.
        '''
        xn = self._get_xn()
        return self._wrapPackageCall(xn,
                                     lambda : xn.SearchFile(filter, search))

    def InstallPackages(self, package_ids, progress_cb=None):
        '''Install a list of package IDs.

        progress_cb is a function taking arguments (status, percentage,
        subpercentage, elapsed, remaining, allow_cancel). If it returns False,
        the action is cancelled (if allow_cancel == True), otherwise it
        continues.

        On failure this throws a PackageKitError or a DBusException.
        '''
        package_ids = self._to_list(package_ids) # Make sure we have a list
        self._doPackages(package_ids, progress_cb, 'install')

    def UpdatePackages(self, package_ids, progress_cb=None):
        '''UPdate a list of package IDs.

        progress_cb is a function taking arguments (status, percentage,
        subpercentage, elapsed, remaining, allow_cancel). If it returns False,
        the action is cancelled (if allow_cancel == True), otherwise it
        continues.

        On failure this throws a PackageKitError or a DBusException.
        '''
        package_ids = self._to_list(package_ids) # Make sure we have a list
        self._doPackages(package_ids, progress_cb, 'update')

    def RemovePackages(self, package_ids, progress_cb=None, allow_deps=False,
        auto_remove=True):
        '''Remove a list of package IDs.

        progress_cb is a function taking arguments (status, percentage,
        subpercentage, elapsed, remaining, allow_cancel). If it returns False,
        the action is cancelled (if allow_cancel == True), otherwise it
        continues.

        allow_deps and auto_remove are passed to the PackageKit function.

        On failure this throws a PackageKitError or a DBusException.
        '''
        package_ids = self._to_list(package_ids) # Make sure we have a list
        self._doPackages(package_ids, progress_cb, 'remove', allow_deps,
            auto_remove)

    def RefreshCache(self, force=False):
        '''
        Refresh the cache, i.e. download new metadata from a
        remote URL so that package lists are up to date. This action
        may take a few minutes and should be done when the session and
        system are idle.
        '''
        xn = self._get_xn()
        self._wrapBasicCall(xn, lambda : xn.RefreshCache(force))


    def GetRepoList(self, filter=None):
        '''
        Returns the list of repositories used in the system

        filter is a correct filter, e.g. None or 'installed;~devel'

        '''
        if (filter == None):
            filter = 'none'
        xn = self._get_xn()
        return self._wrapReposCall(xn, lambda : xn.GetRepoList(filter))


    def RepoEnable(self, repo_id, enabled):
        '''
        Enables the repository specified.

        repo_id is a repository identifier, e.g. fedora-development-debuginfo

        enabled true if enabled, false if disabled

        '''
        xn = self._get_xn()
        self._wrapBasicCall(xn, lambda : xn.RepoEnable(repo_id, enabled))

    def GetUpdates(self, filter=None):
        '''
        This method should return a list of packages that are installed and
        are upgradable.

        It should only return the newest update for each installed package.
        '''
        xn = self._get_xn()
        if (filter == None):
            filter = 'none'
        return self._wrapPackageCall(xn, lambda : xn.GetUpdates(filter))

    def GetPackages(self, filter=None):
        '''
        This method should return a total list of packages, limmited by the
        filter used
        '''
        xn = self._get_xn()
        if (filter == None):
            filter = FILTER_NONE
        return self._wrapPackageCall(xn, lambda : xn.GetPackages(filter))

    def UpdateSystem(self):
        '''
        This method should return a list of packages that are
        installed and are upgradable.

        It should only return the newest update for each installed package.
        '''
        xn = self._get_xn()
        self._wrapPackageCall(xn, lambda : xn.UpdateSystem())

    def DownloadPackages(self,package_ids):
        raise PackageKitError(ERROR_NOT_SUPPORTED)

    def GetDepends(self,filter,package_ids,recursive=False):
        '''
        Search for dependencies for packages
        '''
        package_ids = self._to_list(package_ids) # Make sure we have a list
        xn = self._get_xn()
        return self._wrapPackageCall(xn,
                                     lambda : xn.GetDepends(filter,package_ids,recursive))

    def GetFiles(self,package_ids):
        raise PackageKitError(ERROR_NOT_SUPPORTED)

    def GetRepoList(self,filter):
        raise PackageKitError(ERROR_NOT_SUPPORTED)

    def GetRequires(self,filter,package_ids,recursive=False):
        '''
        Search for requirements for packages
        '''
        package_ids = self._to_list(package_ids) # Make sure we have a list
        xn = self._get_xn()
        return self._wrapPackageCall(xn,
                                     lambda : xn.GetRequires(filter,package_ids,recursive))

    def GetUpdateDetail(self,package_ids):
        '''
        Get details for updates
        '''
        package_ids = self._to_list(package_ids) # Make sure we have a list
        xn = self._get_xn()
        return self._wrapUpdateDetailsCall(xn, lambda : xn.GetUpdateDetail(package_ids))

    def GetDistroUpgrades(self):
        raise PackageKitError(ERROR_NOT_SUPPORTED)

    def InstallFiles(self,trusted,files):
        raise PackageKitError(ERROR_NOT_SUPPORTED)

    def InstallSignatures(self,sig_type,key_id,package_id):
        raise PackageKitError(ERROR_NOT_SUPPORTED)

    def RepoEnable(self,repo_id,enabled):
        raise PackageKitError(ERROR_NOT_SUPPORTED)

    def RepoSetData(self,repo_id,parameter,value):
        raise PackageKitError(ERROR_NOT_SUPPORTED)

    def Rollback(self,transaction_id):
        raise PackageKitError(ERROR_NOT_SUPPORTED)

    def WhatProvides(self,provide_type,search):
        '''
        Search for packages that provide the supplied attributes
        '''
        xn = self._get_xn()
        return self._wrapPackageCall(xn,
                                     lambda : xn.WhatProvides(provide_type,search))

    def SetLocale(self,code):
        raise PackageKitError(ERROR_NOT_SUPPORTED)

    def AcceptEula(self,eula_id):
        raise PackageKitError(ERROR_NOT_SUPPORTED)



    #
    # Internal helper functions
    #

    def _to_utf8(self, obj, errors='replace'):
        '''convert 'unicode' to an encoded utf-8 byte string '''
        if isinstance(obj, unicode):
            obj = obj.encode('utf-8', errors)
        return obj

    def _to_list(self, obj, errors='replace'):
        '''convert 'unicode' to an encoded utf-8 byte string '''
        if isinstance(obj, str):
            obj = [obj]
        return obj

    def _wait(self):
        '''Wait until an async PK operation finishes.'''
        self.main_loop.run()

    def _h_status(self, status):
        self._status = status

    def _h_allowcancel(self, allow):
        self._allow_cancel = allow

    def _h_error(self, enum, desc):
        self._error_enum = enum

    def _h_finished(self, status, code):
        self._finished_status = status
        self.main_loop.quit()

    def _h_progress(self, per, subper, el, rem):
        def _cancel(xn):
            try:
                xn.Cancel()
            except dbus.DBusException, e:
                if e._dbus_error_name == 'org.freedesktop.PackageKit.Transaction.CannotCancel':
                    pass
                else:
                    raise

        ret = self._progress_cb(self._status, int(per),
            int(subper), int(el), int(rem), self._allow_cancel)
        if not ret:
            # we get backend timeout exceptions more likely when we call this
            # directly, so delay it a bit
            gobject.timeout_add(10, _cancel, pk_xn)

    def _auth(self):
        policykit = self.bus.get_object(
            'org.freedesktop.PolicyKit.AuthenticationAgent', '/',
            'org.freedesktop.PolicyKit.AuthenticationAgent')
        if(policykit == None):
           print("Error: Could not get PolicyKit D-Bus Interface\n")
        granted = policykit.ObtainAuthorization("org.freedesktop.packagekit.update-system",
                                                (dbus.UInt32)(xid),
                                                (dbus.UInt32)(os.getpid()))

    def _doPackages(self, package_ids, progress_cb, action,
        allow_deps=None, auto_remove=None):
        '''Shared implementation of InstallPackages,UpdatePackages and RemovePackages.'''

        self._status = None
        self._allow_cancel = False

        pk_xn = self._get_xn()
        if progress_cb:
            pk_xn.connect_to_signal('StatusChanged', self._h_status)
            pk_xn.connect_to_signal('AllowCancel', self._h_allowcancel)
            pk_xn.connect_to_signal('ProgressChanged', self._h_progress)
            self._progress_cb = progress_cb
        pk_xn.connect_to_signal('ErrorCode', self._h_error)
        pk_xn.connect_to_signal('Finished', self._h_finished)
        if action == "install":
           polkit_auth_wrapper(lambda : pk_xn.InstallPackages(package_ids))
        elif action == "remove":
            polkit_auth_wrapper(lambda : pk_xn.RemovePackages(package_ids, allow_deps, auto_remove))
        elif action == "update":
            polkit_auth_wrapper(lambda : pk_xn.UpdatePackages(package_ids))
        self._wait()
        if self._error_enum:
            raise PackageKitError(self._error_enum)
        if self._finished_status != 'success':
            print self._finished_status
            raise PackageKitError('internal-error')

    def _get_xn(self):
        '''Create a new PackageKit Transaction object.'''

        self._error_enum = None
        self._finished_status = None
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

        return dbus.Interface(self.bus.get_object('org.freedesktop.PackageKit',
            tid, False), 'org.freedesktop.PackageKit.Transaction')

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
                    'org.freedesktop.PolicyKit.AuthenticationAgent', '/', 'org.gnome.PolicyKit.AuthorizationManager.SingleInstance')

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
