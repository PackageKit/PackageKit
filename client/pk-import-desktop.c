/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-common.h>
#include <pk-package-id.h>
#include <pk-extra.h>

#include "pk-import-common.h"

static PkClient *client = NULL;
static PkExtra *extra = NULL;

static gchar *
pk_desktop_get_name_for_file (const gchar *filename)
{
	guint size;
	gchar *name;
	PkPackageItem *item;
	PkPackageId *pid;
	gboolean ret;

	/* use PK to find the correct package */
	pk_client_reset (client, NULL);
	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, TRUE, NULL);
	ret = pk_client_search_file (client, "installed", filename, NULL);
	if (!ret) {
		return NULL;
	}

	/* check that we only matched one package */
	size = pk_client_package_buffer_get_size (client);
	if (size != 1) {
		pk_warning ("not correct size, %i", size);
		return NULL;
	}

	/* get the item */
	item = pk_client_package_buffer_get_item (client, 0);
	if (item == NULL) {
		pk_error ("cannot get item");
		return NULL;
	}

	/* get the package name */
	pid = pk_package_id_new_from_string (item->package_id);
	if (pid == NULL) {
		pk_error ("cannot allocate package id");
		return NULL;
	}

	/* strip the name */
	name = g_strdup (pid->name);
	pk_package_id_free (pid);

	/* return a copy */
	return name;
}

static gchar *
pk_import_get_locale (const gchar *buffer)
{
	guint len;
	gchar *locale;
	gchar *result;
	result = g_strrstr (buffer, "[");
	if (result == NULL) {
		return NULL;
	}
	locale = g_strdup (result+1);
	len = strlen (locale);
	locale[len-1] = '\0';
	return locale;
}

static void
pk_desktop_process_desktop (const gchar *package_name, const gchar *filename)
{
	GKeyFile *key;
	gboolean ret;
	guint i;
	gchar *name = NULL;
	gchar *name_unlocalised = NULL;
	gchar *exec = NULL;
	gchar *icon = NULL;
	gchar *comment = NULL;
	gchar *genericname = NULL;
	const gchar *locale = NULL;
	gchar **key_array;
	gsize len;
	gchar *locale_temp;
	static GPtrArray *locale_array = NULL;

	key = g_key_file_new ();
	ret = g_key_file_load_from_file (key, filename, G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
	if (ret == FALSE) {
		pk_error ("bad!!");
	}

	/* get this specific locale list */
	key_array = g_key_file_get_keys (key, G_KEY_FILE_DESKTOP_GROUP, &len, NULL);
	locale_array = g_ptr_array_new ();
	for (i=0; i<len; i++) {
		if (g_str_has_prefix (key_array[i], "Name")) {
			/* set the locale */
			locale_temp = pk_import_get_locale (key_array[i]);
			g_ptr_array_add (locale_array, g_strdup (locale_temp));
		}
	}
	g_strfreev (key_array);

	g_print ("PackageName:\t%s\t[", package_name);

	/* get the default entry */
	name_unlocalised = g_key_file_get_string (key, G_KEY_FILE_DESKTOP_GROUP, "Name", NULL);

	/* for each locale */
	for (i=0; i<locale_array->len; i++) {
		locale = g_ptr_array_index (locale_array, i);
		/* compare the translated against the default */
		name = g_key_file_get_locale_string (key, G_KEY_FILE_DESKTOP_GROUP, "Name", locale, NULL);

		/* if different, then save */
		if (pk_strequal (name_unlocalised, name) == FALSE) {
			g_print (" %s", locale);
			comment = g_key_file_get_locale_string (key, G_KEY_FILE_DESKTOP_GROUP,
								"Comment", locale, NULL);
			genericname = g_key_file_get_locale_string (key, G_KEY_FILE_DESKTOP_GROUP,
								    "GenericName", locale, NULL);
			pk_debug ("PackageName=%s, Locale=%s, Name=%s, GenericName=%s, Comment=%s",
				  package_name, locale, name, genericname, comment);
			pk_extra_set_locale (extra, locale);

			/* save in order of priority */
			if (comment != NULL) {
				pk_extra_set_localised_detail (extra, package_name, comment);
			} else if (genericname != NULL) {
				pk_extra_set_localised_detail (extra, package_name, genericname);
			} else {
				pk_extra_set_localised_detail (extra, package_name, name);
			}
			g_free (comment);
			g_free (genericname);
		}
		g_free (name);
	}
	g_ptr_array_free (locale_array, TRUE);
	g_free (name_unlocalised);
	g_print ("]\n");

	exec = g_key_file_get_string (key, G_KEY_FILE_DESKTOP_GROUP, "Exec", NULL);
	icon = g_key_file_get_string (key, G_KEY_FILE_DESKTOP_GROUP, "Icon", NULL);
	pk_debug ("PackageName=%s, Exec=%s, Icon=%s", package_name, exec, icon);
	pk_extra_set_package_detail (extra, package_name, icon, exec);
	g_free (icon);
	g_free (exec);

	g_key_file_free (key);
}

static void
pk_desktop_process_directory (const gchar *directory)
{
	GDir *dir;
	const gchar *name;
	GPatternSpec *pattern;
	gboolean match;
	gchar *filename;
	gchar *package_name;

	dir = g_dir_open (directory, 0, NULL);
	if (dir == NULL) {
		pk_error ("not a valid desktop dir!");
	}

	pattern = g_pattern_spec_new ("*.desktop");
	name = g_dir_read_name (dir);
	while (name != NULL) {
		/* ITS4: ignore, not used for allocation */
		match = g_pattern_match (pattern, strlen (name), name, NULL);
		if (match == TRUE) {
			filename = g_build_filename (directory, name, NULL);

			/* get the name */
			package_name = pk_desktop_get_name_for_file (filename);

			/* process the file */
			pk_desktop_process_desktop (package_name, filename);
			g_free (package_name);
			g_free (filename);
		}
		name = g_dir_read_name (dir);
	}
	g_dir_close (dir);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GOptionContext *context;
	gboolean verbose = FALSE;
	gboolean ret;
	gchar *database_location = NULL;
	gchar *desktop_location = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Show extra debugging information", NULL },
		{ "database-location", '\0', 0, G_OPTION_ARG_STRING, &database_location,
			"Database location (default set from daemon)", NULL },
		{ "desktop-location", '\0', 0, G_OPTION_ARG_STRING, &desktop_location,
			"Desktop location (default " PK_IMPORT_APPLICATIONSDIR ")", NULL },
		{ NULL}
	};

	g_type_init ();

	context = g_option_context_new ("pk-desktop");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	pk_debug_init (verbose);

	/* set defaults */
	if (desktop_location == NULL) {
		desktop_location = PK_IMPORT_APPLICATIONSDIR;
	}

	client = pk_client_new ();
	extra = pk_extra_new ();
	ret = pk_extra_set_database (extra, database_location);
	if (!ret) {
		pk_warning ("could not open database %s", database_location);
		goto out;
	}

	pk_desktop_process_directory (desktop_location);

out:
	g_object_unref (extra);
	g_object_unref (client);
	return 0;
}
