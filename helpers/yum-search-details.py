#!/usr/bin/python

import yum
import sys

my = yum.YumBase()
my.doConfigSetup()

searchterms = sys.argv[1:]
searchlist = ['name', 'summary', 'description', 'group']

res = my.searchGenerator(searchlist, searchterms)

count = 1
for (pkg,values) in res:
    if count > 10:
        break
    count+=1 
    print "%s\t%s\t%s" % (pkg.name, pkg.summary, pkg.description)

