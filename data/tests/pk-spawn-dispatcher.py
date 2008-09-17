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

def dispatch(args):
    if args[0] == 'search-name':
        # check we escape spaces properly
        if args[2] == 'power manager':
            print 'package\tavailable\tpolkit;0.0.1;i386;data\tPolicyKit daemon'
            sys.stdout.flush()

def main():
    args = sys.argv[1:]
    dispatch(args)
    while True:
        line = raw_input('')
        if line == 'exit':
            break
        args = line.split('\t')
        dispatch(args)

if __name__ == "__main__":
    main()

