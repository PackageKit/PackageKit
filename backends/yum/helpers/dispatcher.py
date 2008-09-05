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

# Dispacher script to run pk commands from stdin

import sys

from yumBackend import PackageKitYumBackend
backend = PackageKitYumBackend('',lock=True)
while True:
    line = raw_input('')
    if line == 'exit':
        break
    args = line.split(' ')
    backend.dispatch_command(args[0],args[1:])
sys.exit(0)

