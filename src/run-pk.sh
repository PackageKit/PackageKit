#!/bin/sh
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

# check some important things are installed systemwide
if [ ! -e "/etc/dbus-1/system.d/org.freedesktop.PackageKit.conf" ]; then
    echo "You need to install the DBus policy. Use sudo cp ../data/org.freedesktop.PackageKit.conf /etc/dbus-1/system.d"
    exit 1
fi
if [ ! -e "/usr/share/polkit-1/actions/org.freedesktop.packagekit.policy" ]; then
    echo "You need to install the PolicyKit rules. Use sudo cp ../policy/org.freedesktop.packagekit.policy /usr/share/polkit-1/actions"
    exit 1
fi


if [ "$1x" = "x" ]; then
    echo "NO BACKEND SPECIFIED, using dummy"
    BACKEND=dummy
else
    BACKEND=$1
fi
export G_DEBUG=fatal_criticals
killall packagekitd
./packagekitd --verbose --disable-timer --keep-environment --backend=$BACKEND

