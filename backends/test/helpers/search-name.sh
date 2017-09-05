#!/bin/sh
#
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

echo "no-percentage-updates"
sleep 1
echo "percentage	10"
echo "status	query"
sleep 1
echo "percentage	30"
echo "package	available	glib2;2.14.0;i386;fedora	The GLib library"
sleep 1
echo "percentage	70"
echo "package	installed	gtk2;gtk2-2.11.6-6.fc8;i386;fedora	GTK+ Libraries for GIMP"
sleep 1
echo "percentage	100"
exit 0
