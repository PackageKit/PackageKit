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
#include <sys/utsname.h>

#include <glib.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>

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
	if (ret && time_val.tv_sec != 0) {
		date = g_date_new ();
		g_date_set_time_val (date, &time_val);
		goto out;
	}

	/* g_time_val_from_iso8601() blows goats and won't
	 * accept a valid ISO8601 formatted date without a
	 * time value - try and parse this case */
	retval = sscanf (iso_date, "%u-%u-%u", &y, &m, &d);
	if (retval != 3) {
		g_warning ("could not parse date '%s'", iso_date);
		goto out;
	}

	/* check it's valid */
	ret = g_date_valid_dmy (d, m, y);
	if (!ret) {
		g_warning ("invalid date %i/%i/%i from '%s'", y, m, d, iso_date);
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
 * Return value: (transfer full): the string array, or %NULL if invalid
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

/**
 * pk_get_os_release:
 *
 * Return value: The current OS release, e.g. "7.2-RELEASE"
 * Note: Don't use this function if you can get this data from /etc/foo
 **/
static gchar *
pk_get_distro_id_os_release (void)
{
	gint retval;
	struct utsname buf;

	retval = uname (&buf);
	if (retval != 0)
		return g_strdup ("unknown");
	return g_strdup (buf.release);
}

/**
 * pk_get_machine_type:
 *
 * Return value: The current machine ID, e.g. "i386"
 * Note: Don't use this function if you can get this data from /etc/foo
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

/**
 * pk_get_distro_id:
 *
 * Return value: the distro-id, typically "distro;version;arch"
 **/
gchar *
pk_get_distro_id (void)
{
	guint i;
	gboolean ret;
	gchar *contents = NULL;
	gchar *arch = NULL;
	gchar *version = NULL;
	gchar **split = NULL;
	gchar *distro = NULL;
	gchar *distro_id = NULL;

	/* The distro id property should have the
	   format "distro;version;arch" as this is
	   used to determine if a specific package
	   can be installed on a certain machine.
	   For instance, x86_64 packages cannot be
	   installed on a i386 machine.
	*/

	/* we can't get arch from /etc */
	arch = pk_get_distro_id_machine_type ();

	/* check for fedora */
	ret = g_file_get_contents ("/etc/fedora-release", &contents, NULL, NULL);
	if (ret) {
		/* Fedora release 8.92 (Rawhide) */
		split = g_strsplit (contents, " ", 0);
		if (split == NULL)
			goto out;

		/* complete! */
		distro_id = g_strdup_printf ("fedora;%s;%s", split[2], arch);
		goto out;
	}

	/* check for suse */
	ret = g_file_get_contents ("/etc/SuSE-release", &contents, NULL, NULL);
	if (ret) {
		/* replace with spaces: openSUSE 11.0 (i586) Alpha3\nVERSION = 11.0 */
		g_strdelimit (contents, "()\n", ' ');

		/* openSUSE 11.0  i586  Alpha3 VERSION = 11.0 */
		split = g_strsplit (contents, " ", 0);
		if (split == NULL)
			goto out;

		/* complete! */
		distro_id = g_strdup_printf ("suse;%s-%s;%s", split[1], split[3], arch);
		goto out;
	}

	/* check for meego */
	ret = g_file_get_contents ("/etc/meego-release", &contents, NULL, NULL);
	if (ret) {
		/* Meego release 1.0 (MeeGo) */
		split = g_strsplit (contents, " ", 0);
		if (split == NULL)
			goto out;

		/* complete! */
		distro_id = g_strdup_printf ("meego;%s;%s", split[2], arch);
		goto out;
	}

	/* check for foresight or foresight derivatives */
	ret = g_file_get_contents ("/etc/distro-release", &contents, NULL, NULL);
	if (ret) {
		/* Foresight Linux 2 */
		split = g_strsplit (contents, " ", 0);
		if (split == NULL)
			goto out;

		/* complete! */
		distro_id = g_strdup_printf ("foresight;%s;%s", split[2], arch);
		goto out;
	}

	/* check for PLD */
	ret = g_file_get_contents ("/etc/pld-release", &contents, NULL, NULL);
	if (ret) {
		/* 2.99 PLD Linux (Th) */
		split = g_strsplit (contents, " ", 0);
		if (split == NULL)
			goto out;

		/* complete! */
		distro_id = g_strdup_printf ("pld;%s;%s", split[0], arch);
		goto out;
	}

	/* check for Arch */
	ret = g_file_test ("/etc/arch-release", G_FILE_TEST_EXISTS);
	if (ret) {
		/* complete! */
		distro_id = g_strdup_printf ("arch;current;%s", arch);
		goto out;
	}

	/* check for LSB */
	ret = g_file_get_contents ("/etc/lsb-release", &contents, NULL, NULL);
	if (ret) {
		/* split by lines */
		split = g_strsplit (contents, "\n", -1);
		for (i=0; split[i] != NULL; i++) {
			if (g_str_has_prefix (split[i], "DISTRIB_ID="))
				distro = g_ascii_strdown (&split[i][11], -1);
			if (g_str_has_prefix (split[i], "DISTRIB_RELEASE="))
				version = g_ascii_strdown (&split[i][16], -1);
		}

		/* complete! */
		distro_id = g_strdup_printf ("%s;%s;%s", distro, version, arch);
		goto out;
	}

	/* check for Debian or Debian derivatives */
	ret = g_file_get_contents ("/etc/debian_version", &contents, NULL, NULL);
	if (ret) {
		/* remove "\n": "squeeze/sid\n" */
		g_strdelimit (contents, "\n", '\0');
		/* removes leading and trailing whitespace */
		g_strstrip (contents);

		/* complete! */
		distro_id = g_strdup_printf ("debian;%s;%s", contents, arch);
		goto out;
	}

#ifdef __FreeBSD__
	ret = TRUE;
#endif
	/* FreeBSD */
	if (ret) {
		/* we can't get version from /etc */
		version = pk_get_distro_id_os_release ();
		if (version == NULL)
			goto out;

		/* 7.2-RELEASE */
		split = g_strsplit (version, "-", 0);
		if (split == NULL)
			goto out;

		/* complete! */
		distro_id = g_strdup_printf ("freebsd;%s;%s", split[0], arch);
		goto out;
	}
out:
	g_strfreev (split);
	g_free (version);
	g_free (distro);
	g_free (arch);
	g_free (contents);
	return distro_id;
}
