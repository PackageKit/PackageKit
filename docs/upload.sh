#!/bin/sh
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

USER="hughsient"
SERVER="annarchy.freedesktop.org"
LOCATION="/srv/www.freedesktop.org/www/software/PackageKit/"

scp -r html/* $USER@$SERVER:$LOCATION
scp api/html/*.html $USER@$SERVER:/$LOCATION/gtk-doc/
scp api/html/*.png $USER@$SERVER:/$LOCATION/gtk-doc/
scp api/html/*.css $USER@$SERVER:/$LOCATION/gtk-doc/
