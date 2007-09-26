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

from types import *
import dbus
from dbus.mainloop.glib import DBusGMainLoop
import gobject

class PackageKitException(Exception):
	def __init__(self):
		Exception.__init__(self)
	
	def __init__(self,e=None):
		Exception.__init__(self)
		if e == None:
			self._pk_name = None
			self._full_str = None
		else:
			if not isinstance(e,dbus.exceptions.DBusException):
				raise Exception,"Can only handle DBusExceptions"
			self._pk_name = str(e.get_dbus_name())
			self._full_str = str(e)
	
	def get_backend_name(self):
		return self._pk_name
	
	def __str__(self):
		if self._full_str!=None:
			return self._full_str
		else:
			return ""

class PackageKitNotStarted(PackageKitException):
	pass

class PackageKitAccessDenied(PackageKitException):
	pass

class PackageKitTransactionFailure(PackageKitException):
	pass

class PackageKitBackendFailure(PackageKitException):
	pass

def dbusException(func):
	def wrapper(*args,**kwargs):
		try:
			return func(*args,**kwargs)
		except dbus.exceptions.DBusException,e:
			if e.get_dbus_name() == "org.freedesktop.DBus.Error.AccessDenied":
				raise PackageKitAccessDenied
			elif e.get_dbus_name() == "org.freedesktop.DBus.Error.NoReply":
				raise PackageKitBackendFailure
			else:
				raise PackageKitException(e)
		except Exception:
			print "wibble"
			raise
	return wrapper


class PackageKit:
	def job_id(func):
		def wrapper(*args,**kwargs):
			jid = func(*args,**kwargs)
			if jid == -1:
				raise PackageKitTransactionFailure
			else:
				return jid
		return wrapper

	def tid(self):
		return self.pk_iface.GetTid()

	def __init__(self):
		DBusGMainLoop(set_as_default=True)
		bus = dbus.SystemBus()
		try:
			pk = bus.get_object('org.freedesktop.PackageKit', '/org/freedesktop/PackageKit')
			self.pk_iface = dbus.Interface(pk, dbus_interface='org.freedesktop.PackageKit')
		except dbus.exceptions.DBusException,e:
			if e.get_dbus_name() == "org.freedesktop.DBus.Error.ServiceUnknown":
				raise PackageKitNotStarted
			else:
				raise PackageKitException(e)

		#self.job = None
		self.progress = 0.0
		bus.add_signal_receiver(self.catchall_signal_handler, interface_keyword='dbus_interface', member_keyword='member',dbus_interface="org.freedesktop.PackageKit")
	
	def run(self):
		self.loop = gobject.MainLoop()
		self.loop.run()

	def catchall_signal_handler(self,*args, **kwargs):
		#if args[0] != self.job and kwargs['member']!="TransactionListChanged":
		#	print "args",args,kwargs
		#	return
		if kwargs['member'] == "Finished":
			self.loop.quit()
			self.Finished(args[0],args[1],args[2])
		elif kwargs['member'] == "PercentageChanged":
			progress = float(args[1])+(progress%1.0)
			self.Percentage(args[0], progress)
		elif kwargs['member'] == "SubPercentageChanged":
			progress = (float(args[1])/100.0)+int(progress)
			self.Percentage(args[0], progress)
		elif kwargs['member'] == "TransactionStatusChanged":
			self.JobStatus(args[0], args[1])
		elif kwargs['member'] == "Package":
			self.Package(args[0],args[1],args[2],args[3])
		elif kwargs['member'] == "UpdateDetail":
			self.UpdateDetail(args[0],args[1],args[2],args[3],args[4],args[5],args[6])
		elif kwargs['member'] == "Description":
			self.Description(args[0],args[1],args[2],args[3],args[4],args[5])
		elif kwargs['member'] == "ErrorCode":
			self.ErrorCode(args[0],args[1],args[2])
		elif kwargs['member'] == "RequireRestart":
			self.RequireRestart(args[0],args[1],args[2])
		elif kwargs['member'] in ["NoPercentageUpdates","TransactionListChanged","Transaction",
					  "AllowInterrupt","JobListChanged"]:
			pass
		else:
			print "Caught unhandled signal %s"% kwargs['member']
			print "  args:"
			for arg in args:
				print "		" + str(arg)

# --- PK Signal Handlers ---

	def Finished(self,
		     jid,          # Job ID
		     status,       # enum - unknown, success, failed, canceled
		     running_time  # amount of time transaction has been running in seconds
		     ):
		pass
	
	def Percentage(self,
		       jid,        # Job ID
		       progress    # 0.0 - 100.0
		       ):
		pass
	
	def JobStatus(self,
		      jid,        # Job ID
		      status      # enum - invalid, setup, download, install, update, exit
		      ):
		pass
	
	def Package(self,
		    jid,        # Job ID
		    value,      # installed=1, not-installed=0 | security=1, normal=0
		    package_id,
		    package_summary
		    ):
		pass

	def UpdateDetail(self,
			 jid,        # Job ID
			 package_id,
			 updates,
			 obsoletes,
			 url,
			 restart_required,
			 update_text
			 ):
		pass

	def Description(self,
			jid,        # Job ID
			package_id,
			license,
			group,
			detail,
			url
			):
		pass

	def ErrorCode(self,
		      jid,        # Job ID
		      error_code, # enumerated - see pk-enum.c in PackageKit source
		      details     # non-localized details
		      ):
		pass
	
	def RequireRestart(self,
			   jid,        # Job ID
			   type,       # enum - system,application,session
			   details     # non-localized details
			   ):
		pass


# --- PK Methods ---
	
	@dbusException
	@job_id
	def SearchName(self,pattern,filter="none"):
		return self.pk_iface.SearchName(self.tid(),filter,pattern)

	@dbusException
	@job_id
	def GetDescription(self,package_id):
		return self.pk_iface.GetDescription(self.tid(),package_id)

	@dbusException
	@job_id
	def RefreshCache(self,force=False):
		return self.pk_iface.RefreshCache(self.tid(),force)

# hack to avoid exporting them
del dbusException

