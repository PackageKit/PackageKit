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

#include "config.h"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* just include the header, link to none of the symbols */
#include "lib/packagekit-glib2/pk-offline-private.h"

int
main (int argc, char *argv[])
{
	FILE *fp = NULL;
	int rc;
	int retval = EXIT_SUCCESS;
	struct passwd *pw;

	/* ensure root user */
	if (getuid () != 0 || geteuid () != 0) {
		fprintf (stderr, "This program can only be used using pkexec\n");
		retval = EXIT_FAILURE;
		goto out;
	}

	if (argc > 1 && strcmp (argv[1], "--cancel") == 0) {
		rc = unlink (PK_OFFLINE_TRIGGER_FILENAME);
		if (rc < 0) {
			fprintf (stderr, "Failed to remove file " PK_OFFLINE_TRIGGER_FILENAME ": %s\n",
				 strerror (errno));
			retval = EXIT_FAILURE;
			goto out;
		}

		return EXIT_SUCCESS;
	}

	/* open success action */
	fp = fopen (PK_OFFLINE_ACTION_FILENAME, "w+");
	if (fp == NULL) {
		fprintf (stderr, "Failed to open %s for writing\n",
			 PK_OFFLINE_ACTION_FILENAME);
		retval = EXIT_FAILURE;
		goto out;
	}
	if (argc > 1 && strcmp (argv[1], "power-off") == 0) {
		fputs ("power-off", fp);
	} else {
		fputs ("reboot", fp);
	}

	/* create symlink for the systemd-system-update-generator */
	rc = symlink ("/var/cache", PK_OFFLINE_TRIGGER_FILENAME);
	if (rc < 0) {
		fprintf (stderr, "Failed to create symlink: %s\n",
			 strerror (errno));
		retval = EXIT_FAILURE;
		goto out;
	}

	/* get UID for username */
	pw = getpwnam (PACKAGEKIT_USER);
	if (pw == NULL) {
		fprintf (stderr, "Failed to get PackageKit uid: %s\n",
			 strerror (errno));
		retval = EXIT_FAILURE;
		goto out;
	}

	/* change it to the PackageKit user so the daemon can delete
	 * the file if any package state changes */
	rc = lchown (PK_OFFLINE_TRIGGER_FILENAME, pw->pw_uid, -1);
	if (rc < 0) {
		fprintf (stderr, "Failed to change owner of symlink: %s\n",
			 strerror (errno));
		retval = EXIT_FAILURE;
		goto out;
	}
out:
	if (fp != NULL)
		fclose (fp);
	return retval;
}
