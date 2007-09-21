# PackageKit python interface
#
# Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License v2 as published by
# the Free Software Foundation.

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

class PackageKitJobFailure(PackageKitException):
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
				raise PackageKitJobFailure
			else:
				return jid
		return wrapper

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
			self.Finish()
		elif kwargs['member'] == "PercentageChanged":
			progress = float(args[1])+(progress%1.0)
			self.Percentage(progress)
		elif kwargs['member'] == "SubPercentageChanged":
			progress = (float(args[1])/100.0)+int(progress)
			self.Percentage(progress)
		elif kwargs['member'] == "JobStatusChanged":
			self.JobStatus(args[1])
		elif kwargs['member'] == "Package":
			self.Package(args[2],args[3])
		elif kwargs['member'] in ["NoPercentageUpdates","TransactionListChanged"]:
			pass
		else:
			print "Caught signal %s"% kwargs['member']
			for arg in args:
				print "		" + str(arg)
	
	def Percentage(self,value):
		pass
	
	def JobStatus(self,string):
		pass
	
	def Finish(self):
		pass
	
	@dbusException
	@job_id
	def SearchName(self,pattern,filter="none"):
		return self.pk_iface.SearchName(filter,pattern)

	@dbusException
	@job_id
	def GetDescription(package_id):
		return pk_iface.GetDescription(package_id)

	@dbusException
	@job_id
	def RefreshCache(force=False):
		return pk_iface.RefreshCache(force)

# hack to avoid exporting them
#del job_id
del dbusException
