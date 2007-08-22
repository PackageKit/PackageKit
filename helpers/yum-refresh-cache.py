#!/usr/bin/python
#
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
# Copyright (C) 2007 Red Hat Inc, Seth Vidal <skvidal@fedoraproject.org>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import yum
import sys

def progress(pct):
    print >> sys.stderr, "percentage:%i" % (pct)

my = yum.YumBase()
my.doConfigSetup()
pct = 0

progress(pct)
try:
    if len(my.repos.listEnabled()) == 0:
        progress(100)
    	sys.exit(1)

    #work out the slice for each one
    bump = (100/len(my.repos.listEnabled()))/2

    for repo in my.repos.listEnabled():
        repo.metadata_expire = 0
        my.repos.populateSack(which=[repo.id], mdtype='metadata', cacheonly=1)
        pct+=bump
        progress(pct)
        my.repos.populateSack(which=[repo.id], mdtype='filelists', cacheonly=1)
        pct+=bump
        progress(pct)

    #we might have a rounding error
    progress(100)

except yum.Errors.YumBaseError, e:
    print str(e)

