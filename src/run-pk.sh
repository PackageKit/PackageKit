#!/bin/sh
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

if [ "$USER" != "root" ]; then
    echo "You are not running this script as root. Use sudo."
    exit 1
fi

if [ "$1x" = "x" ]; then
    BACKEND=dummy
else
    BACKEND=$1
fi
export G_DEBUG=fatal_criticals
killall packagekitd
./packagekitd --verbose --disable-timer --backend=$BACKEND

