/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:pk-common
 * @short_description: Common utility functions for PackageKit
 *
 * This file contains functions that may be useful.
 */

#include "config.h"

#include <glib.h>
#include <glib/gstdio.h>

#include "egg-debug.h"

/**
 * pk_directory_remove_contents:
 *
 * Does not remove the directory itself, only the contents.
 **/
gboolean
pk_directory_remove_contents (const gchar *directory)
{
	gboolean ret = FALSE;
	GDir *dir;
	GError *error = NULL;
	const gchar *filename;
	gchar *src;
	gint retval;

	/* try to open */
	dir = g_dir_open (directory, 0, &error);
	if (dir == NULL) {
		egg_warning ("cannot open directory: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find each */
	while ((filename = g_dir_read_name (dir))) {
		src = g_build_filename (directory, filename, NULL);
		ret = g_file_test (src, G_FILE_TEST_IS_DIR);
		if (ret) {
			egg_debug ("directory %s found in %s, deleting", filename, directory);
			/* recurse, but should be only 1 level deep */
			pk_directory_remove_contents (src);
			retval = g_remove (src);
			if (retval != 0)
				egg_warning ("failed to delete %s", src);
		} else {
			egg_debug ("file found in %s, deleting", directory);
			retval = g_unlink (src);
			if (retval != 0)
				egg_warning ("failed to delete %s", src);
		}
		g_free (src);
	}
	g_dir_close (dir);
	ret = TRUE;
out:
	return ret;
}

