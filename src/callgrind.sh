#!/bin/sh
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

if [ "$1x" = "x" ]; then
    echo "NO BACKEND SPECIFIED, using dummy"
    BACKEND=dummy
else
    BACKEND=$1
fi

export G_DEBUG=fatal_criticals
sudo touch /etc/PackageKit/PackageKit.conf
sudo valgrind --tool=callgrind --collect-systime=yes .libs/lt-packagekitd --backend=$BACKEND --disable-timer
