#!/bin/sh
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

if [ -z "${http_proxy}" ]; then
	echo "no http proxy"
	exit 1
fi

if [ -z "${ftp_proxy}" ]; then
	echo "no ftp proxy"
	exit 1
fi

echo -e "percentage\t100"

