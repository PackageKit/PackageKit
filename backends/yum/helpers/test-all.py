#!/usr/bin/python
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

# Copyright (C) 2007 Tim Lauridsen <timlau@fedoraproject.org>

# test the spawned helpers

import os
import sys
from subprocess import call

failed = []
def test_cmd(cmd): 
    print "RUNNING : %s" % cmd
    if call(cmd, shell=True):
        failed.append("FAILED : %s" % cmd)
       
test_cmd('./refresh-cache.py')  
test_cmd('./resolve.py none yum')  
test_cmd('./search-name.py none yum')  
test_cmd('./search-details.py none "yum;3.2.18-1.fc9;noarch;installed"')  
test_cmd('./search-file.py none "/usr/bin/yum"')  
test_cmd('./search-group.py none "system"')
test_cmd('./get-depends.py none "yum;3.2.18-1.fc9;noarch;installed" no')
test_cmd('./get-requires.py none "yum;3.2.18-1.fc9;noarch;installed" no')
test_cmd('./get-repo-list.py none')  
test_cmd('./get-update-detail.py "yum;3.2.18-1.fc9;noarch;updates"')  
test_cmd('./get-updates.py none')  
test_cmd('./get-packages.py "installed"')  
test_cmd('./get-files.py "yum;3.2.18-1.fc9;noarch;updates"')  
if failed:
    for fail in failed:
        print fail
    print "%i tests failed" % len(failed)
    sys.exit(1)
else:
    print "All tests was ok"
    sys.exit(0)

      
