#!/bin/sh
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

sudo echo "Build!"
#autobuild.sh all PolicyKit
#sudo auto_refresh_from_repo.sh
#autobuild.sh all PolicyKit-gnome
#sudo auto_refresh_from_repo.sh
autobuild.sh all PackageKit force
sudo auto_refresh_from_repo.sh
autobuild.sh all gnome-packagekit force
sudo auto_refresh_from_repo.sh

