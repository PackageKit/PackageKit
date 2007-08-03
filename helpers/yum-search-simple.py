#!/usr/bin/python

import yum
import sys

my = yum.YumBase()
my.doConfigSetup()

searchterms = sys.argv[1:]
searchlist = ['name', 'summary', 'description', 'group']

res = my.searchGenerator(searchlist, searchterms)

for (pkg,values) in res:
    print "%s\t%s" % (pkg.name, pkg.summary)

