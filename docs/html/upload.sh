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

scp img/*.png $USER@$SERVER:/$LOCATION/img/
scp *.html $USER@$SERVER:/$LOCATION/
scp *.css $USER@$SERVER:/$LOCATION/
scp ../spec/pk-reference.html $USER@$SERVER:/$LOCATION/
scp ../spec/pk-*.png $USER@$SERVER:/$LOCATION/
scp ../api/html/*.html $USER@$SERVER:/$LOCATION/gtk-doc/
scp ../api/html/*.png $USER@$SERVER:/$LOCATION/gtk-doc/
scp ../api/html/*.css $USER@$SERVER:/$LOCATION/gtk-doc/

