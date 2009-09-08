/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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

#include <stdio.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-debug.h"

#include "pk-client-sync.h"
#include "pk-console-shared.h"

/**
 * pk_console_get_number:
 **/
guint
pk_console_get_number (const gchar *question, guint maxnum)
{
	gint answer = 0;
	gint retval;

	/* pretty print */
	g_print ("%s", question);

	do {
		/* get a number */
		retval = scanf("%u", &answer);

		/* positive */
		if (retval == 1 && answer > 0 && answer <= (gint) maxnum)
			break;
		g_print (_("Please enter a number from 1 to %i: "), maxnum);
	} while (TRUE);
	return answer;
}

/**
 * pk_console_get_prompt:
 **/
gboolean
pk_console_get_prompt (const gchar *question, gboolean defaultyes)
{
	gchar answer = '\0';
	gboolean ret = FALSE;

	/* pretty print */
	g_print ("%s", question);
	if (defaultyes)
		g_print (" [Y/n] ");
	else
		g_print (" [N/y] ");

	do {
		/* ITS4: ignore, we are copying into the same variable, not a string */
		answer = (gchar) fgetc (stdin);

		/* positive */
		if (answer == 'y' || answer == 'Y') {
			ret = TRUE;
			break;
		}
		/* negative */
		if (answer == 'n' || answer == 'N')
			break;

		/* default choice */
		if (answer == '\n' && defaultyes) {
			ret = TRUE;
			break;
		}
		if (answer == '\n' && !defaultyes)
			break;
	} while (TRUE);

	/* remove the trailing \n */
	answer = (gchar) fgetc (stdin);
	if (answer != '\n')
		ungetc (answer, stdin);

	return ret;
}

/**
 * pk_console_resolve_package:
 **/
gchar *
pk_console_resolve_package (PkClient *client, PkBitfield filter, const gchar *package, GError **error)
{
	gchar *package_id = NULL;
	gboolean valid;
	gchar **tmp;
	PkResults *results;
	GPtrArray *array = NULL;
	guint i;
	gchar *printable;
	const PkResultItemPackage *item;

	/* have we passed a complete package_id? */
	valid = pk_package_id_check (package);
	if (valid)
		return g_strdup (package);

	/* split */
	tmp = g_strsplit (package, ",", -1);

	/* get the list of possibles */
	results = pk_client_resolve_sync (client, filter, tmp, NULL, NULL, NULL, error);
	if (results == NULL)
		goto out;

	/* get the packages returned */
	array = pk_results_get_package_array (results);
	if (array == NULL) {
		*error = g_error_new (1, 0, "did not get package struct for %s", package);
		goto out;
	}

	/* nothing found */
	if (array->len == 0) {
		*error = g_error_new (1, 0, "could not find %s", package);
		goto out;
	}

	/* just one thing found */
	if (array->len == 1) {
		item = g_ptr_array_index (array, 0);
		package_id = g_strdup (item->package_id);
		goto out;
	}

	/* TRANSLATORS: more than one package could be found that matched, to follow is a list of possible packages  */
	g_print ("%s\n", _("More than one package matches:"));
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		printable = pk_package_id_to_printable (item->package_id);
		g_print ("%i. %s\n", i+1, printable);
		g_free (printable);
	}

	/* TRANSLATORS: This finds out which package in the list to use */
	i = pk_console_get_number (_("Please choose the correct package: "), array->len);
	item = g_ptr_array_index (array, i-1);
	package_id = g_strdup (item->package_id);
out:
	if (results != NULL)
		g_object_unref (results);
	if (array != NULL)
		g_ptr_array_unref (array);
	g_strfreev (tmp);
	return package_id;
}

/**
 * pk_console_resolve_packages:
 **/
gchar **
pk_console_resolve_packages (PkClient *client, PkBitfield filter, gchar **packages, GError **error)
{
	gchar **package_ids;
	guint i;
	guint len;

	/* get length */
	len = g_strv_length (packages);
	egg_debug ("resolving %i packages", len);

	/* create output array*/
	package_ids = g_new0 (gchar *, len+1);

	/* resolve each package */
	for (i=0; i<len; i++) {
		package_ids[i] = pk_console_resolve_package (client, filter, packages[i], error);
		if (package_ids[i] == NULL) {
			/* destroy state */
			g_strfreev (package_ids);
			package_ids = NULL;
			break;
		}
	}
	return package_ids;
}

