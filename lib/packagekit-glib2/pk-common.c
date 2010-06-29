/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-common
 * @short_description: Common utility functions for PackageKit
 *
 * This file contains functions that may be useful.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <string.h>

#include <glib.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>

#include "egg-debug.h"
#include "egg-string.h"

/**
 * pk_iso8601_present:
 *
 * Return value: The current iso8601 date and time
 *
 * Since: 0.5.2
 **/
gchar *
pk_iso8601_present (void)
{
	GTimeVal timeval;
	gchar *timespec;

	/* get current time */
	g_get_current_time (&timeval);
	timespec = g_time_val_to_iso8601 (&timeval);

	return timespec;
}

/**
 * pk_iso8601_from_date:
 * @date: a %GDate to convert
 *
 * Return value: If valid then a new ISO8601 date, else NULL
 *
 * Since: 0.5.2
 **/
gchar *
pk_iso8601_from_date (const GDate *date)
{
	gsize retval;
	gchar iso_date[128];

	if (date == NULL)
		return NULL;
	retval = g_date_strftime (iso_date, 128, "%F", date);
	if (retval == 0)
		return NULL;
	return g_strdup (iso_date);
}

/**
 * pk_iso8601_to_date:
 * @iso_date: The ISO8601 date to convert
 *
 * Return value: If valid then a new %GDate, else NULL
 *
 * Since: 0.5.2
 **/
GDate *
pk_iso8601_to_date (const gchar *iso_date)
{
	gboolean ret;
	guint retval;
	guint d = 0;
	guint m = 0;
	guint y = 0;
	GTimeVal time_val;
	GDate *date = NULL;

	if (egg_strzero (iso_date))
		goto out;

	/* try to parse complete ISO8601 date */
	ret = g_time_val_from_iso8601 (iso_date, &time_val);
	if (ret) {
		date = g_date_new ();
		g_date_set_time_val (date, &time_val);
		goto out;
	}

	/* g_time_val_from_iso8601() blows goats and won't
	 * accept a valid ISO8601 formatted date without a
	 * time value - try and parse this case */
	retval = sscanf (iso_date, "%u-%u-%u", &y, &m, &d);
	if (retval != 3) {
		egg_warning ("could not parse date '%s'", iso_date);
		goto out;
	}

	/* check it's valid */
	ret = g_date_valid_dmy (d, m, y);
	if (!ret) {
		egg_warning ("invalid date %i/%i/%i from '%s'", y, m, d, iso_date);
		goto out;
	}

	/* create valid object */
	date = g_date_new_dmy (d, m, y);
out:
	return date;
}

/**
 * pk_ptr_array_to_strv:
 * @array: the GPtrArray of strings
 *
 * Form a composite string array of strings.
 * The data in the GPtrArray is copied.
 *
 * Return value: the string array, or %NULL if invalid
 *
 * Since: 0.5.2
 **/
gchar **
pk_ptr_array_to_strv (GPtrArray *array)
{
	gchar **value;
	const gchar *value_temp;
	guint i;

	g_return_val_if_fail (array != NULL, NULL);

	/* copy the array to a strv */
	value = g_new0 (gchar *, array->len + 1);
	for (i=0; i<array->len; i++) {
		value_temp = (const gchar *) g_ptr_array_index (array, i);
		value[i] = g_strdup (value_temp);
	}

	return value;
}
