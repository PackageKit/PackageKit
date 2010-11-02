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

#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/stat.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib.h>

#include "egg-string.h"

/**
 * egg_strtoint:
 * @text: The text the convert
 * @value: The return numeric return value
 *
 * Converts a string into a signed integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
egg_strtoint (const gchar *text, gint *value)
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
 * egg_strtouint:
 * @text: The text the convert
 * @value: The return numeric return value
 *
 * Converts a string into a unsigned integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
egg_strtouint (const gchar *text, guint *value)
{
	gchar *endptr = NULL;
	guint64 value_raw;

	/* invalid */
	if (text == NULL)
		return FALSE;

	/* parse */
	value_raw = g_ascii_strtoull (text, &endptr, 10);

	/* parsing error */
	if (endptr == text)
		return FALSE;

	/* out of range */
	if (value_raw > G_MAXUINT)
		return FALSE;

	/* cast back down to value */
	*value = (guint) value_raw;
	return TRUE;
}

/**
 * egg_strzero:
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
egg_strzero (const gchar *text)
{
	if (text == NULL)
		return TRUE;
	if (text[0] == '\0')
		return TRUE;
	return FALSE;
}

/**
 * egg_strlen:
 * @text: The text to check
 * @len: The maximum length of the string
 *
 * This function is a much safer way of doing strlen as it checks for NULL and
 * a stupidly long string.
 *
 * Return value: the length of the string, or len if the string is too long.
 **/
guint
egg_strlen (const gchar *text, guint len)
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

/**
 * egg_strvequal:
 * @id1: the first item of text to test
 * @id2: the second item of text to test
 *
 * This function will check to see if the GStrv arrays are string equal
 *
 * Return value: %TRUE if the arrays are the same, or are both %NULL
 **/
gboolean
egg_strvequal (gchar **id1, gchar **id2)
{
	guint i;
	guint length1;
	guint length2;

	if (id1 == NULL && id2 == NULL)
		return TRUE;

	if (id1 == NULL || id2 == NULL) {
		g_debug ("GStrv compare invalid '%p' and '%p'", id1, id2);
		return FALSE;
	}

	/* check different sizes */
	length1 = g_strv_length (id1);
	length2 = g_strv_length (id2);
	if (length1 != length2)
		return FALSE;

	/* text equal each one */
	for (i=0; i<length1; i++) {
		if (g_strcmp0 (id1[i], id2[i]) != 0)
			return FALSE;
	}

	return TRUE;
}

/**
 * egg_strreplace:
 * @text: The input text to make safe
 * @find: What to search for
 * @replace: What to replace with
 *
 * Replaces chars in the text with a replacement.
 * The %find and %replace variables to not have to be of the same length
 *
 * Return value: the new string (copied)
 **/
gchar *
egg_strreplace (const gchar *text, const gchar *find, const gchar *replace)
{
	gchar **array;
	gchar *retval;

	/* common case, not found */
	if (g_strstr_len (text, -1, find) == NULL) {
		return g_strdup (text);
	}

	/* split apart and rejoin with new delimiter */
	array = g_strsplit (text, find, 0);
	retval = g_strjoinv (replace, array);
	g_strfreev (array);
	return retval;
}

