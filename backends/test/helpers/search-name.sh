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

echo -e "no-percentage-updates"
sleep 1
echo -e "percentage\t10"
echo -e "status\tquery"
sleep 1
echo -e "percentage\t30"
echo -e "package\tavailable\tglib2;2.14.0;i386;fedora\tThe GLib library"
sleep 1
echo -e "percentage\t70"
echo -e "package\tinstalled\tgtk2;gtk2-2.11.6-6.fc8;i386;fedora\tGTK+ Libraries for GIMP"
sleep 1
echo -e "percentage\t100"
exit 0

