/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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
 * SECTION:pk-package-id
 * @short_description: Functionality to read a PackageID
 */

#include "config.h"

#include <glib.h>

#include "src/pk-cleanup.h"

#include <packagekit-glib2/pk-package-id.h>

/**
 * pk_package_id_split:
 * @package_id: the ; delimited PackageID to split
 *
 * Splits a PackageID into the correct number of parts, checking the correct
 * number of delimiters are present.
 *
 * Return value: (transfer full): a GStrv or %NULL if invalid, use g_strfreev() to free
 *
 * Since: 0.5.3
 **/
gchar **
pk_package_id_split (const gchar *package_id)
{
	gchar **sections = NULL;

	if (package_id == NULL)
		goto out;

	/* split by delimeter ';' */
	sections = g_strsplit (package_id, ";", -1);
	if (g_strv_length (sections) != 4)
		goto out;

	/* name has to be valid */
	if (sections[0][0] != '\0')
		return sections;
out:
	g_strfreev (sections);
	return NULL;
}

/**
 * pk_package_id_check:
 * @package_id: the PackageID to check
 *
 * Return value: %TRUE if the PackageID was well formed.
 *
 * Since: 0.5.0
 **/
gboolean
pk_package_id_check (const gchar *package_id)
{
	_cleanup_strv_free_ gchar **sections = NULL;
	gboolean ret;

	/* NULL check */
	if (package_id == NULL)
		return FALSE;

	/* UTF8 */
	ret = g_utf8_validate (package_id, -1, NULL);
	if (!ret)
		return FALSE;

	/* correct number of sections */
	sections = pk_package_id_split (package_id);
	if (sections == NULL)
		return FALSE;
	return TRUE;
}

/**
 * pk_package_id_build:
 * @name: the package name
 * @version: the package version
 * @arch: the package architecture
 * @data: the package extra data
 *
 * Return value: returns a string to form the PackageID.
 *
 * Since: 0.5.0
 **/
gchar *
pk_package_id_build (const gchar *name, const gchar *version,
		     const gchar *arch, const gchar *data)
{
	g_return_val_if_fail (name != NULL, NULL);
	return g_strjoin (";",
			  name,
			  version != NULL ? version : "",
			  arch != NULL ? arch : "",
			  data != NULL ? data : "",
			  NULL);
}

/**
 * pk_arch_base_ix86:
 **/
static gboolean
pk_arch_base_ix86 (const gchar *arch)
{
	if (g_strcmp0 (arch, "i386") == 0 ||
	    g_strcmp0 (arch, "i486") == 0 ||
	    g_strcmp0 (arch, "i586") == 0 ||
	    g_strcmp0 (arch, "i686") == 0)
		return TRUE;
	return FALSE;
}

/**
 * pk_package_id_equal_fuzzy_arch_section:
 **/
static gboolean
pk_package_id_equal_fuzzy_arch_section (const gchar *arch1, const gchar *arch2)
{
	if (g_strcmp0 (arch1, arch2) == 0)
		return TRUE;
	if (pk_arch_base_ix86 (arch1) && pk_arch_base_ix86 (arch2))
		return TRUE;
	return FALSE;
}

/**
 * pk_package_id_equal_fuzzy_arch:
 * @package_id1: the first PackageID
 * @package_id2: the second PackageID
 *
 * Only compare the name, version, and arch, where the architecture will fuzzy
 * match with i*86.
 *
 * Return value: %TRUE if the PackageIDs can be considered equal.
 *
 * Since: 0.5.0
 **/
gboolean
pk_package_id_equal_fuzzy_arch (const gchar *package_id1, const gchar *package_id2)
{
	_cleanup_strv_free_ gchar **sections1 = NULL;
	_cleanup_strv_free_ gchar **sections2 = NULL;

	sections1 = pk_package_id_split (package_id1);
	sections2 = pk_package_id_split (package_id2);
	if (g_strcmp0 (sections1[0], sections2[0]) == 0 &&
	    g_strcmp0 (sections1[1], sections2[1]) == 0 &&
	    pk_package_id_equal_fuzzy_arch_section (sections1[2], sections2[2]))
		return TRUE;
	return FALSE;
}

/**
 * pk_package_id_to_printable:
 * @package_id: the PackageID
 *
 * Formats the PackageID to be printable to the user.
 *
 * Return value: the name-version.arch formatted string, use g_free() to free.
 *
 * Since: 0.5.2
 **/
gchar *
pk_package_id_to_printable (const gchar *package_id)
{
	_cleanup_strv_free_  gchar **parts = NULL;
	gchar *value = NULL;
	GString *string = NULL;

	/* invalid */
	if (package_id == NULL)
		goto out;

	/* split */
	parts = pk_package_id_split (package_id);
	if (parts == NULL)
		goto out;

	/* name */
	string = g_string_new (parts[PK_PACKAGE_ID_NAME]);

	/* version if present */
	if (parts[PK_PACKAGE_ID_VERSION][0] != '\0')
		g_string_append_printf (string, "-%s", parts[PK_PACKAGE_ID_VERSION]);

	/* arch if present */
	if (parts[PK_PACKAGE_ID_ARCH][0] != '\0')
		g_string_append_printf (string, ".%s", parts[PK_PACKAGE_ID_ARCH]);
out:
	if (string != NULL)
		value = g_string_free (string, FALSE);
	return value;
}
