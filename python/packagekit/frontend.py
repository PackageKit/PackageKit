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

# Copyright (C) 2007
#    Tim Lauridsen <timlau@fedoraproject.org>
#    Tom Parker <palfrey@tevp.net>
#    Robin Norwood <rnorwood@redhat.com>

import dbus
from dbus.mainloop.glib import DBusGMainLoop
import gobject
from enums import PackageKitEnum
from pkdbus import PackageKitDbusInterface, dbusException
from pkexceptions import PackageKitException, PackageKitNotStarted
from pkexceptions import PackageKitAccessDenied, PackageKitTransactionFailure
from pkexceptions import PackageKitBackendFailure

class PackageKit(PackageKitDbusInterface):
    def __init__(self):
        PackageKitDbusInterface.__init__(self,
                         'org.freedesktop.PackageKit',
                         'org.freedesktop.PackageKit',
                         '/org/freedesktop/PackageKit')

    def tid(self):
        return self.pk_iface.GetTid()

    def job_id(func):
        def wrapper(*args, **kwargs):
            jid = func(*args, **kwargs)
            if jid == -1:
                raise PackageKitTransactionFailure
            else:
                return jid
        return wrapper

    def run(self):
        self.loop = gobject.MainLoop()
        self.loop.run()

    def catchall_signal_handler(self, *args, **kwargs):
        if kwargs['member'] == "Finished":
            self.loop.quit()
            self.Finished(args[0], args[1], args[2])
        elif kwargs['member'] == "ProgressChanged":
            self.ProgressChanged(args[0], float(args[1])+(float(args[2])/100.0), args[3], args[4])
        elif kwargs['member'] == "StatusChanged":
            self.JobStatus(args[0], args[1])
        elif kwargs['member'] == "Package":
            self.Package(args[0], args[1], args[2], args[3])
        elif kwargs['member'] == "UpdateDetail":
            self.UpdateDetail(args[0], args[1], args[2], args[3], args[4], args[5], args[6])
        elif kwargs['member'] == "Details":
            self.Details(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7])
        elif kwargs['member'] == "ErrorCode":
            self.ErrorCode(args[0], args[1], args[2])
        elif kwargs['member'] == "RequireRestart":
            self.RequireRestart(args[0], args[1], args[2])
        elif kwargs['member'] == "Transaction":
            self.Transaction(args[0], args[1], args[2], args[3], args[4], args[5])
        elif kwargs['member'] in ["TransactionListChanged",
                      "AllowCancel", "JobListChanged", "Locked"]:
            pass
        else:
            print "Caught unhandled signal %s"% kwargs['member']
            print "  args:"
            for arg in args:
                print "        " + str(arg)

