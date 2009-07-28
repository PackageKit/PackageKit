#!/usr/bin/python
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
This is a module for dealing with removable media
NOTE: releasing (unmounting and unlocking) is done when media is destructed
"""

class MediaDevice(object):
    """
    You should just use acquire() to get the mount point (the implementation is
    supposed to be smart enough to return mount point when it's already mounted)
    You don't need to manually call release(), just destruct MediaDevice object
    and the implementation should do that if needed.
    """
    def __init__(self, media_id):
        """
        media_id argument is the implementation-specific id, provided by MediaManager.
        """
        self._unmount_needed = False
        self._unlocked_needed = False
        raise NotImplemented

    def __del__(self):
        if self._unmount_needed:
            self.unmount()
        if self._unlocked_needed:
            self.unlock()

    def is_removable(self):
        raise NotImplemented

    def is_mounted(self):
        raise NotImplemented

    def is_locked(self):
        raise NotImplemented

    def get_mount_point(self):
        raise NotImplemented

    def lock(self):
        raise NotImplemented

    def unlock(self):
        raise NotImplemented

    def mount(self):
        raise NotImplemented

    def unmount(self):
        raise NotImplemented

    def acquire(self):
        raise NotImplemented

    def release(self):
        raise NotImplemented

class MediaManager (object):
    """Just iterate over an instance of this class to get MediaDevice objects"""
    def __init__(self):
        raise NotImplemented

    def __iter__(self):
        raise NotImplemented

