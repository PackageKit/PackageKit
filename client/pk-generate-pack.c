/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2008 Shishir Goel <crazyontheedge@gmail.com>
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

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <dbus/dbus-glib.h>

#include <pk-client.h>
#include <pk-control.h>
#include <pk-package-id.h>
#include <pk-package-ids.h>
#include <pk-common.h>
#ifdef HAVE_ARCHIVE_H
#include <archive.h>
#include <archive_entry.h>
#endif /* HAVE_ARCHIVE_H */
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-tools-common.h"

/**
 * pk_service_pack_main:
 **/
gboolean
pk_service_pack_main (const gchar *pack_filename, const gchar *directory, const gchar *package_id, PkPackageList *exclude_list, GError **error)
{
	return TRUE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_genpack_test (EggTest *test)
{
	PkClient *client = NULL;
	gboolean ret;
	gboolean retval;
	GError *error = NULL;
	gchar *file;
	PkPackageList *list = NULL;
	GPtrArray *file_array = NULL;
	gchar *src;
	gchar **package_ids;

	if (!egg_test_start (test, "PkGeneratePack"))
		return;

	/************************************************************/
	egg_test_title (test, "get client");
	client = pk_client_new ();
	if (client != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "download only NULL");
	ret = pk_service_pack_download_package_ids (client, NULL, NULL);
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "download only gitk");
	package_ids = pk_package_ids_from_id ("gitk;1.5.5.1-1.fc9;i386;installed");
	ret = pk_service_pack_download_package_ids (client, package_ids, "/tmp");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);
	g_strfreev (package_ids);
	g_object_unref (client);

	/************************************************************/
	egg_test_title (test, "metadata NULL");
	ret = pk_service_pack_create_from_files_metadata_file (NULL);
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "metadata /tmp/metadata.conf");
	ret = pk_service_pack_create_from_files_metadata_file ("/tmp/metadata.conf");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);
	g_remove ("/tmp/metadata.conf");

	/************************************************************/
	egg_test_title (test, "scandir NULL");
	file_array = pk_service_pack_scan_files_in_directory (NULL);
	if (file_array == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "scandir /tmp");
	file_array = pk_service_pack_scan_files_in_directory ("/tmp");
	if (file_array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "generate pack /tmp/gitk.servicepack gitk");
	file_array = g_ptr_array_new ();
	src = g_build_filename ("/tmp", "gitk-1.5.5.1-1.fc9.i386.rpm", NULL);
	g_ptr_array_add (file_array, src);
	ret = pk_service_pack_create_from_files ("/tmp/gitk.servicepack", file_array, &error);
	if (!ret) {
		if (error != NULL) {
			egg_test_failed (test, "failed to create pack %s" , error->message);
			g_error_free (error);
		} else {
			egg_test_failed (test, "could not set error");
		}
	} else
		egg_test_success (test, NULL);

	if (file_array != NULL) {
		g_ptr_array_foreach (file_array, (GFunc) g_free, NULL);
		g_ptr_array_free (file_array, TRUE);
	}
	g_remove ("/tmp/gitk.servicepack");

	/************************************************************/
	egg_test_end (test);
}
#endif
