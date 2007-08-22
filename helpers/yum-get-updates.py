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

#Yum already knows how to parse the security status, thanks to the yum.update_md module.
#Something similar to this:

#from yum.update_md import UpdateMetadata
#md = UpdateMetadata()
#try:
#  md.add(repo)
#except: pass # No updateinfo.xml.gz in repo
#notice = md.get_notice((name, version, release))
#if notice['type'] == 'security':
#  moo

sys.exit(1)

