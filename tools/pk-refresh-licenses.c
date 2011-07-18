/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include <stdlib.h>
#include <math.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

typedef struct {
	gchar	*enum_name;
	gchar	*full_name;
} PkRefreshLicenseItem;

/**
 * pk_refresh_licenses_mkenum:
 **/
static gchar *
pk_refresh_licenses_mkenum (const gchar *text)
{
	guint i;
	GString *string;
	gchar c;
	gchar *new;

	string = g_string_new ("PK_LICENSE_ENUM_");

	for (i=0; text[i] != '\0'; i++) {
		c = text[i];
		if (c == '.') {
			new = g_strdup ("_DOT_");
		} else if (c == '-' || c == ' ') {
			new = g_strdup ("_");
		} else if (c == '+') {
			new = g_strdup ("_PLUS");
		} else {
			new = g_strdup_printf ("%c", g_ascii_toupper (c));
		}
		g_string_append (string, new);
		g_free (new);
	}

	return g_string_free (string, FALSE);	
}

/**
 * pk_refresh_licenses_compare_func:
 **/
static gint
pk_refresh_licenses_compare_func (gconstpointer a, gconstpointer b)
{
	PkRefreshLicenseItem **item1 = (PkRefreshLicenseItem **) a;
	PkRefreshLicenseItem **item2 = (PkRefreshLicenseItem **) b;
	return g_strcmp0 ((*item1)->enum_name, (*item2)->enum_name);
}

/**
 * pk_refresh_licenses_get_data:
 **/
