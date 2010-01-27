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

# Copyright (C) 2007
#    Tim Lauridsen <timlau@fedoraproject.org>
#    Tom Parker <palfrey@tevp.net>
#    Thomas Liu <tliu@redhat.com>
#    Robin Norwood <rnorwood@redhat.com>

import dbus
import os
from dbus.mainloop.glib import DBusGMainLoop
import gobject
from enums import PackageKitEnum
from pkdbus import PackageKitDbusInterface, dbusException
from pkexceptions import PackageKitException, PackageKitNotStarted
from pkexceptions import PackageKitAccessDenied, PackageKitTransactionFailure
from pkexceptions import PackageKitBackendFailure

class PackageKit(PackageKitDbusInterface):
    def __init__(self):
        self.loop = gobject.MainLoop()
        PackageKitDbusInterface.__init__(self,
                         'org.freedesktop.PackageKit',
                         'org.freedesktop.PackageKit',
                         '/org/freedesktop/PackageKit')

    def tid(self):
        return self.pk_iface.GetTid()
    
    def get_iface(self):
        DBusGMainLoop(set_as_default=True)
        interface = 'org.freedesktop.PackageKit.Transaction'
        bus = dbus.SystemBus()
        bus.add_signal_receiver(self.catchall_signal_handler, interface_keyword='dbus_interface', member_keyword='member', dbus_interface=interface)
        return dbus.Interface(bus.get_object('org.freedesktop.PackageKit', self.tid()), interface)

    def job_id(func):
        """
        Decorator for the dbus calls.
        Append async=True to the args if you want the call to be asynchronous.
        """
        def wrapper(*args, **kwargs):
            self = args[0]
            jid = polkit_auth_wrapper(func, *args)
            if jid == -1:
                raise PackageKitTransactionFailure
            elif not 'async' in kwargs.keys() and jid == None:
                self.run()
            else:
                return jid
        return wrapper

    def run(self):
        self.loop.run()

    def catchall_signal_handler(self, *args, **kwargs):
        member = kwargs['member'] 
        if member == "AllowCancel":
            self.AllowCancel(args[0])
        elif member == "CallerActiveChanged":
            self.CallerActiveChanged(args[0]) 
        elif member == "Category":
            self.Category(args[0], args[1], args[2], args[3], args[4])
        elif member == "Details":
            self.Details(args[0], args[1], args[2], args[3], args[4], args[5])
        elif member == "ErrorCode":
            self.ErrorCode(args[0], args[1])
        elif member == "Files":
            self.Files(args[0], args[1])
        elif member == "Finished":
            self.loop.quit()
            self.Finished(args[0], args[1])
        elif member == "Message":
            self.Message(args[0], args[1]) 
        elif member == "Package":
            self.Package(args[0], args[1], args[2])
        elif member == "ProgressChanged":
            self.ProgressChanged(args[0], args[1], args[2], args[3])
        elif member == "RepoDetail":
            self.RepoDetail(args[0], args[1], args[2])
        elif member == "RepoSignatureRequired":
            self.RepoSignatureRequired(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7])
        elif member == "EulaRequired":
            self.EulaRequired(args[0], args[1], args[2], args[3])
        elif member == "MediaChangeRequired":
            self.MediaChangeRequired(args[0], args[1], args[2])
        elif member == "RequireRestart":
            self.RequireRestart(args[0], args[1])
        elif member == "StatusChanged":
            self.StatusChanged(args[0])
        elif member == "Transaction":
            self.Transaction(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7])
        elif member == "UpdateDetail":
            self.UpdateDetail(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11])
        elif member == "DistroUpgrade":
            self.DistroUpgrade(args[0], args[1], args[2])
        elif member in ["Destroy", "Locked", "TransactionListChanged"]:
            pass
        else:
            print "Caught unhandled signal %s"% member
            print "  args:"
            for arg in args:
                print "        " + str(arg)

