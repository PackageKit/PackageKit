#!/bin/sh
#
# Copyright (C) 2007 Grzegorz Dabrowski <gdx@o2.pl>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

echo "no-percentage-updates" > /dev/stderr
echo "status	remove" > /dev/stderr
pkg=$(echo "$2" | cut -f1 -d';')
box -u "$pkg" 2>&1 >/dev/null
