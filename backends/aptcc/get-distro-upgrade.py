#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Provides an apt backend to PackageKit

Copyright (C) 2007 Ali Sabil <ali.sabil@gmail.com>
Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
Copyright (C) 2008-2009 Sebastian Heinlein <glatzor@ubuntu.com>
Copyright (C) 2010 Daniel Nicoletti <dantti85-pk@yahoo.com.br>

Licensed under the GNU General Public License Version 2

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

# TODO get rid of this python file
import sys;
import time;

try:
    from UpdateManager.Core.MetaRelease import MetaReleaseCore
except ImportError:
    META_RELEASE_SUPPORT = False
else:
    META_RELEASE_SUPPORT = True

# Could not load the UpdateManager
if META_RELEASE_SUPPORT == False:
    sys.exit(1);

#FIXME Evil to start the download during init
meta_release = MetaReleaseCore(False, False)

#FIXME: should use a lock
while meta_release.downloading:
    time.sleep(1)

#FIXME: Add support for description
if meta_release.new_dist != None:
    print meta_release.new_dist.name;
    print meta_release.new_dist.version;
