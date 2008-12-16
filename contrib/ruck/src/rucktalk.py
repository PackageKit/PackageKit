###
### Copyright 2002 Ximian, Inc.
### Copyright 2008 Aidan Skinner <aidan@skinner.me.uk>
###
### This program is free software; you can redistribute it and/or modify
### it under the terms of the GNU General Public License, version 2,
### as published by the Free Software Foundation.
###
### This program is distributed in the hope that it will be useful,
### but WITHOUT ANY WARRANTY; without even the implied warranty of
### MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
### GNU General Public License for more details.
###
### You should have received a copy of the GNU General Public License
### along with this program; if not, write to the Free Software
### Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
###

import sys
import os
import stat

show_messages = 1
show_verbose  = 0
show_warnings = 1
show_errors   = 1
show_debug    = 0
be_terse      = 0

# Check to see if stdout has been redirected to a file.
stdout_is_file = 0
if stat.S_ISREG(os.fstat(sys.stdout.fileno())[stat.ST_MODE]):
    stdout_is_file = 1

def message(str):
    if show_messages:
        print str

esc = ""

def message_status(str):
    if show_messages and not be_terse:
        # If we've redirected to a file, don't print escape characters
        if stdout_is_file:
            print str
        else:
            print esc + "[1G" + str + esc + "[0K",
            sys.stdout.flush()

def message_finished(str, force_output=0):
    if show_messages and (force_output or not be_terse):
        # If we've redirected to a file, don't print escape characters
        if stdout_is_file:
            print str
        else:
            print esc + "[1G" + str + esc + "[0K"

def verbose(str):
    if show_verbose:
        print str

def warning(str):
    if show_warnings:
        print "Warning: " + str

def error(str):
    if show_errors:
        print "ERROR: " + str

def fatal(str):
    error(str)
    sys.exit(1)

def debug(str):
    if show_debug:
        print "DEBUG: " + str

def prompt(str):
    sys.stdout.write(str + " ")
    sys.stdout.flush()
    return raw_input()
