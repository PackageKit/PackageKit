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
 * @title: Common functions
 * @short_description: Common utility functions
 *
 * This file contains functions that may be useful.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <sys/utsname.h>

#include <glib.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-common-private.h>
#include <packagekit-glib2/pk-enum.h>

/**
 * pk_iso8601_present:
 * 
 * Get the current date / time as an ISO8601 string.
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
 * @date: a #GDate to convert
 *
 * Convert a date into an ISO8601 data string.
 *
 * Return value: If valid then a new ISO8601 date, else %NULL
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
 * pk_iso8601_to_date: (skip)
 * @iso_date: The ISO8601 date to convert
 *
 * Convert an ISO8601 date string to a #GDate.
 *
 * Return value: If valid then a new %GDate, else NULL
 *
 * Since: 0.5.2
 **/
GDate *
pk_iso8601_to_date (const gchar *iso_date)
{
	gboolean ret = FALSE;
	guint retval;
	guint d = 0;
	guint m = 0;
	guint y = 0;
	GTimeVal time_val;
	GDate *date = NULL;

	if (iso_date == NULL || iso_date[0] == '\0')
		goto out;

	/* try to parse complete ISO8601 date */
	if (g_strstr_len (iso_date, -1, " ") != NULL)
		ret = g_time_val_from_iso8601 (iso_date, &time_val);
	if (ret && time_val.tv_sec != 0) {
		g_debug ("Parsed %s %i", iso_date, ret);
		date = g_date_new ();
		g_date_set_time_val (date, &time_val);
		goto out;
	}

	/* g_time_val_from_iso8601() blows goats and won't
	 * accept a valid ISO8601 formatted date without a
	 * time value - try and parse this case */
	retval = sscanf (iso_date, "%u-%u-%u", &y, &m, &d);
	if (retval != 3)
		goto out;

	/* check it's valid */
	ret = g_date_valid_dmy (d, m, y);
	if (!ret)
		goto out;

	/* create valid object */
	date = g_date_new_dmy (d, m, y);
out:
	return date;
}

/**
 * pk_iso8601_to_datetime: (skip)
 * @iso_date: The ISO8601 date to convert
 *
 * Convert an ISO8601 date string to a #GDateTime.
 *
 * Return value: If valid then a new %GDateTime, else NULL
 *
 * Since: 0.8.11
 **/
GDateTime *
pk_iso8601_to_datetime (const gchar *iso_date)
{
	gboolean ret = FALSE;
	guint retval;
	guint d = 0;
	guint m = 0;
	guint y = 0;
	GTimeVal time_val;
	GDateTime *date = NULL;

	if (iso_date == NULL || iso_date[0] == '\0')
		goto out;

	/* try to parse complete ISO8601 date */
	if (g_strstr_len (iso_date, -1, " ") != NULL)
		ret = g_time_val_from_iso8601 (iso_date, &time_val);
	if (ret && time_val.tv_sec != 0) {
		g_debug ("Parsed %s %i", iso_date, ret);
		date = g_date_time_new_from_timeval_utc (&time_val);
		goto out;
	}

	/* g_time_val_from_iso8601() blows goats and won't
	 * accept a valid ISO8601 formatted date without a
	 * time value - try and parse this case */
	retval = sscanf (iso_date, "%u-%u-%u", &y, &m, &d);
	if (retval != 3)
		goto out;

	/* create valid object */
	date = g_date_time_new_utc (y, m, d, 0, 0, 0);
out:
	return date;
}

/**
 * pk_ptr_array_to_strv:
 * @array: (element-type utf8): the GPtrArray of strings
 *
 * Form a composite string array of strings.
 * The data in the GPtrArray is copied.
 *
 * Return value: (transfer full) (array zero-terminated=1): the string array, or %NULL if invalid
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
	for (i = 0; i < array->len; i++) {
		value_temp = (const gchar *) g_ptr_array_index (array, i);
		value[i] = g_strdup (value_temp);
	}

	return value;
}

/**
 * pk_get_machine_type:
 *
 * Get the machine type for the current host, e.g. "i386"
 * Note: Don't use this function if you can get this data from /etc/foo
 *
 * Return value: a machine type
 **/
