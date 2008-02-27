#!/usr/bin/env python
"""
Allows to start the apt2 backend in a profling mode
"""

__author__ = "Sebastian Heinlein <devel@glatzor.de>"


import hotshot
import sys

from aptDBUSBackend import main

if len(sys.argv) == 2:
    profile = sys.argv[1]
else:
    profile = "profile"

prof = hotshot.Profile(profile)
print prof.runcall(main)
prof.close()
