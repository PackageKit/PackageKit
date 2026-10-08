#!/bin/sh
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
# Copyright (C) 2024 Matthias Klumpp <matthias@tenstral.net>
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

# Upload the website and the reference manual.
# Usage: docs/upload.sh BUILDDIR [SSH_USER]

set -e

BUILDDIR="${1:?Usage: $0 BUILDDIR [SSH_USER]}"
SSH_USER="${2:-mak}"
SERVER="annarchy.freedesktop.org"
LOCATION="/srv/www.freedesktop.org/www/software/PackageKit"

SRCDIR="$(dirname "$0")"
MANUAL="$BUILDDIR/docs/api/packagekit"

if [ ! -f "$MANUAL/index.html" ]; then
	echo "No reference manual in $MANUAL, build with -Ddocs=true first." >&2
	exit 1
fi

scp -r "$SRCDIR"/html/* "$SSH_USER@$SERVER:$LOCATION/"
ssh "$SSH_USER@$SERVER" mkdir -p "$LOCATION/doc"
scp -r "$MANUAL"/* "$SSH_USER@$SERVER:$LOCATION/doc/"
