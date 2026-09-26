#!/usr/bin/env python3
#
# Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#

from signal import *
from time import sleep
from sys import stdout


def process_term(signum, frame):
    exit()


def main():
    signal(SIGTERM, process_term)

    for i in [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]:
        stdout.write("percentage\t%i\n" % (i * 10))
        stdout.flush()
        sleep(0.3)


if __name__ == "__main__":
    main()
