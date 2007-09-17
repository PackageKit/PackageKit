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

echo "no-percentage-updates" > /dev/stderr
echo "status	query" > /dev/stderr
sleep 1
echo "package	1	glib2;2.14.0;i386;fedora	The GLib library"
sleep 1
echo "package	1	gtk2;gtk2-2.11.6-6.fc8;i386;fedora	GTK+ Libraries for GIMP"
exit 0

