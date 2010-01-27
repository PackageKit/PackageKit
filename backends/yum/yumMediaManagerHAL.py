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

# Copyright (C) 2009
#    Muayyad Saleh Alsadi <alsadi@ojuba.org>

"""
This is the HAL implementation of MediaManager module for dealing with removable media
This module is inteded to be a proof of concept and it is not supposed to be used in final release

it's used like this

    from yumMediaManagerHAL import MediaManagerHAL as MediaManager
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

# one might use dbus-send command line tool to debug hal, like this:
# dbus-send --system --print-reply --dest = org.freedesktop.Hal /org/freedesktop/Hal/Manager org.freedesktop.Hal.Manager.FindDeviceByCapability string:storage
# dbus-send --system --print-reply --dest = org.freedesktop.Hal /org/freedesktop/Hal/Manager org.freedesktop.Hal.Manager.FindDeviceByCapability string:storage.cdrom | less
# dbus-send --system --print-reply --dest = org.freedesktop.Hal /org/freedesktop/Hal/devices/storage_model_DVDRAM_GH20NS10 org.freedesktop.Hal.Device.GetProperty string:storage.removable
# dbus-send --system --print-reply --dest = org.freedesktop.Hal /org/freedesktop/Hal/Manager org.freedesktop.Hal.Manager.FindDeviceByCapability string:volume
# dbus-send --system --print-reply --dest = org.freedesktop.Hal /org/freedesktop/Hal/Manager org.freedesktop.Hal.Manager.FindDeviceByCapability string:volume.disc

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from yumMediaManager import MediaManager, MediaDevice
dbus_loop = DBusGMainLoop(set_as_default = True)
bus = dbus.SystemBus()
interface = 'org.freedesktop.Hal.Device'

class MediaDeviceHAL(MediaDevice):
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
        self.__dev = bus.get_object("org.freedesktop.Hal", media_id)
        storage_uid = self.__dev.GetPropertyString('block.storage_device', dbus_interface = interface)
        self.__storage = bus.get_object("org.freedesktop.Hal", storage_uid)

    def is_removable(self):
        return self.__storage.GetPropertyString('storage.removable', dbus_interface = interface)

    def is_mounted(self):
        return self.__dev.GetPropertyString('volume.is_mounted', dbus_interface = interface)

    def is_locked(self):
        return self.__dev.IsLockedByOthers(interface, dbus_interface = interface)

    def get_mount_point(self):
        """
        return the mount point or None if not mounted
        """
        if not self.is_mounted():
            return None
        return self.__dev.GetPropertyString('volume.mount_point', dbus_interface = interface) or None

    def lock(self):
        """
        return True if lock is successfully acquired.
        """
        # FIXME: it does not work, it returns None instead of True
        r = self.__dev.Lock('needed by Package Manager', dbus_interface = interface) != False
        # print r
        self._unlock_needed |= bool(r)
        return r

    def unlock(self):
        """
        return True if it was able to release the lock successfully.
        """
        try:
            return self.__dev.Unlock(dbus_interface = interface) != False
        except dbus.exceptions.DBusException:
            return False

    # two internal methods needed by mount()
    def __get_label(self):
        return self.__dev.GetPropertyString('volume.label', dbus_interface = interface)

    def __get_fstype(self):
        return self.__dev.GetPropertyString('volume.fstype', dbus_interface = interface)

    def mount(self):
        """
        mount the device and return the mount point.
        If it's already mounted, just return the mount point.
        """
        if self.is_mounted():
            return self.get_mount_point()
        
        try:
            r = self.__dev.Mount(self.__get_label(), self.__get_fstype(), dbus.Array(dbus.String()), dbus_interface = 'org.freedesktop.Hal.Device.Volume')
        except dbus.exceptions.DBusException:
            return None

        if r != 0:
            return None

        self._unmount_needed = True
        return self.get_mount_point() # return Mount point

    def unmount(self):
        """
        unmount the device and return True.
        """
        try:
            if not self.is_mounted():
                return True
            r = self.__dev.Unmount(dbus.Array(dbus.String()), dbus_interface = 'org.freedesktop.Hal.Device.Volume')
        except dbus.exceptions.DBusException:
            return False
        return r == 0

class MediaManagerHAL(MediaManager):
    """Just iterate over an instance of this class to get MediaDevice objects"""
    def __init__(self):
        self.__dev = bus.get_object("org.freedesktop.Hal", "/org/freedesktop/Hal/Manager")

    def __close_tray_and_be_ready(self):
        for udi in self.__dev.FindDeviceByCapability('storage.cdrom', dbus_interface = 'org.freedesktop.Hal.Manager'):
            try:
                dev = bus.get_object("org.freedesktop.Hal", udi)
                dev.CloseTray(dbus.Array(dbus.String()), dbus_interface = 'org.freedesktop.Hal.Device.Storage')
            except dbus.exceptions.DBusException:
                continue

    def __iter__(self):
        self.__close_tray_and_be_ready()
        # use volume.disc to restrict that to optical discs
        for i in self.__dev.FindDeviceByCapability('volume', dbus_interface = 'org.freedesktop.Hal.Manager'):
            o = MediaDeviceHAL(i)
            if o.is_removable():
                yield o

