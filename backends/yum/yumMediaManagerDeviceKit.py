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

# Copyright (C) 2009
#    Muayyad Saleh Alsadi <alsadi@ojuba.org>

"""
This is the DeviceKit implementation of MediaManager module for dealing with removable media

it's used like this

    from yumMediaManagerDeviceKit import MediaManagerDeviceKit as MediaManager
    manager = MediaManager()
    media, found = None, False
    for media in manager:
        mnt = media.acquire() # get mount point, mount and lock if needed
        found = is_it_the_needed_media(mnt)
        if found: break
    if found: copy_files ..etc.
    if media: del media

NOTE: releasing (unmounting and unlocking) is done when media is destructed
"""

#list:
#dbus-send --system --print-reply --dest=org.freedesktop.DeviceKit.Disks
#/org/freedesktop/DeviceKit/Disks
#org.freedesktop.DeviceKit.Disks.EnumerateDevices
#
#filter:
#dbus-send --system --print-reply --dest=org.freedesktop.DeviceKit.Disks
#/org/freedesktop/DeviceKit/Disks/devices/sr0
#org.freedesktop.DBus.Properties.Get
#string:org.freedesktop.DeviceKit.Disks.Device string:"device-is-removable"
#
#mount:
#dbus-send --system --print-reply --dest=org.freedesktop.DeviceKit.Disks
#/org/freedesktop/DeviceKit/Disks/devices/sr0
#org.freedesktop.DeviceKit.Disks.Device.FilesystemMount string:auto
#array:string:

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from yumMediaManager import MediaManager, MediaDevice
dbus_loop = DBusGMainLoop(set_as_default = True)
bus = dbus.SystemBus()
interface = 'org.freedesktop.DeviceKit.Disks'

# TODO: catch some "except dbus.exceptions.DBusException"

class MediaDeviceDeviceKit(MediaDevice):
    """
    You should just use acquire() to get the mount point (the implementation is
    supposed to be smart enough to return mount point when it's already mounted)
    You don't need to manually call release(), just destruct MediaDevice object
    and the implementation should do that if needed.
    """
    def __init__(self, media_id):
        """
        media_id argument is the implementation-specific id in our case it's udi in hal, 
        it's provided by MediaManager.
        """
        self._unmount_needed = False
        self._unlock_needed = False
        self.__uid = media_id
        self.__dev = bus.get_object(interface, media_id)

    def get_device_property(self, key):
        return self.__dev.Get(interface+'.Device', key, dbus_interface="org.freedesktop.DBus.Properties")

    def is_removable(self):
        return bool(self.get_device_property('device-is-removable'))

    def is_mounted(self):
        return bool(self.get_device_property('device-is-mounted'))
 
    def is_locked(self):
        return False

    def get_mount_point(self):
        """
        return the mount point or None if not mounted
        """
        if not self.is_mounted():
            return None
        l=self.get_device_property('device-mount-paths')
        if l: return str(l[0]) or None
        return None

    def lock(self):
        """
        return True if lock is successfully acquired.
        """
        return False

    def unlock(self):
        """
        return True if it was able to release the lock successfully.
        """
        return False

    def mount(self):
        """
        mount the device and return the mount point.
        If it's already mounted, just return the mount point.
        """
        if self.is_mounted():
            return self.get_mount_point()
        r=None
        try:
            r = str(self.__dev.FilesystemMount('auto', dbus.Array(dbus.String()), dbus_interface = interface+".Device")) or None
        except dbus.exceptions.DBusException:
            pass
        return r

    def unmount(self):
        """
        unmount the device and return True.
        """
        try:
            self.__dev.FilesystemUnmount(dbus.Array(dbus.String()), dbus_interface = interface+".Device")
        except dbus.exceptions.DBusException:
            pass
        return not self.is_mounted()

class MediaManagerDeviceKit(MediaManager):
    """Just iterate over an instance of this class to get MediaDevice objects"""
    def __init__(self):
        self.__dev = bus.get_object(interface, "/org/freedesktop/DeviceKit/Disks")

    def __iter__(self):
        #self.__close_tray_and_be_ready()
        # use volume.disc to restrict that to optical discs
        for i in self.__dev.EnumerateDevices(dbus_interface = interface):
            o = MediaDeviceDeviceKit(i)
            if o.is_removable():
                yield o

