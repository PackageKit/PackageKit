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
		egg_debug ("GStrv compare invalid '%p' and '%p'", id1, id2);
		return FALSE;
	}

	/* check different sizes */
	length1 = g_strv_length (id1);
	length2 = g_strv_length (id2);
	if (length1 != length2)
		return FALSE;

	/* text equal each one */
	for (i=0; i<length1; i++) {
		if (!egg_strequal (id1[i], id2[i]))
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
	if (strstr (text, find) == NULL) {
		return g_strdup (text);
	}

	/* split apart and rejoin with new delimiter */
	array = g_strsplit (text, find, 0);
	retval = g_strjoinv (replace, array);
	g_strfreev (array);
	return retval;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_string_test (EggTest *test)
{
	gboolean ret;
	gchar *text_safe;
	const gchar *temp;
	guint length;
	gint value;
	guint uvalue;
	gchar **id1;
	gchar **id2;

	if (!egg_test_start (test, "EggString"))
		return;

	/************************************************************
	 ****************       String equal       ******************
	 ************************************************************/
	egg_test_title (test, "egg_strequal same argument");
	temp = "dave";
	if (egg_strequal (temp, temp))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect ret when both same");

	/************************************************************/
	egg_test_title (test, "egg_strequal both const");
	if (egg_strequal ("dave", "dave"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect ret when both same");

	/************************************************************
	 ****************    String array equal    ******************
	 ************************************************************/
	egg_test_title (test, "egg_strvequal same argument");
	id1 = g_strsplit ("the quick brown fox", " ", 0);
	if (egg_strvequal (id1, id1))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect ret when both same");
	g_strfreev (id1);

	/************************************************************/
	egg_test_title (test, "egg_strvequal same");
	id1 = g_strsplit ("the quick brown fox", " ", 0);
	id2 = g_strsplit ("the quick brown fox", " ", 0);
	if (egg_strvequal (id1, id2))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect ret when both same");
	g_strfreev (id1);
	g_strfreev (id2);

	/************************************************************/
	egg_test_title (test, "egg_strvequal different lengths");
	id1 = g_strsplit ("the quick brown", " ", 0);
	id2 = g_strsplit ("the quick brown fox", " ", 0);
	if (!egg_strvequal (id1, id2))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect ret when both same");
	g_strfreev (id1);
	g_strfreev (id2);

	/************************************************************/
	egg_test_title (test, "egg_strvequal different");
	id1 = g_strsplit ("the quick brown fox", " ", 0);
	id2 = g_strsplit ("richard hughes maintainer dude", " ", 0);
	if (!egg_strvequal (id1, id2))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "same when different");
	g_strfreev (id1);
	g_strfreev (id2);

	/************************************************************
	 ****************          Zero            ******************
	 ************************************************************/
	temp = NULL;
	egg_test_title (test, "test strzero (null)");
	ret = egg_strzero (NULL);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed null");

	/************************************************************/
	egg_test_title (test, "test strzero (null first char)");
	ret = egg_strzero ("");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed null");

	/************************************************************/
	egg_test_title (test, "test strzero (long string)");
	ret = egg_strzero ("Richard");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "zero length word!");

	/************************************************************/
	egg_test_title (test, "id strcmp pass");
	ret = egg_strequal ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;fedora");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "id strcmp fail");
	ret = egg_strequal ("moo;0.0.1;i386;fedora", "moo;0.0.2;i386;fedora");
	egg_test_assert (test, !ret);

	/************************************************************
	 ****************          strlen          ******************
	 ************************************************************/
	egg_test_title (test, "strlen bigger");
	length = egg_strlen ("123456789", 20);
	if (length == 9)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the strlen %i", length);

	/************************************************************/
	egg_test_title (test, "strlen smaller");
	length = egg_strlen ("123456789", 5);
	if (length == 5)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the strlen %i", length);

	/************************************************************/
	egg_test_title (test, "strlen correct");
	length = egg_strlen ("123456789", 9);
	if (length == 9)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the strlen %i", length);

	/************************************************************
	 ****************         Replace          ******************
	 ************************************************************/
	egg_test_title (test, "replace start");
	text_safe = egg_strreplace ("richard\nhughes", "r", "e");
	if (egg_strequal (text_safe, "eichaed\nhughes"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the replace '%s'", text_safe);
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "replace none");
	text_safe = egg_strreplace ("richard\nhughes", "dave", "e");
	if (egg_strequal (text_safe, "richard\nhughes"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the replace '%s'", text_safe);
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "replace end");
	text_safe = egg_strreplace ("richard\nhughes", "s", "e");
	if (egg_strequal (text_safe, "richard\nhughee"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the replace '%s'", text_safe);
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "replace unicode");
	text_safe = egg_strreplace ("richard\n- hughes", "\n- ", "\n• ");
	if (egg_strequal (text_safe, "richard\n• hughes"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the replace '%s'", text_safe);
	g_free (text_safe);

	/************************************************************
	 **************       Check for numbers      ****************
	 ************************************************************/
	egg_test_title (test, "check number valid");
	ret = egg_strnumber ("123");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "check number valid");
	ret = egg_strnumber ("-123");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "check number zero");
	ret = egg_strnumber ("0");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "check number oversize");
	ret = egg_strnumber ("123456891234");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "check number NULL");
	ret = egg_strnumber (NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "check number blank");
	ret = egg_strnumber ("");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "check number not negative");
	ret = egg_strnumber ("503-");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "check number positive");
	ret = egg_strnumber ("+503");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "check number random chars");
	ret = egg_strnumber ("dave");
	egg_test_assert (test, !ret);

	/************************************************************
	 **************        Convert numbers       ****************
	 ************************************************************/
	egg_test_title (test, "convert valid number");
	ret = egg_strtoint ("234", &value);
	if (ret && value == 234)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "value is %i", value);

	/************************************************************/
	egg_test_title (test, "convert negative valid number");
	ret = egg_strtoint ("-234", &value);
	if (ret && value == -234)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "value is %i", value);

	/************************************************************/
	egg_test_title (test, "don't convert invalid number");
	ret = egg_strtoint ("dave", &value);
	if (ret == FALSE && value == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "value is %i", value);

	/************************************************************/
	egg_test_title (test, "convert valid uint number");
	ret = egg_strtouint ("234", &uvalue);
	if (ret && uvalue == 234)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "value is %i", uvalue);

	/************************************************************/
	egg_test_title (test, "convert invalid uint number");
	ret = egg_strtouint ("-234", &uvalue);
	if (ret == FALSE && uvalue == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "value is %i", uvalue);

	egg_test_end (test);
}
#endif

