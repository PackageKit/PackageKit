#!/usr/bin/python
#
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import yum
import sys

my = yum.YumBase()
#my.doConfigSetup()
my.conf.cache = 1

options = sys.argv[1]
searchterms = sys.argv[2]

searchlist = ['name', 'summary', 'description', 'group']
res = my.searchGenerator(searchlist, [searchterms])

count = 1
for (pkg,values) in res:
    if count > 100:
        break
    count+=1 
    installed = 'no'

    # are we installed?
    if my.rpmdb.installed(pkg.name):
        installed = 'yes'

    # do we print to stdout?
    do_print = 0;
    if options == 'installed' and installed == 'yes':
    	do_print = 1
    elif options == 'available' and installed == 'no':
    	do_print = 1
    elif options == 'all':
    	do_print = 1

    # print in correct format
    if do_print == 1:
    	print "%s\t%s\t%s" % (installed, pkg.name, pkg.summary)

