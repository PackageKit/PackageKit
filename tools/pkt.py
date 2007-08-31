#!/usr/bin/python

from sys import argv,exit
from types import *
import dbus
from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)
import gobject
from optparse import OptionParser

bus = dbus.SystemBus()
try:
	pk = bus.get_object('org.freedesktop.PackageKit', '/org/freedesktop/PackageKit')
	pk_iface = dbus.Interface(pk, dbus_interface='org.freedesktop.PackageKit')
except dbus.exceptions.DBusException,e:
	if e.get_dbus_name() == "org.freedesktop.DBus.Error.ServiceUnknown":
		print "PackageKit doesn't appear to be started. You may need to enable dbus autostart"
		exit(1)
	else:
		raise

job = None
progress = 0.0

def catchall_signal_handler(*args, **kwargs):
	global job,progress
	if args[0] != job and kwargs['member']!="JobListChanged":
		print args,job,kwargs
		raise Exception
	if kwargs['member'] == "Finished":
		loop.quit()
	elif kwargs['member'] == "PercentageChanged":
		progress = float(args[1])+(progress%1.0)
		print "Progress: %.2f%%"%progress
	elif kwargs['member'] == "SubPercentageChanged":
		progress = (float(args[1])/100.0)+int(progress)
		print "Progress: %.2f%%"%progress
	elif kwargs['member'] == "JobStatusChanged":
		print "Job type: %s"%args[1]
	elif kwargs['member'] == "Package":
		print "Package: %s - %s"%(args[2],args[3])
	elif kwargs['member'] in ["NoPercentageUpdates","JobListChanged"]:
		pass
	else:
		print "Caught signal %s"% kwargs['member']
		for arg in args:
			print "		" + str(arg)

bus.add_signal_receiver(catchall_signal_handler, interface_keyword='dbus_interface', member_keyword='member',dbus_interface="org.freedesktop.PackageKit")

def search(*args):
	patt = " ".join(args[0])
	return pk_iface.SearchName("none",patt)

def desc(*args):
	if len(args)!=1 or len(args[0])!=1:
		print "desc only takes single arg"
		return -1
	return pk_iface.GetDescription(args[0][0])

def update(args):
	if len(args)>0 and len(args[0])>0:
		print "update doesn't take args"
		return -1
	return pk_iface.RefreshCache(False)

def usage():
	print "Usage: %s <command> <options>"%argv[0]
	print "Valid commands are:"
	for k in globals().keys():
		if k in ["usage","catchall_signal_handler"]: #ignore
			continue
		g = globals()[k]
		if type(g) == FunctionType:
			print "\t%s"%k
	exit(1)

parser = OptionParser()
(options, args) = parser.parse_args()

if len(args)==0:
	usage()

if not globals().has_key(args[0]):
	print "Don't know operation '%s'"%args[0]
	usage()

try:
	job = globals()[args[0]](args[1:])
except dbus.exceptions.DBusException,e:
	if e.get_dbus_name() == "org.freedesktop.DBus.Error.AccessDenied":
		print "You don't have sufficient permissions to access PackageKit (on the org.freedesktop.PackageKit dbus service)"
		exit(1)
	else:
		raise

if job == -1:
	usage()

loop = gobject.MainLoop()
loop.run()
