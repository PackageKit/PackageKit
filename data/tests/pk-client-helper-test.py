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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Copyright (C) 2010 Richard Hughes <richard@hughsie.com>

import sys

def main():
    while True:
        try:
            line = sys.stdin.readline().strip('\n')
        except IOError, e:
            print "could not read from stdin: %s", str(e)
            break
        except KeyboardInterrupt, e:
            print "process was killed by ctrl-c", str(e)
            break;
        if not line or line == 'exit':
            break
        if line == 'ping':
            sys.stdout.write ('pong\n')
            sys.stdout.flush()
        else:
            sys.stdout.write ("you said to me: %s\n" % line)
            sys.stdout.flush()

if __name__ == "__main__":
    main()
