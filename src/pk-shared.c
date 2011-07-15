/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:pk-common
 * @short_description: Common utility functions for PackageKit
 *
 * This file contains functions that may be useful.
 */

#include "config.h"

#include <glib.h>
#include <glib/gstdio.h>

#include "pk-shared.h"

/**
 * pk_directory_remove_contents:
 *
 * Does not remove the directory itself, only the contents.
 **/
gboolean
pk_directory_remove_contents (const gchar *directory)
{
	gboolean ret = FALSE;
	GDir *dir;
	GError *error = NULL;
	const gchar *filename;
	gchar *src;
	gint retval;

	/* try to open */
	dir = g_dir_open (directory, 0, &error);
	if (dir == NULL) {
		g_warning ("cannot open directory: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find each */
	while ((filename = g_dir_read_name (dir))) {
		src = g_build_filename (directory, filename, NULL);
		ret = g_file_test (src, G_FILE_TEST_IS_DIR);
		if (ret) {
			g_debug ("directory %s found in %s, deleting", filename, directory);
			/* recurse, but should be only 1 level deep */
			pk_directory_remove_contents (src);
			retval = g_remove (src);
			if (retval != 0)
				g_warning ("failed to delete %s", src);
		} else {
			g_debug ("file found in %s, deleting", directory);
			retval = g_unlink (src);
			if (retval != 0)
				g_warning ("failed to delete %s", src);
		}
		g_free (src);
	}
	g_dir_close (dir);
	ret = TRUE;
out:
	return ret;
}


/**
 * pk_load_introspection:
 **/
GDBusNodeInfo *
pk_load_introspection (const gchar *filename, GError **error)
{
	gboolean ret;
	gchar *data = NULL;
	GDBusNodeInfo *info = NULL;
	GFile *file;

	/* load file */
	file = g_file_new_for_path (filename);
	ret = g_file_load_contents (file, NULL, &data,
				    NULL, NULL, error);
	if (!ret)
		goto out;

	/* build introspection from XML */
	info = g_dbus_node_info_new_for_xml (data, error);
	if (info == NULL)
		goto out;
out:
	g_object_unref (file);
	g_free (data);
	return info;
}

/**
 * pk_hint_enum_to_string:
 **/
const gchar *
pk_hint_enum_to_string (PkHintEnum hint)
{
	if (hint == PK_HINT_ENUM_FALSE)
		return "false";
	if (hint == PK_HINT_ENUM_TRUE)
		return "true";
	if (hint == PK_HINT_ENUM_UNSET)
		return "unset";
	return NULL;
}

/**
 * pk_hint_enum_from_string:
 **/
PkHintEnum
pk_hint_enum_from_string (const gchar *hint)
{
	if (g_strcmp0 (hint, "false") == 0)
		return PK_HINT_ENUM_FALSE;
	if (g_strcmp0 (hint, "true") == 0)
		return PK_HINT_ENUM_TRUE;
	if (g_strcmp0 (hint, "unset") == 0)
		return PK_HINT_ENUM_UNSET;
	return PK_HINT_ENUM_UNSET;
}


/**
 * pk_strtoint:
 * @text: The text the convert
 * @value: The return numeric return value
 *
 * Converts a string into a signed integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
pk_strtoint (const gchar *text, gint *value)
{
	gchar *endptr = NULL;
	gint64 value_raw;

	/* invalid */
	if (text == NULL)
		return FALSE;

	/* parse */
	value_raw = g_ascii_strtoll (text, &endptr, 10);

	/* parsing error */
	if (endptr == text)
		return FALSE;

	/* out of range */
	if (value_raw > G_MAXINT || value_raw < G_MININT)
		return FALSE;

	/* cast back down to value */
	*value = (gint) value_raw;
	return TRUE;
}

/**
 * pk_strtouint64:
 * @text: The text the convert
 * @value: The return numeric return value
 *
 * Converts a string into a unsigned integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
pk_strtouint64 (const gchar *text, guint64 *value)
{
	gchar *endptr = NULL;

	/* invalid */
	if (text == NULL)
		return FALSE;

	/* parse */
	*value = g_ascii_strtoull (text, &endptr, 10);
	if (endptr == text)
		return FALSE;

	return TRUE;
}

/**
 * pk_strtouint:
 * @text: The text the convert
 * @value: The return numeric return value
 *
 * Converts a string into a unsigned integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
pk_strtouint (const gchar *text, guint *value)
{
	gboolean ret;
	guint64 value_raw;

	ret = pk_strtouint64 (text, &value_raw);
	if (!ret)
		return FALSE;

	/* out of range */
	if (value_raw > G_MAXUINT)
		return FALSE;

	/* cast back down to value */
	*value = (guint) value_raw;
	return TRUE;
}

/**
 * pk_strzero:
 * @text: The text to check
 *
 * This function is a much safer way of doing "if (strlen (text) == 0))"
 * as it does not rely on text being NULL terminated. It's also much
 * quicker as it only checks the first byte rather than scanning the whole
 * string just to verify it's not zero length.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
pk_strzero (const gchar *text)
{
	if (text == NULL)
		return TRUE;
	if (text[0] == '\0')
		return TRUE;
	return FALSE;
}

/**
 * pk_strlen:
 * @text: The text to check
 * @len: The maximum length of the string
 *
 * This function is a much safer way of doing strlen as it checks for NULL and
 * a stupidly long string.
 *
 * Return value: the length of the string, or len if the string is too long.
 **/
guint
pk_strlen (const gchar *text, guint len)
{
	guint i;

	/* common case */
	if (text == NULL || text[0] == '\0')
		return 0;

	/* only count up to len */
	for (i=1; i<len; i++) {
		if (text[i] == '\0')
			break;
	}
	return i;
}
