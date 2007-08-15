#!/usr/bin/python

import yum
import sys

my = yum.YumBase()
my.doConfigSetup()

searchterms = sys.argv[1:]
searchlist = ['name', 'summary', 'description', 'group']

res = my.searchGenerator(searchlist, searchterms)

for (pkg,values) in res:
    installed = 'no'
    if my.rpmdb.installed(pkg.name):
        installed = 'yes'
    print "%s\t%s\t%s" % (installed, pkg.name, pkg.summary)

