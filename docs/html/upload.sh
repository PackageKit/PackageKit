#!/bin/sh
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

USER="hughsie"
SERVER="packagekit.org"
LOCATION="/srv/www/html"

scp *.html $USER@$SERVER:/$LOCATION/
scp img/*.png $USER@$SERVER:/$LOCATION/img/
scp img/thumbnails/*.png $USER@$SERVER:/$LOCATION/img/thumbnails/
scp videos/*.ogv $USER@$SERVER:/$LOCATION/videos/
scp *.css $USER@$SERVER:/$LOCATION/
scp ../api/html/*.html $USER@$SERVER:/$LOCATION/gtk-doc/
scp ../api/html/*.png $USER@$SERVER:/$LOCATION/gtk-doc/
scp ../api/html/*.css $USER@$SERVER:/$LOCATION/gtk-doc/
docbook2pdf ../../man/pkcon.xml --output ./files
docbook2pdf ../../man/pkmon.xml --output ./files
scp files/* $USER@$SERVER:/$LOCATION/files/
scp ../../contrib/PackageKit.catalog $USER@$SERVER:/$LOCATION/files/
docbook2html ../../../gnome-packagekit/docs/dbus/org.freedesktop.PackageKit.ref.xml
mv r1.html api-reference-session.html
scp api-reference-session.html $USER@$SERVER:/$LOCATION/gtk-doc/
rm api-reference-session.html
scp ../example.catalog $USER@$SERVER:/$LOCATION/packagekit.catalog

