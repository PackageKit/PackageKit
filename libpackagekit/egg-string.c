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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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

#include "egg-debug.h"
#include "egg-string.h"

/**
 * egg_strnumber:
 * @text: The text the validate
 *
 * Tests a string to see if it is a number. Both positive and negative numbers
 * are allowed.
 *
 * Return value: %TRUE if the string represents a numeric value
 **/
gboolean
egg_strnumber (const gchar *text)
{
	guint i;
	guint length;

	/* check explicitly */
	if (egg_strzero (text))
		return FALSE;

	/* max length is 10 */
	length = egg_strlen (text, 10);
	if (length == 10) {
		egg_warning ("input too long: %s", text);
		return FALSE;
	}

	for (i=0; i<length; i++) {
		if (i == 0 && text[i] == '-') {
			/* negative sign */
		} else if (g_ascii_isdigit (text[i]) == FALSE) {
			egg_warning ("not a number '%c' in text!", text[i]);
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * egg_strtoint:
 * @text: The text the convert
 * @value: The return numeric return value, or 0 if invalid.
 *
 * Converts a string into a signed integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
egg_strtoint (const gchar *text, gint *value)
{
	gboolean ret;
	ret = egg_strnumber (text);
	if (!ret) {
		*value = 0;
		return FALSE;
	}
	/* ITS4: ignore, we've already checked for validity */
	*value = atoi (text);
	return TRUE;
}

/**
 * egg_strtouint:
 * @text: The text the convert
 * @value: The return numeric return value, or 0 if invalid.
 *
 * Converts a string into a unsigned integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
egg_strtouint (const gchar *text, guint *value)
{
	gboolean ret;
	gint temp;
	ret = egg_strtoint (text, &temp);
	if (ret == FALSE || temp < 0) {
		*value = 0;
		return FALSE;
	}
	*value = (guint) temp;
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
	if (text == NULL || text[0] == '\0') {
		return 0;
	}

	/* only count up to len */
	for (i=1; i<len; i++) {
		if (text[i] == '\0')
			break;
	}
	return i;
}

/**
 * egg_strequal:
 * @id1: the first item of text to test
 * @id2: the second item of text to test
 *
 * This function is a much safer way of doing strcmp as it checks for
 * NULL first, and returns boolean TRUE, not zero for success.
 *
 * Return value: %TRUE if the string are both non-%NULL and the same.
 **/
gboolean
egg_strequal (const gchar *id1, const gchar *id2)
{
	if (id1 == NULL || id2 == NULL) {
		egg_debug ("string compare invalid '%s' and '%s'", id1, id2);
		return FALSE;
	}
	return (strcmp (id1, id2) == 0);
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
	if (strstr (text, find) == NULL) {
		return g_strdup (text);
	}

	/* split apart and rejoin with new delimiter */
	array = g_strsplit (text, find, 0);
	retval = g_strjoinv (replace, array);
	g_strfreev (array);
	return retval;
}

