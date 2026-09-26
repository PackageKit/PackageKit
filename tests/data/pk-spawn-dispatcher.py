#!/usr/bin/env python3
#
# Copyright (C) 2008 Tim Lauridsen <timlau@fedoraproject.org>
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#

# Dispacher script to run pk commands from stdin

import sys
import time
import os

sys.path.insert(0, os.path.join(os.getcwd(), 'python'))

from packagekit.backend import *


class PackageKitYumBackend(PackageKitBaseBackend):
    def __init__(self, args, lock=True):
        PackageKitBaseBackend.__init__(self, args)
        PackageKitBaseBackend.doLock(self)
        # simulate doing something
        time.sleep(2)

    def unLock(self):
        PackageKitBaseBackend.unLock(self)
        # simulate doing something
        time.sleep(0.5)

    def search_name(self, filters, key):
        # check we escape spaces properly
        if key == ['power manager']:
            self.package("polkit;0.0.1;i386;data", INFO_AVAILABLE, "PolicyKit daemon")


def main():
    backend = PackageKitYumBackend('', lock=True)
    backend.dispatcher(sys.argv[1:])


if __name__ == "__main__":
    main()
