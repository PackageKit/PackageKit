#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Provides an apt backend to PackageKit

Copyright (C) 2008 Sebastian Heinlein <glatzor@ubuntu.com>

Licensed under the GNU General Public License Version 2

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

__author__  = "Sebastian Heinlein <devel@glatzor.de>"

import hotshot
import hotshot.stats
import optparse
import sys

def main():
    parser = optparse.OptionParser(usage="hotshot-analyze.py "
                                         "PATH_TO_STATS_FILE",
                                   description="Statistics analyzer for the "
                                               "HotShot Python profiler")
    (options, args) = parser.parse_args()
    if len(args) != 1:
        parser.print_help()
        sys.exit(1)

    stats = hotshot.stats.load(args[0])
    stats.strip_dirs()
    stats.sort_stats('time', 'calls')
    stats.print_stats(20)

if __name__ == "__main__":
    main()
