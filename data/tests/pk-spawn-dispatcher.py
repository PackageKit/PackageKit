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

while True:
    line = raw_input('')
    if line == 'exit':
        break
    args = line.split(' ')
    print 'command:',args[0]
    print '  arguments:',args[1:]
    if args[0] == 'search-name':
    	print "package\tavailable\tpolkit;0.0.1;i386;data\tPolicyKit daemon"
sys.exit(0)

