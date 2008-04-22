#!/bin/sh -e
#
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

FIRMWARE_DIR="/lib/firmware"

if [ ! -e "$FIRMWARE_DIR/$FIRMWARE" ]; then
	# we've need firmware
	echo "$FIRMWARE_DIR/$FIRMWARE" > /var/run/PackageKit/udev-firmware
fi

exit 0

