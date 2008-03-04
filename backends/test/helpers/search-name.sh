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

echo -e "no-percentage-updates" > /dev/stderr
sleep 1
echo -e "percentage\t10" > /dev/stderr
echo -e "status	query" > /dev/stderr
sleep 1
echo -e "percentage\t30" > /dev/stderr
echo -e "package	available	glib2;2.14.0;i386;fedora	The GLib library"
sleep 1
echo -e "percentage\t70" > /dev/stderr
echo -e "package	installed	gtk2;gtk2-2.11.6-6.fc8;i386;fedora	GTK+ Libraries for GIMP"
sleep 1
echo -e "percentage\t100" > /dev/stderr
exit 0

