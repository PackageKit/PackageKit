#!/usr/bin/python

from sys import argv

import dbus
from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)
import gobject

bus = dbus.SystemBus()
pk = bus.get_object('org.freedesktop.PackageKit', '/org/freedesktop/PackageKit')
pk_iface = dbus.Interface(pk, dbus_interface='org.freedesktop.PackageKit')

job = None

def catchall_signal_handler(*args, **kwargs):
	global job
	print ("Caught signal (in catchall handler) "
		   + kwargs['dbus_interface'] + "." + kwargs['member'])
	for arg in args:
		print "		" + str(arg)
	if kwargs['member'] == "JobListChanged":
		stuff = pk_iface.GetJobStatus(job)
		print stuff
	elif kwargs['member'] == "Finished":
		if args[0] == job:
			loop.quit()

bus.add_signal_receiver(catchall_signal_handler, interface_keyword='dbus_interface', member_keyword='member')

job = pk_iface.FindPackages(argv[1],0,False,True)
print "job",job
loop = gobject.MainLoop()
loop.run()
