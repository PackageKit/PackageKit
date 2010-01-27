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
This is the os.system implementation of MediaManager module for dealing with removable media
This module is inteded to be a proof of concept and it is not supposed to be used in final release

it's used like this

    from yumMediaManagerOS import MediaManagerOS as MediaManager
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

import re,sys, os, os.path
from glob import glob
from tempfile import mkdtemp
from yumMediaManager import MediaManager,MediaDevice
class MediaDeviceOS(MediaDevice):
    """
    You should just use acquire() to get the mount point (the implementation is
    supposed to be smart enough to return mount point when it's already mounted)
    You don't need to manually call release(), just destruct MediaDevice object
    and the implementation should do that if needed.
    """
    __ch_re=re.compile(r'\\(0\d\d)')
    def __init__(self,media_id):
        """
        media_id argument is the implementation-specific id
        in our case it's a the device file in /dev/
        """
        self._unmount_needed = False
        self._unlock_needed = False
        self.__d = media_id

    def is_removable(self):
        return True

    def is_mounted(self):
        return self.get_mount_point()!=None

    def is_locked(self):
        # FIXME: there is no locks in OS
        return False

    def get_mount_point(self):
        """
        return the mount point or None if not mounted
        """
        l=map(lambda i: i.split(), open('/proc/mounts','rt').readlines())
        d=filter(lambda i: len(i)==6 and i[0]==self.__d, l)
        if not d: return None
        mnt=self.__ch_re.sub(lambda m: chr(int(m.group(1),8)),d[0][1])
        if not os.path.ismount(mnt): return None
        return mnt

    def lock(self):
        """
        return True if lock is successfully acquired.
        """
        # FIXME: there is no locks in OS
        return False

    def unlock(self):
        """
        return True if it was able to release the lock successfully.
        """
        # FIXME: there is no locks in OS
        return False
    
    def mount(self):
        """
        mount the device and return the mount point.
        If it's already mounted, just return the mount point.
        """
        mnt=self.get_mount_point()
        if mnt!=None: return mnt
        # do the actual mounting
        mnt=mkdtemp(prefix='MediaRepo')
        r=os.system('mount -t udf,iso9660 "%s" "%s"' % (self.__d, mnt))
        if r:
            try: os.rmdir(mnt)
            except OSError: pass
            return None
        self._unmount_needed|=bool(r==0)
        # return Mount point
        return mnt

    def unmount(self):
        """
        unmount the device and return True.
        """
        mnt=self.get_mount_point()
        if mnt==None:
            return True
        r=os.system('umount "%s"' % (self.__d))
        if os.path.isdir(mnt):
            try: os.rmdir(mnt)
            except OSError: pass
        return bool(r==0)

class MediaManagerOS(MediaManager):
    """Just iterate over an instance of this class to get MediaDevice objects"""
    def __init__(self):
        pass

    def __iter__(self):
        l=glob('/dev/sr[0-9]*')
        for d in l:
            o=MediaDeviceOS(d)
            yield o;

