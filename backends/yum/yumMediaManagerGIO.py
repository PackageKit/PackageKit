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
This is the GIO implementation of MediaManager module for dealing with removable media

it's used like this

    from yumMediaManagerGIO import MediaManagerGIO as MediaManager
    manager=MediaManager()
    media,found=None,False
    for media in manager:
        mnt=media.acquire() # get mount point, mount and lock if needed
        found=is_it_the_needed_media(mnt)
        if found: break
    if found: copy_files ..etc.
    if media: del media

NOTE: releasing (unmounting and unlocking) is done when media is destructed
"""
import gio
import glib
import os.path

from yumMediaManager import MediaManager,MediaDevice

class MediaDeviceGIO(MediaDevice):
    """
    You should just use acquire() to get the mount point (the implementation is
    supposed to be smart enough to return mount point when it's already mounted)
    You don't need to manually call release(), just destruct MediaDevice object
    and the implementation should do that if needed.
    """
    def __init__(self,media_id):
        """
        media_id argument is the implementation-specific id
        in our case it's a tuble of device and volume in GIO,
        the tuble is provided by MediaManager.
        """
        self._unmount_needed = False
        self._unlock_needed = False
        self.__d = media_id[0]
        self.__v = media_id[1]
        self.__loop=glib.MainLoop()

    def is_removable(self):
        return self.__d.is_media_removable()

    def is_mounted(self):
        m=self.__v.get_mount()
        return bool(m and os.path.ismount(m.get_root().get_path()))

    def is_locked(self):
        # FIXME: there is no locks in GIO
        return False

    def get_mount_point(self):
        """
        return the mount point or None if not mounted
        """
        if not self.is_mounted(): return None
        return self.__v.get_mount().get_root().get_path()

    def lock(self):
        """
        return True if lock is successfully acquired.
        """
        # FIXME: there is no locks in GIO
        return False

    def unlock(self):
        """
        return True if it was able to release the lock successfully.
        """
        # FIXME: there is no locks in GIO
        return False
    
    def __mount_cb(self,src,res):
        "Internal method used for mounting"
        self.__mount_r=src.mount_finish (res)
        self.__loop.quit()
    def mount(self):
        """
        mount the device and return the mount point.
        If it's already mounted, just return the mount point.
        """
        if self.is_mounted():
            return self.get_mount_point()
        # do the actual mounting
        self.__mount_r=False
        self.__v.mount(gio.MountOperation(),self.__mount_cb)
        self.__loop.run()
        if not self.__mount_r: return None
        self._unmount_needed|=self.__mount_r
        # return Mount point
        return self.get_mount_point()

    def __unmount_cb(self,src,res):
        "Internal method used for unmounting"
        self.__unmount_r=src.unmount_finish (res)
        self.__loop.quit()

    def unmount(self):
        """
        unmount the device and return True.
        """
        m=self.__v.get_mount()
        if not m or not self.is_mounted():
            return True
        self.__unmount_r=False
        m.unmount(self.__unmount_cb)
        self.__loop.run()
        return self.__unmount_r

class MediaManagerGIO(MediaManager):
    """Just iterate over an instance of this class to get MediaDevice objects"""
    def __init__(self):
        self.vm=gio.volume_monitor_get()

    def __iter__(self):
        l=filter(lambda d: d.is_media_removable(),self.vm.get_connected_drives())
        l2=[]
        # loop over devices with media
        for d in l:
             if not d.has_media():
                # skip devices without media
                l2.append(d)
                continue
             # a device could contain many volumes eg. a flash with two partitions
             for v in d.get_volumes():
                 o=MediaDeviceGIO((d,v))
                 yield o;
        # loop over devices without media (ie. need to close tray and mount)
        # FIXME: there is no way in GIO to close tray
        #for i in l2:
        #    pass

