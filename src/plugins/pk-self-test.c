/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2010 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>

#include "pk-lsof.h"

static void
pk_test_lsof_get_files_for_directory (GPtrArray *files, const gchar *dirname)
{
	GDir *dir;
	const gchar *filename;
	dir = g_dir_open (dirname, 0, NULL);
	if (dir == NULL)
		return;
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		if (g_str_has_prefix (filename, "libglib-2.0.so"))
			g_ptr_array_add (files,
					 g_build_filename (dirname, filename, NULL));
		filename = g_dir_read_name (dir);
	}
	g_dir_close (dir);
}

static void
pk_test_lsof_func (void)
{
	gboolean ret;
	PkLsof *lsof;
	GPtrArray *pids;
	GPtrArray *files;

	lsof = pk_lsof_new ();
	g_assert (lsof != NULL);

	/* refresh lsof data */
	ret = pk_lsof_refresh (lsof);
	g_assert (ret);

	/* get pids for some test files */
	files = g_ptr_array_new_with_free_func (g_free);
	pk_test_lsof_get_files_for_directory (files, "/lib");
	pk_test_lsof_get_files_for_directory (files, "/usr/lib");
	pk_test_lsof_get_files_for_directory (files, "/usr/lib64");
	pk_test_lsof_get_files_for_directory (files, "/usr/lib/x86_64-linux-gnu");
	pk_test_lsof_get_files_for_directory (files, "/usr/lib/i386-linux-gnu");
	g_assert_cmpint (files->len, >, 0);
	g_ptr_array_add (files, NULL);
	pids = pk_lsof_get_pids_for_filenames (lsof, (gchar**) files->pdata);
	g_assert_cmpint (pids->len, >, 0);
	g_ptr_array_unref (pids);

	g_ptr_array_unref (files);
	g_object_unref (lsof);
}

int
main (int argc, char **argv)
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/plugins/lsof", pk_test_lsof_func);

	return g_test_run ();
}

