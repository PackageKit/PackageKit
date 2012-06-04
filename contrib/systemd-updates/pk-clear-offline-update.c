/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main (int argc, char *argv[])
{
	int rc;

	/* ensure root user */
	if (getuid () != 0 || geteuid () != 0) {
		fprintf (stderr, "This program can only be used using pkexec\n");
		return EXIT_FAILURE;
	}

	/* Just delete the file, no questions asked :) */
	rc = unlink ("/var/lib/PackageKit/offline-update-competed");
	if (rc < 0) {
		fprintf (stderr, "Failed to remove file: %s\n",
			 strerror (errno));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