static gchar *
pk_get_distro_id_machine_type (void)
{
	gint retval;
	struct utsname buf;

	retval = uname (&buf);
	if (retval != 0)
		return g_strdup ("unknown");
	return g_strdup (buf.machine);
}

/*
 * pk_parse_os_release:
 *
 * Internal helper to parse os-release
 **/
static gboolean
pk_parse_os_release (gchar **id, gchar **name, gchar **version_id, GError **error)
{
	const gchar *filename = "/etc/os-release";
	gboolean ret;
	g_autofree gchar *contents = NULL;
	g_autoptr(GKeyFile) key_file = NULL;
	g_autoptr(GString) str = NULL;

	/* load data */
	if (!g_file_test (filename, G_FILE_TEST_EXISTS))
		filename = "/usr/lib/os-release";
	if (!g_file_get_contents (filename, &contents, NULL, error))
		return FALSE;

	/* make a valid GKeyFile from the .ini data by prepending a header */
	str = g_string_new (contents);
	g_string_prepend (str, "[os-release]\n");
	key_file = g_key_file_new ();
	ret = g_key_file_load_from_data (key_file, str->str, -1, G_KEY_FILE_NONE, error);
	if (!ret) {
		return FALSE;
	}

	/* get keys */
	if (id != NULL) {
		g_autofree gchar *tmp = g_key_file_get_string (key_file, "os-release", "ID", error);
		if (tmp == NULL)
			return FALSE;
		*id = g_shell_unquote (tmp, error);
		if (*id == NULL)
			return FALSE;
	}
	if (name != NULL) {
		g_autofree gchar *tmp = g_key_file_get_string (key_file, "os-release", "NAME", error);
		if (tmp == NULL)
			return FALSE;
		*name = g_shell_unquote (tmp, error);
		if (*name == NULL)
			return FALSE;
	}
	if (version_id != NULL) {
		g_autofree gchar *tmp = g_key_file_get_string (key_file, "os-release", "VERSION_ID", error);
		if (tmp == NULL)
			return FALSE;
		*version_id = g_shell_unquote (tmp, error);
		if (*version_id == NULL)
			return FALSE;
	}
	return TRUE;
}

/**
 * pk_get_distro_id:
 *
 * Get the distribution ID for the current host, typically "distro;version;arch"
 *
 * Return value: a distribution ID
 **/
gchar *
pk_get_distro_id (void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *arch = NULL;
	g_autofree gchar *id = NULL;
	g_autofree gchar *version = NULL;

	/* we don't want distro specific results in 'make check' */
	if (g_getenv ("PK_SELF_TEST") != NULL)
		return g_strdup ("selftest;11.91;i686");

	ret = pk_parse_os_release (&id, NULL, &version, &error);
	if (!ret) {
		g_warning ("failed to load os-release: %s", error->message);
		return NULL;
	}

	arch = pk_get_distro_id_machine_type ();
	return g_strdup_printf ("%s;%s;%s", id, version, arch);
}

/**
 * pk_get_distro_name:
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the distribution name for this host as specified by NAME in /etc/os-release, e.g. "Fedora",
 *
 * Return value: a distro name
 **/
gchar *
pk_get_distro_name (GError **error)
{
	gboolean ret;
	gchar *name = NULL;

	ret = pk_parse_os_release (NULL, &name, NULL, error);
	if (!ret)
		return NULL;

	return name;
}

/**
 * pk_get_distro_version_id:
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the distribution version ID as specified by VERSION_ID in /etc/os-release, e.g. "23"
 *
 * Return value: a distribution version
 **/
gchar *
pk_get_distro_version_id (GError **error)
{
	gboolean ret;
	gchar *version_id = NULL;

	ret = pk_parse_os_release (NULL, NULL, &version_id, error);
	if (!ret)
		return NULL;

	return version_id;
}