# --- PK Signal Handlers ---
# See http://www.packagekit.org/gtk-doc/Transaction.html

    def AllowCancel(self,
                    allow_cancel):
        pass

    def CallerActiveChanged(self,
                            is_active):
        pass

    def Category(self,
                 parent_id,
                 cat_id,
                 name,
                 summary,
                 icon):
        pass

    def Details(self,
                package_id,
                license,
                group,
                detail,
                url,
                size):
        pass

    def ErrorCode(self,
                  code,
                  details):
        pass

    def Files(self,
              package_id,
              file_list):
        pass

    def Finished(self,
                 exit,
                 runtime):
        pass

    def Message(self,
                type,
                details):
        pass

    def Package(self,
                info,
                package_id,
                summary):
        pass

    def ProgressChanged(self,
                        percentage,
                        subpercentage,
                        elapsed,
                        remaining):
        pass

    def RepoDetail(self,
                   repo_id,
                   description,
                   enabled):
        pass

    def RepoSignatureRequired(self,
                              package_id,
                              repository_name,
                              key_url,
                              key_userid,
                              key_id,
                              key_fingerprint,
                              key_timestamp,
                              type):
        pass

    def EulaRequired(self,
                     eula_id,
                     package_id,
                     vendor_name,
                     license_agreement):
        pass

    def MediaChangeRequired(self,
                            media_type,
                            media_id,
                            media_text):
        pass

    def RequireRestart(self,
                       type,
                       package_id):
        pass

    def StatusChanged(self,
                      status):
        pass

    def Transaction(self,
                    old_tid,
                    timespec,
                    succeeded,
                    role,
                    duration,
                    data,
                    uid,
                    cmdline):
        pass

    def UpdateDetail(self,
                     package_id,
                     updates,
                     obsoletes,
                     vendor_url,
                     bugzilla_url,
                     cve_url,
                     restart,
                     update_text,
                     changelog,
                     state,
                     issued,
                     updated):
        pass

    def DistroUpgrade(self, 
                      type,
                      name,
                      summary):
        pass

# --- PK Methods ---

## Start a new transaction to do Foo

    @dbusException
    @job_id
    def GetUpdates(self, filter="none"):
        """
        Lists packages which could be updated.
        Causes 'Package' signals for each available package.
        """
        return self.get_iface().GetUpdates(filter)

    @dbusException
    @job_id
    def RefreshCache(self, force=False):
        """
        Refreshes the backend's cache.
        """
        return self.get_iface().RefreshCache(force)

    @dbusException
    @job_id
    def RepoEnable(self, repo, enabled):
        """
        Enable or disable the repository
        """
        return self.get_iface().RepoEnable(repo, enabled)

    @dbusException
    @job_id
    def GetRepoList(self, filter):
        """
        Enable or disable the repository
        """
        return self.get_iface().GetRepoList(filter)
    
    @dbusException
    @job_id
    def UpdateSystem(self):
        """
        Applies all available updates.
        Asynchronous
        """
        return self.get_iface().UpdateSystem()

    @dbusException
    @job_id
    def UpdatePackages(self, package_ids):
        """
        Applies all available updates.
        Asynchronous
        """
        return self.get_iface().UpdateSystem(package_ids)

    @dbusException
    @job_id
    def Resolve(self, package_names, filter="none"):
        """
        Finds a packages with the given names, and gives back a Package that matches those names exactly
        (not yet supported in yum backend, and maybe others)
        """
        return self.get_iface().Resolve(filter, package_names)

    @dbusException
    @job_id
    def SearchName(self, pattern, filter="none"):
        """
        Searches the 'Name' field for something matching 'pattern'.
        'filter' could be 'installed', a repository name, or 'none'.
        Causes 'Package' signals for each package found.
        """
        return self.get_iface().SearchName(filter, pattern)

    @dbusException
    @job_id
    def SearchDetails(self, pattern, filter="none"):
        """
        Searches the 'Details' field for something matching 'pattern'.
        'filter' could be 'installed', a repository name, or 'none'.
        Causes 'Package' signals for each package found.
        """
        return self.get_iface().SearchDetails(filter, pattern)

    @dbusException
    @job_id
    def SearchGroup(self, pattern, filter="none"):
        """
        Lists all packages in groups matching 'pattern'.
        'filter' could be 'installed', a repository name, or 'none'.
        Causes 'Package' signals for each package found.
        """
        return self.get_iface().SearchGroup(filter, pattern)

    @dbusException
    @job_id
    def SearchFile(self, pattern, filter="none"):
        """
        Lists all packages that provide a file matching 'pattern'.
        'filter' could be 'installed', a repository name, or 'none'.
        Causes 'Package' signals for each package found.
        """
        return self.get_iface().SearchFile(filter, pattern)

    @dbusException
    @job_id
    def GetDepends(self, package_ids, filter="none", recursive=False):
        """
        Lists package dependancies
        """
        return self.get_iface().GetDepends(filter, package_ids, recursive)

    @dbusException
    @job_id
    def GetRequires(self, package_ids, filter="none", recursive=False):
        """
        Lists package requires
        """
        return self.get_iface().GetRequires(filter, package_id, recursive)

    @dbusException
    @job_id
    def GetUpdateDetail(self, package_ids):
        """
        More details about an update.
        """
        return self.get_iface().GetUpdateDetail(package_ids)

    @dbusException
    @job_id
    def GetDetails(self, package_ids):
        """
        Gets the Details of given package_ids.
        Causes a 'Details' signal.
        """
        return self.get_iface().GetDetails(package_ids)

    @dbusException
    @job_id
    def RemovePackages(self, package_ids, allow_deps=False, auto_remove=False):
        """
        Removes packages.
        Asynchronous
        """
        return self.get_iface().RemovePackages(package_ids, allow_deps, auto_remove)

    @dbusException
    @job_id
    def InstallPackages(self, package_ids):
        """
        Installs packages.
        Asynchronous
        """
        return self.get_iface().InstallPackages(package_ids)

    @dbusException
    @job_id
    def UpdatePackages(self, package_ids):
        """
        Updates a package.
        Asynchronous
        """
        return self.get_iface().UpdatePackages(package_ids)

    @dbusException
    @job_id
    def InstallFiles(self, full_paths, only_trusted=False):
        """
        Installs a package which provides given file?
        Asynchronous
        """
        return self.get_iface().InstallFiles(only_trusted, full_paths)
    
    @dbusException
    @job_id
    def SetLocale(self, code):
        """
        Set system locale.
        """
        return self.get_iface().SetLocale(code)
    
    @dbusException
    @job_id
    def AcceptEula(self, eula_id):
        """
        This method allows the user to accept an end user licence agreement.
        """
        return self.get_iface().AcceptEula(eula_id)
 
    @dbusException
    @job_id
    def DownloadPackages(self, package_ids):
        """
        This method allows the user to accept an end user licence agreement.
        """
        return self.get_iface().DownloadPackages(package_ids)
    
    @dbusException
    @job_id
    def GetAllowCancel(self):
        """
        Get if cancel is allowed for the transaction 
        """
        return self.get_iface().GetAllowCancel()

    @dbusException
    @job_id
    def GetCategories(self):
        """
        This method returns the collection categories
        """
        return self.get_iface().GetCategories()
    
    @dbusException
    @job_id
    def GetFiles(self, package_ids):
        """
        This method should return the file list of the package_ids
        """
        return self.get_iface().GetFiles(package_ids)

    @dbusException
    @job_id
    def GetPackageLast(self):
        """
        This method emits the package that was last emmitted from the daemon.
        """
        return self.get_iface().GetPackageLast()

    @dbusException
    @job_id
    def GetDistroUpgrades(self):
        """
        This method should return a list of distribution upgrades that are available.
        """
        return self.get_iface().GetDistroUpgrades()

    @dbusException
    @job_id
    def InstallSignature(self, sig_type, key_id, package_id):
        """
        This method allows us to install new security keys.
        """
        return self.get_iface().InstallSignature(sig_type, key_id, package_id)

    @dbusException
    @job_id
    def IsCallerActive(self):
        """
        This method allows us to find if the original caller of the method is still connected to the session bus.
        """
        return self.get_iface().IsCallerActive()

    @dbusException
    @job_id
    def RepoSetData(self, repo_id, parameter, value):
        """
        This method allows arbitary data to be passed to the repository handler.
        """
        return self.get_iface().RepoSetData(repo_id, parameter, value)

    @dbusException
    @job_id
    def Rollback(self, transaction_id):
        """
        This method rolls back the package database to a previous transaction.
        """
        return self.get_iface().Rollback(transaction_id)

    @dbusException
    @job_id
    def WhatProvides(self, filter, type, search):
        """
        This method returns packages that provide the supplied attributes.
        """
        return self.get_iface().WhatProvides(filter, type, search)

