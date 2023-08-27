#!/usr/bin/env python3
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# Copyright (C) 2010 Richard Hughes <richard@hughsie.com>

from signal import *
from time import sleep
from sys import stdout

def process_quit(signum, frame):
    exit()

def main():
    signal(SIGQUIT, process_quit)

    for i in [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]:
        stdout.write("percentage\t%i\n" % (i * 10))
        stdout.flush()
        sleep(0.3)

if __name__ == "__main__":
    main()