# --- PK Signal Handlers ---

    def Finished(self,
            jid,         # Job ID
            status,      # enum - unknown, success, failed, canceled
            running_time  # amount of time transaction has been running in seconds
            ):
        pass

    def ProgressChanged(self,
            jid,       # Job ID
            percent,   # 0.0 - 100.0
            elapsed,     # time
            remaining    # time
            ):
        pass

    def JobStatus(self,
            jid,       # Job ID
            status      # enum - invalid, setup, download, install, update, exit
            ):
        pass

    def Package(self,
            jid,       # Job ID
            value,     # installed=1, not-installed=0 | security=1, normal=0
            package_id,
            package_summary
            ):
        pass

    def UpdateDetail(self,
             jid,       # Job ID
             package_id,
             updates,
             obsoletes,
             url,
             restart_required,
             update_text
             ):
        pass

    def Details(self,
            jid,       # Job ID
            package_id,
            license,
            group,
            detail,
            url,
            size,      # in bytes
            file_list   # separated by ';'
            ):
        pass

    def ErrorCode(self,
            jid,       # Job ID
            error_code, # enumerated - see pk-enum.c in PackageKit source
            details     # non-localized details
            ):
        pass

    def RequireRestart(self,
            jid,       # Job ID
            type,      # enum - system, application, session
            details     # non-localized details
            ):
        pass

    def Transaction(self,
            jid,      # Job ID
            old_jid,  # Old Job ID
            timespec, # Time (2007-09-27T15:29:22Z)
            succeeded, # 1 or 0
            role,     # enum, see task_role in pk-enum.c
            duration   # in seconds
            ):
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
        return self.pk_iface.GetUpdates(self.tid(), filter)

    @dbusException
    @job_id
    def RefreshCache(self, force=False):
        """
        Refreshes the backend's cache.
        """
        return self.pk_iface.RefreshCache(self.tid(), force)

    @dbusException
    @job_id
    def UpdateSystem(self):
        """
        Applies all available updates.
        Asynchronous
        """
        return self.pk_iface.UpdateSystem(self.tid())

    @dbusException
    @job_id
    def Resolve(self, package_name, filter="none"):
        """
        Finds a package with the given name, and gives back a Package that matches that name exactly
        (not yet supported in yum backend, and maybe others)
        """
        return self.pk_iface.Resolve(self.tid(), filter, package_name)

    @dbusException
    @job_id
    def SearchName(self, pattern, filter="none"):
        """
        Searches the 'Name' field for something matching 'pattern'.
        'filter' could be 'installed', a repository name, or 'none'.
        Causes 'Package' signals for each package found.
        """
        return self.pk_iface.SearchName(self.tid(), filter, pattern)

    @dbusException
    @job_id
    def SearchDetails(self, pattern, filter="none"):
        """
        Searches the 'Details' field for something matching 'pattern'.
        'filter' could be 'installed', a repository name, or 'none'.
        Causes 'Package' signals for each package found.
        """
        return self.pk_iface.SearchDetails(self.tid(), filter, pattern)

    @dbusException
    @job_id
    def SearchGroup(self, pattern, filter="none"):
        """
        Lists all packages in groups matching 'pattern'.
        'filter' could be 'installed', a repository name, or 'none'.
        Causes 'Package' signals for each package found.
        """
        return self.pk_iface.SearchGroup(self.tid(), filter, pattern)

    @dbusException
    @job_id
    def SearchFile(self, pattern, filter="none"):
        """
        Lists all packages that provide a file matching 'pattern'.
        'filter' could be 'installed', a repository name, or 'none'.
        Causes 'Package' signals for each package found.
        """
        return self.pk_iface.SearchFile(self.tid(), filter, pattern)

    @dbusException
    @job_id
    def GetDepends(self, package_id, recursive=False):
        """
        Lists package dependancies
        """
        return self.pk_iface.GetDepends(self.tid(), package_id, recursive)

    @dbusException
    @job_id
    def GetRequires(self, package_id, recursive):
        """
        Lists package requires
        """
        return self.pk_iface.GetRequires(self.tid(), package_id, recursive)

    @dbusException
    @job_id
    def GetUpdateDetail(self, package_id):
        """
        More details about an update.
        """
        return self.pk_iface.GetUpdateDetail(self.tid(), package_id)

    @dbusException
    @job_id
    def GetDetails(self, package_id):
        """
        Gets the Details of a given package_id.
        Causes a 'Details' signal.
        """
        return self.pk_iface.GetDetails(self.tid(), package_id)

    @dbusException
    @job_id
    def RemovePackages(self, package_ids, allow_deps=False ):
        """
        Removes a package.
        Asynchronous
        """
        return self.pk_iface.RemovePackages(self.tid(), package_ids, allow_deps)

    @dbusException
    @job_id
    def InstallPackages(self, package_ids):
        """
        Installs a package.
        Asynchronous
        """
        return self.pk_iface.InstallPackages(self.tid(), package_ids)

    @dbusException
    @job_id
    def UpdatePackages(self, package_ids):
        """
        Updates a package.
        Asynchronous
        """
        return self.pk_iface.UpdatePackages(self.tid(), package_ids)

    @dbusException
    @job_id
    def InstallFiles(self, full_paths):
        """
        Installs a package which provides given file?
        Asynchronous
        """
        return self.pk_iface.InstallFiles(self.tid(), full_paths)

    @dbusException
    @job_id
    def ServicePack(self, location, enabled):
        """
        Updates a service pack from a location
        Asynchronous
        """
        return self.pk_iface.ServicePack(self.tid(), location, enabled)

## Do things or query transactions
    @dbusException
    @job_id
    def Cancel(self):
        """
        Might not succeed for all manner or reasons.
        throws NoSuchTransaction
        """
        return self.pk_iface.Cancel(self.tid())

    @dbusException
    @job_id
    def GetStatus(self):
        """
        This is what the transaction is currrently doing, and might change.
        Returns status (query, download, install, exit)
        throws NoSuchTransaction
        """
        return self.pk_iface.GetStatus(self.tid())

    @dbusException
    @job_id
    def GetRole(self):
        """
        This is the master role, i.e. won't change for the lifetime of the transaction
        Returns status (query, download, install, exit) and package_id (package acted upon, or NULL
        throws NoSuchTransaction
        """
        return self.pk_iface.GetRole(self.tid())

    @dbusException
    @job_id
    def GetPercentage(self):
        """
        Returns percentage of transaction complete
        throws NoSuchTransaction
        """
        return self.pk_iface.GetPercentage(self.tid())

    @dbusException
    @job_id
    def GetSubPercentage(self):
        """
        Returns percentage of this part of transaction complete
        throws NoSuchTransaction
        """
        return self.pk_iface.GetSubPercentage(self.tid())

    @dbusException
    @job_id
    def GetPackage(self):
        """
        Returns package being acted upon at this very moment
        throws NoSuchTransaction
        """
        return self.pk_iface.GetPackage(self.tid())

## Get lists of transactions

    @dbusException
    def GetTransactionList(self):
        """
        Returns list of (active) transactions.
        """
        return self.pk_iface.GetTransactionList()

    @dbusException
    @job_id
    def GetOldTransactions(self, number=5):
        """
        Causes Transaction signals for each Old transaction.
        """
        return self.pk_iface.GetOldTransactions(self.tid(), number)

## General methods

    @dbusException
    def GetBackendDetail(self):
        """
        Returns name, author, and version of backend.
        """
        return self.pk_iface.GetBackendDetail()

    @dbusException
    def GetActions(self):
        """
        Returns list of supported actions.
        """
        return self.pk_iface.GetActions()

    @dbusException
    def GetGroups(self):
        """
        Returns list of supported groups.
        """
        return self.pk_iface.GetGroups()

    @dbusException
    def GetFilters(self):
        """
        Returns list of supported filters.
        """
        return self.pk_iface.GetFilters()

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

