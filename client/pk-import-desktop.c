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

#define PK_EXTRA_DESKTOP_DATABASE		"/usr/share/applications"

static PkClient *client = NULL;
static PkExtra *extra = NULL;

static gchar *
pk_desktop_get_name_for_file (const gchar *filename)
{
	guint size;
	gchar *name;
	PkPackageItem *item;
	PkPackageId *pid;

	/* use PK to find the correct package */
	pk_client_reset (client);
	pk_client_set_use_buffer (client, TRUE);
	pk_client_set_synchronous (client, TRUE);
	pk_client_search_file (client, "installed", filename);

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

static void
pk_desktop_process_desktop (const gchar *package_name, const gchar *filename)
{
	GKeyFile *key;
	gboolean ret;
	gchar *name = NULL;
	gchar *name_unlocalised = NULL;
	gchar *exec = NULL;
	gchar *icon = NULL;
	gchar *comment = NULL;
	gchar *genericname = NULL;
	const gchar *locale = NULL;

	key = g_key_file_new ();
	ret = g_key_file_load_from_file (key, filename, G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
	if (ret == FALSE) {
		pk_error ("bad!!");
	}

	guint i = 0;
	const gchar *locale_array[] = {"ar", "bg", "ca", "da", "de", "dz", "el", "es", "et", "fi", "gl",
				       "hu", "it", "ja", "ka", "mk", "nb", "pa", "pl", "pt", "pt_BR",
				       "ru", "sl", "sv", "th", "uk", "vi", "zh_CN", "zh_HK", "zh_TW", NULL};
	g_print ("PackageName:\t%s\t[default", package_name);

	/* get the default entry */
	name_unlocalised = g_key_file_get_string (key, G_KEY_FILE_DESKTOP_GROUP, "Name", NULL);

	/* for each locale */
	do {
		locale = locale_array[i];
		/* compare the translated against the default */
		name = g_key_file_get_locale_string (key, G_KEY_FILE_DESKTOP_GROUP, "Name", locale, NULL);

		/* if different, then save */
		if (pk_strequal (name_unlocalised, name) == FALSE) {
			g_print (" %s", locale);
			comment = g_key_file_get_locale_string (key, G_KEY_FILE_DESKTOP_GROUP, "Comment", locale, NULL);
			genericname = g_key_file_get_locale_string (key, G_KEY_FILE_DESKTOP_GROUP, "GenericName", locale, NULL);
			pk_debug ("PackageName=%s, Locale=%s, Name=%s, GenericName=%s, Comment=%s", package_name, locale, name, genericname, comment);
			pk_extra_set_locale (extra, locale);
			pk_extra_set_localised_detail (extra, package_name, name, genericname, comment);
			g_free (comment);
			g_free (genericname);
		}
		g_free (name);
	} while (locale_array[i++] != NULL); /* this means we get one last run with NULL */
	g_free (name_unlocalised);
	g_print ("]\n");

	exec = g_key_file_get_locale_string (key, G_KEY_FILE_DESKTOP_GROUP, "Exec", locale, NULL);
	icon = g_key_file_get_locale_string (key, G_KEY_FILE_DESKTOP_GROUP, "Icon", locale, NULL);
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
	gchar *database_location = NULL;
	gchar *desktop_location = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Show extra debugging information", NULL },
		{ "database-location", '\0', 0, G_OPTION_ARG_STRING, &database_location,
			"Database location (default set from daemon)", NULL },
		{ "desktop-location", '\0', 0, G_OPTION_ARG_STRING, &desktop_location,
			"Desktop location (default " PK_EXTRA_DESKTOP_DATABASE ")", NULL },
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
		desktop_location = PK_EXTRA_DESKTOP_DATABASE;
	}

	client = pk_client_new ();
	extra = pk_extra_new ();
	pk_extra_set_database (extra, database_location);

	pk_desktop_process_directory (desktop_location);

	g_object_unref (extra);
	g_object_unref (client);
	return 0;
}