## Do things or query transactions
    @dbusException
    @job_id
    def Cancel(self):
        """
        Might not succeed for all manner or reasons.
        throws NoSuchTransaction
        """
        return self.get_iface().Cancel()

    @dbusException
    @job_id
    def GetStatus(self):
        """
        This is what the transaction is currrently doing, and might change.
        Returns status (query, download, install, exit)
        throws NoSuchTransaction
        """
        return self.get_iface().GetStatus()

    @dbusException
    @job_id
    def GetRole(self):
        """
        This is the master role, i.e. won't change for the lifetime of the transaction
        Returns status (query, download, install, exit) and package_id (package acted upon, or NULL
        throws NoSuchTransaction
        """
        return self.get_iface().GetRole()

    @dbusException
    @job_id
    def GetProgress(self):
        """
        Returns progress of transaction
        """
        return self.get_iface().GetSubPercentage()

    @dbusException
    @job_id
    def GetPackages(self, filter="none"):
        """
        Returns packages being acted upon at this very moment
        throws NoSuchTransaction
        """
        return self.get_iface().GetPackage(filter)

## Get lists of transactions
    @dbusException
    @job_id
    def GetOldTransactions(self, number=5):
        """
        Causes Transaction signals for each Old transaction.
        """
        return self.get_iface().GetOldTransactions(number)

class DumpingPackageKit(PackageKit):
    """
    Just like PackageKit(), but prints all signals instead of handling them
    """
    def catchall_signal_handler(self, *args, **kwargs):
        if kwargs['member'] == "Finished":
            self.loop.quit()

        print "Caught signal %s"% kwargs['member']
        print "  args:"
        for arg in args:
            print "        " + str(arg)

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


