#!/usr/bin/python

import yum
import sys

my = yum.YumBase()
my.doConfigSetup()

searchterms = sys.argv[1:]
searchlist = ['name', 'summary', 'description', 'packager', 'group', 'url']

res = my.searchGenerator(searchlist, searchterms)

count = 1
for (pkg,values) in res:
    if count > 10:
        break
    count+=1
    installed = 'no'
    if my.rpmdb.installed(pkg.name):
        installed = 'yes'
    
    print "%s\t%s\t%s\t%s\t%s\t%s\t%s" % (pkg.name, installed, pkg.summary, pkg.description, pkg.packager, pkg.group, pkg.url)

