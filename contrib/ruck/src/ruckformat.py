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

import string
import re
import time
import rucktalk

###
### Utility functions.  Not really public.
###

def seconds_to_str(t):

    h = int(t/3600)
    m = int((t % 3600)/60)
    s = t % 60

    if h > 0:
        return "%dh%02dm%0ds" % (h, m, s)
        return "%ds" % t
    elif m > 0:
        return "%dm%02ds" % (m, s)
    else:
        return "%ds" % s

def bytes_to_str(x):

    for fmt in ("%db", "%.2fk", "%.2fM", "%.2fg"):

        if x < 1024:
            return fmt % x

        x = x / 1024.0

    return "!!!"


def pad_row(row, col_sizes):
    return map(string.ljust, row, col_sizes)


def clean_row(row, separator):
    return map(lambda x, sep=separator:string.replace(x,sep,"_"), row)


def max_col_widths(table):
    return reduce(lambda v,w:map(max,v,w),
                  map(lambda x:map(len,x),table))


def stutter(str, N):
    if N <= 0:
        return ""
    return str + stutter(str, N-1)


def linebreak(in_str, width):

    str = string.strip(in_str)

    if not str:
        return []

    if len(str) <= width:
        return [str]

    if width < len(str) and str[width] == " ":
        n = width
    else:
        n = string.rfind(str[0:width], " ")

    lines = []

    if n == -1:
        lines.append(str)
    else:
        lines.append(str[0:n])
        lines = lines + linebreak(str[n+1:], width)

    return lines

## Assemble EVRs into strings

def naevr_to_str(naevr):
    version = ""

    (n,a,e,v,r) = naevr

    if e != '0':
        version = version + e + ":"

    version = version + v

    if r:
        version = version + "-" + r

    version = version + '.' + str(a)

    return version


## Assemble EVRs into abbreviated strings

def naevr_to_abbrev_str(naevr):

    (n,a,e,v,r) = naevr

    if r and string.find(r, "snap") != -1:
        r = re.compile(".*(\d\d\d\d)(\d\d)(\d\d)(\d\d)(\d\d)")
        m = r.match(v) or r.match(r)
        if m:
            return "%s-%s-%s, %s:%s" % \
                   (m.group(1), m.group(2), m.group(3), m.group(4), m.group(5))

    return naevr_to_str(naevr)


## Extract data from a package

def package_to_row(pkg, no_abbrev, key_list):

    row = []

    for key in key_list:
        val = "?"
        if key == "installed":
            # FIXME: show newer versions than are installed as 'U'?
            if (pkg.installed):
                val = 'I'
            else:
                val = 'U'
        elif key == "repo":
            val = pkg.repoid
        elif key == "version":
            val = pkg.ver
        elif key == "name":
            # Trim long names
            val = pkg.name
            if not no_abbrev and len(val) > 25:
                val = val[0:22] + "..."

        row.append(val)

    return row

def package_to_str(pkg, version=True, repo=True):
    pkg_str = pkg.name
    if version:
        if pkg.epoch == '0':
            pkg_str += "-%s-%s" % (pkg.version, pkg.release)
        else:
            pkg_str += "-%s:%s-%s" % (pkg.epoch, pkg.version, pkg.release)
    pkg_str += ".%s" % pkg.arch

    if repo:
        pkg_str += " (%s)" % pkg.repoid

    return pkg_str


def progress_to_str(pc, completed_size, total_size,
                    remaining_sec, elapsed_sec, text=None):

    msg = "%3d%%" % pc

    if remaining_sec > 0:
        hash_max = 10
    else:
        hash_max = 20
    hash_count = int(hash_max * pc / 100)
    hashes = "#" * hash_count + "-" * (hash_max - hash_count)

    msg = msg + " " + hashes

    if completed_size > 0 and total_size > 0:
        cs = bytes_to_str(completed_size)
        ts = bytes_to_str(total_size)
        msg = msg + " (" + cs + "/" + ts + ")"

    if elapsed_sec > 0:
        msg = msg + ", " + seconds_to_str(elapsed_sec) + " elapsed"

        if remaining_sec > 0:
            msg = msg + ", " + seconds_to_str(remaining_sec) + " remain"

            if elapsed_sec > 0 and completed_size > 0:
                rate = completed_size / elapsed_sec
                msg = msg + ", " + bytes_to_str(rate) + "/s"

    if text != None:
        msg += ", " + text

    return msg


###
### Code that actually does something.
###

def separated(table, separator):

    for r in table:
        rucktalk.message(string.join(clean_row(r, separator), separator + " "))


def aligned(table):

    col_sizes = max_col_widths(table)

    for r in table:
        rucktalk.message(string.join(pad_row(r, col_sizes), " "))


def opt_table(table):

    opt_list = []

    for r in table:
        opt = "--" + r[1]
        if r[0]:
            opt = "-" + r[0] + ", " + opt
        if r[2]:
            opt = opt + "=<" + r[2] + ">"

        opt_list.append([opt + "  ", r[3]])

    # By appending [0,0], we insure that this will work even if
    # opt_list is empty (which it never should be)
    max_len = apply(max, map(lambda x:len(x[0]), opt_list) + [0,0])

    for opt, desc_str in opt_list:

        if 79 - max_len > 10:
            desc = linebreak(desc_str, 79 - max_len)
        else:
            desc = [desc_str]

        desc_first = desc.pop(0)
        rucktalk.message(string.ljust(opt, max_len) + desc_first)
        for d in desc:
            rucktalk.message(" " * max_len + d)


def tabular(headers, table):

    def row_to_string(row, col_sizes):
        if rucktalk.be_terse:
            return string.join(row, "|")
        else:
            return string.join(pad_row(row, col_sizes), " | ")

    col_sizes = max_col_widths(table)

    if headers and not rucktalk.be_terse:
        col_sizes = map(max, map(len,headers), col_sizes)

        # print headers
        rucktalk.message(string.join(pad_row(headers, col_sizes), " | "))

        # print head/body separator
        rucktalk.message(string.join (map(lambda x:stutter("-",x), col_sizes), "-+-"))

    # print table body
    for r in table:
        rucktalk.message(row_to_string(r, col_sizes))

def bool_to_str(b):
    if b:
        return 'Yes'
    else:
        return 'No'

def bool_to_short_str(b):
    if b:
        return 'Y'
    else:
        return 'N'
