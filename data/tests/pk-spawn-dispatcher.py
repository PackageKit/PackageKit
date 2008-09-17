#!/usr/bin/python
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

# Copyright (C) 2008 Tim Lauridsen <timlau@fedoraproject.org>
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>

# Dispacher script to run pk commands from stdin

import sys
from packagekit.backend import *

class PackageKitYumBackend(PackageKitBaseBackend):
    def __init__(self,args,lock=True):
        PackageKitBaseBackend.__init__(self,args)

    def search_name(self,filters,key):
        # check we escape spaces properly
        if key == 'power manager':
            self.package("polkit;0.0.1;i386;data",INFO_AVAILABLE,"PolicyKit daemon")

def main():
    backend = PackageKitYumBackend('',lock=True)
    backend.dispatcher(sys.argv[1:])

if __name__ == "__main__":
    main()