static gchar *
pk_refresh_licenses_get_data (const gchar *url)
{
	gchar *command;
	gboolean ret;
	GError *error = NULL;
	gchar *contents = NULL;

	/* get file */
	command = g_strdup_printf ("wget \"%s&action=edit\" --output-document=./Licensing.wiki", url);
	ret = g_spawn_command_line_sync (command, NULL, NULL, NULL, &error);
	if (!ret) {
		g_warning ("failed to download file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get contents */
	ret = g_file_get_contents ("./Licensing.wiki", &contents, NULL, &error);
	if (!ret) {
		g_warning ("failed to get contents: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_unlink ("./Licensing.wiki");
	g_free (command);
	return contents;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean ret;
	gint retval = EXIT_FAILURE;
	GError *error = NULL;
	gchar *contents_tmp = NULL;
	GString *contents;
	gchar **lines = NULL;
	gchar **parts;
	guint i, j;
	const gchar *trim;
	gint fullname = -1;
	gint fsf_free = -1;
	gint shortname = -1;
	GPtrArray *data = NULL;
	gboolean is_col;
	GString *string_txt = NULL;
	PkRefreshLicenseItem *item;
	PkRefreshLicenseItem *item_tmp;
	const gchar *locations[] =  {
		"https://fedoraproject.org/w/index.php?title=Licensing:Main",
		"https://fedoraproject.org/w/index.php?title=Licensing:Fonts/Preferred",
		"https://fedoraproject.org/w/index.php?title=Licensing:Fonts/Good",
		NULL };

	/* get all the data from several sources */
	contents = g_string_new ("");
	for (i=0; locations[i] != NULL; i++) {
		g_print ("GETTING: %s\n", locations[i]);
		contents_tmp = pk_refresh_licenses_get_data (locations[i]);
		g_string_append (contents, contents_tmp);
		g_free (contents_tmp);
	}

	/* output arrays */
	data = g_ptr_array_new ();

	/* split into lines */
	lines = g_strsplit (contents->str, "\n", -1);
	for (i=0; lines[i] != NULL; i++) {
		if (lines[i][0] != '|') {
			fullname = -1;
			fsf_free = -1;
			shortname = -1;
			continue;
		}
		if (g_strstr_len (lines[i], -1, "background-color") != NULL)
			continue;

		parts = g_strsplit (lines[i]+1, "||", -1);

		/* trim spaces */
		for (j=0; parts[j] != NULL; j++)
			parts[j] = g_strstrip (parts[j]);

		if (g_strv_length (parts) < 2)
			goto skip;
		if (g_strv_length (parts) > 6)
			goto skip;

		/* check if this is a column */
		is_col = FALSE;
		for (j=0; parts[j] != NULL; j++) {
			trim = parts[j];

			/* Fedora likes NO in bold on the Wiki */
			if (g_strcmp0 (trim, "'''NO'''") == 0)
				trim = "NO";

			/* is command */
			if (g_str_has_prefix (trim, "'''")) {
				if (g_strcmp0 (trim, "'''Full Name'''") == 0) {
					fullname = j;
					g_debug ("fullname now col %i", fullname);
				} else if (g_strcmp0 (trim, "'''FSF Free?'''") == 0) {
					fsf_free = j;
					g_debug ("fsf_free now col %i", fsf_free);
				} else if (g_strcmp0 (trim, "'''Short Name'''") == 0) {
					shortname = j;
					g_debug ("shortname now col %i", shortname);
				} else if (g_strcmp0 (trim, "'''GPLv2 Compat?'''") == 0) {
					// ignore
				} else if (g_strcmp0 (trim, "'''GPLv2 Compatible?'''") == 0) {
					// ignore
				} else if (g_strcmp0 (trim, "'''GPLv3 Compat?'''") == 0) {
					// ignore
				} else if (g_strcmp0 (trim, "'''Upstream URL'''") == 0) {
					// ignore
				} else if (g_str_has_prefix (trim, "'''[")) {
					// ignore URL
				} else {
					g_warning ("column not matched: %s", trim);
				}
				is_col = TRUE;
			}
		}

		/* we've just processed a column */
		if (is_col)
			goto skip;

		if (fullname == -1) {
			g_warning ("fullname not set for %s", lines[i]+1);
			goto skip;
		}
		if (fsf_free == -1) {
			g_warning ("fsf_free not set for %s", lines[i]+1);
			goto skip;
		}

		/* is license free */
		if (g_ascii_strcasecmp (parts[fsf_free], "Yes") != 0) {
			g_print ("NONFREE: %s\n", parts[fullname]);
			goto skip;
		}

		if (shortname == -1) {
			g_warning ("shortname not set for %s", lines[i]+1);
			goto skip;
		}
	
		/* is note */
		if (g_str_has_prefix (parts[shortname], "(See Note") != 0) {
			g_print ("NOTE: %s\n", parts[fullname]);
			goto skip;
		}
		
		/* add data */
		item = g_new (PkRefreshLicenseItem, 1);
		item->enum_name = pk_refresh_licenses_mkenum (parts[shortname]);
		item->full_name = g_strdup (parts[shortname]);
		g_ptr_array_add (data, item);
		g_print ("FREE: %s\n", parts[fullname]);
skip:
		g_strfreev (parts);
	}

	/* is the enum name duplicated? */
	for (i=0; i<data->len; i++) {
		item = g_ptr_array_index (data, i);

		/* search for items that are the same */
		for (j=i+1; j<data->len; j++) {
			item_tmp = g_ptr_array_index (data, j);
			/* is the same? in which case remove */
			if (g_strcmp0 (item_tmp->enum_name, item->enum_name) == 0)
				g_ptr_array_remove_fast (data, item_tmp);
		}
	}

	/* sort */
	g_ptr_array_sort (data, pk_refresh_licenses_compare_func);

	/* process data, and output to header file */
	string_txt = g_string_new (NULL);
	for (i=0; i<data->len; i++) {
		item = g_ptr_array_index (data, i);
		g_string_append_printf (string_txt, "%s\n", item->full_name);
	}

	/* set c contents */
	ret = g_file_set_contents ("./licenses.txt", string_txt->str, -1, &error);
	if (!ret) {
		g_warning ("failed to set contents: %s", error->message);
		g_error_free (error);
		goto out;
	}

	retval = EXIT_SUCCESS;
out:
	if (string_txt != NULL)
		g_string_free (string_txt, TRUE);
	if (data != NULL)
		g_ptr_array_unref (data);
	g_string_free (contents, TRUE);
	g_strfreev (lines);
	return retval;
}

