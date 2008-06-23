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
#include <libintl.h>
#include <locale.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-common.h>
#include <pk-package-id.h>
#include <pk-package-ids.h>
#include <pk-extra.h>

#include "pk-import-common.h"

#define PK_SPECSPO_DOMAIN 	"redhat-dist"

static PkClient *client = NULL;
static PkExtra *extra = NULL;
static GPtrArray *locale_array = NULL;
static GPtrArray *package_array = NULL;

/**
 * pk_import_specspo_get_summary:
 **/
static const gchar *
pk_import_specspo_get_summary (const gchar *name)
{
	guint size;
	gboolean ret;
	PkPackageItem *item;
	GError *error = NULL;
	PkPackageList *list;
	gchar **names;

	ret = pk_client_reset (client, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return NULL;
	}

	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, TRUE, NULL);
	names = pk_package_ids_from_id (name);
	ret = pk_client_resolve (client, PK_FILTER_ENUM_NONE, names, &error);
	g_strfreev (names);
	if (!ret) {
		pk_warning ("failed to resolve: %s", error->message);
		g_error_free (error);
		return NULL;
	}

	/* check that we only matched one package */
	list = pk_client_get_package_list (client);
	size = pk_package_list_get_size (list);
	if (size != 1) {
		pk_warning ("not correct size, %i", size);
		return NULL;
	}

	/* get the item */
	item = pk_package_list_get_item (list, 0);
	if (item == NULL) {
		pk_error ("cannot get item");
		g_object_unref (list);
		return NULL;
	}
	g_object_unref (list);

	return item->summary;
}

/**
 * pk_import_specspo_do_package:
 **/
static void
pk_import_specspo_do_package (const gchar *package_name)
{
	const gchar *summary;
	const gchar *locale;
	char *trans;
	char *set_locale;
	guint j;

	summary = pk_import_specspo_get_summary (package_name);
	if (summary == NULL) {
		g_print ("no summary for %s\n", package_name);
		return;
	}
	g_print ("processing %s [", package_name);
//	g_print ("%s,", summary);

	for (j=0; j<locale_array->len; j++) {
		locale = g_ptr_array_index (locale_array, j);
		set_locale = setlocale (LC_ALL, locale);
		if (pk_strequal (set_locale, locale)) {
			/* get the translation */
			trans = gettext (summary);

			/* if different, then save */
			if (pk_strequal (summary, trans) == FALSE) {
				g_print (" %s", locale);
//				g_print (" %s", trans);
				pk_extra_set_locale (extra, locale);
				pk_extra_set_data_locale (extra, package_name, trans);
			}
		}
	}
	g_print ("]\n");
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
	const gchar *package;
	guint i;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Show extra debugging information", NULL },
		{ "database-location", '\0', 0, G_OPTION_ARG_STRING, &database_location,
			"Database location (default set from daemon)", NULL },
		{ NULL}
	};

	g_type_init ();

	context = g_option_context_new ("pk-import-specspo");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	pk_debug_init (verbose);

	client = pk_client_new ();
	locale_array = pk_import_get_locale_list ();
	package_array = pk_import_get_package_list ();

	extra = pk_extra_new ();
	ret = pk_extra_set_database (extra, database_location);
	if (!ret) {
		g_print (_("Could not open database: %s"), database_location);
		g_print ("\n%s\n", _("You probably need to run this program as the root user"));
		goto out;
	}

	/* set the gettext bits */
	textdomain (PK_SPECSPO_DOMAIN);
	bindtextdomain (PK_SPECSPO_DOMAIN, PK_IMPORT_LOCALEDIR);
	bind_textdomain_codeset (PK_SPECSPO_DOMAIN, "UTF-8");

	/* for each locale */
	for (i=0; i<package_array->len; i++) {
		package = g_ptr_array_index (package_array, i);
		pk_import_specspo_do_package (package);
	}

out:
	g_object_unref (client);
	g_object_unref (extra);
	g_ptr_array_free (package_array, TRUE);
	g_ptr_array_free (locale_array, TRUE);

	return 0;
}

