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
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>

#include "pk-debug.h"
#include "pk-common.h"
#include "pk-enum.h"

/**
 * pk_get_machine_type:
 *
 * Return value: The current machine ID, e.g. "i386"
 * Note: Don't use this function if you can get this data from /etc/foo
 **/
static gchar *
pk_get_machine_type (void)
{
	gint retval;
	struct utsname buf;

	retval = uname (&buf);
	if (retval != 0) {
		return g_strdup ("unknown");
	}
	return g_strdup (buf.machine);
}

/**
 * pk_get_distro_id:
 *
 * Return value: The current distro-id, e.g. fedora-8-i386, or %NULL for an
 * error or not known
 **/
gchar *
pk_get_distro_id (void)
{
	gboolean ret;
	gchar *contents = NULL;
	gchar *distro = NULL;
	gchar *arch = NULL;
	gchar **split = NULL;

	/* check for fedora */
	ret = g_file_get_contents ("/etc/fedora-release", &contents, NULL, NULL);
	if (ret) {
		/* Fedora release 8.92 (Rawhide) */
		split = g_strsplit (contents, " ", 0);
		if (split == NULL)
			goto out;

		/* we can't get arch from /etc */
		arch = pk_get_machine_type ();
		if (arch == NULL)
			goto out;

		/* complete! */
		distro = g_strdup_printf ("fedora-%s-%s", split[2], arch);
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
		distro = g_strdup_printf ("suse-%s-%s", split[1], split[3]);
		goto out;
	}

	/* check for foresight */
	ret = g_file_get_contents ("/etc/distro-release", &contents, NULL, NULL);
	if (ret) {
		/* Foresight Linux 2.0.2 */
		split = g_strsplit (contents, " ", 0);
		if (split == NULL)
			goto out;

		/* we can't get arch from /etc */
		arch = pk_get_machine_type ();
		if (arch == NULL)
			goto out;

		/* complete! */
		distro = g_strdup_printf ("foresight-%s-%s", split[2], arch);
		goto out;
	}

out:
	g_strfreev (split);
	g_free (arch);
	g_free (contents);
	return distro;
}

/**
 * pk_iso8601_present:
 *
 * Return value: The current iso8601 date and time
 **/
gchar *
pk_iso8601_present (void)
{
	GTimeVal timeval;
	gchar *timespec;

	/* get current time */
	g_get_current_time (&timeval);
	timespec = g_time_val_to_iso8601 (&timeval);
	pk_debug ("timespec=%s", timespec);

	return timespec;
}

/**
 * pk_iso8601_difference:
 * @isodate: The ISO8601 date to compare
 *
 * Return value: The difference in seconds between the iso8601 date and current
 **/
guint
pk_iso8601_difference (const gchar *isodate)
{
	GTimeVal timeval_then;
	GTimeVal timeval_now;
	gboolean ret;
	guint time;

	g_return_val_if_fail (isodate != NULL, 0);

	/* convert date */
	ret = g_time_val_from_iso8601 (isodate, &timeval_then);
	if (!ret) {
		pk_warning ("failed to parse '%s'", isodate);
		return 0;
	}
	g_get_current_time (&timeval_now);

	/* work out difference */
	time = timeval_now.tv_sec - timeval_then.tv_sec;
	pk_debug ("difference=%i", time);

	return time;
}


/**
 * pk_strvalidate_char:
 * @item: A single char to test
 *
 * Tests a char to see if it may be dangerous.
 *
 * Return value: %TRUE if the char is valid
 **/
static gboolean
pk_strvalidate_char (gchar item)
{
	switch (item) {
	case '$':
	case '`':
	case '\'':
	case '"':
	case '^':
	case '[':
	case ']':
	case '{':
	case '}':
	case '\\':
	case '<':
	case '>':
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_strsafe:
 * @text: The input text to make safe
 *
 * Replaces chars in the text that may be dangerous, or that may print
 * incorrectly. These chars include new lines, tabs and quotes, and are
 * replaced by spaces.
 *
 * Return value: the new string with no insane chars
 **/
gchar *
pk_strsafe (const gchar *text)
{
	gchar *text_safe;
	gboolean ret;
	const gchar *delimiters;

	if (text == NULL) {
		return NULL;
	}

	/* is valid UTF8? */
	ret = g_utf8_validate (text, -1, NULL);
	if (!ret) {
		pk_warning ("text '%s' was not valid UTF8!", text);
		return NULL;
	}

	/* rip out any insane characters */
	delimiters = "\\\f\n\r\t\"'";
	text_safe = g_strdup (text);
	g_strdelimit (text_safe, delimiters, ' ');
	return text_safe;
}

/**
 * pk_strnumber:
 * @text: The text the validate
 *
 * Tests a string to see if it is a number. Both positive and negative numbers
 * are allowed.
 *
 * Return value: %TRUE if the string represents a numeric value
 **/
gboolean
pk_strnumber (const gchar *text)
{
	guint i;
	guint length;

	/* check explicitly */
	if (pk_strzero (text)) {
		return FALSE;
	}

	/* ITS4: ignore, not used for allocation and checked for oversize */
	length = strlen (text);
	for (i=0; i<length; i++) {
		if (i > 10) {
			pk_debug ("input too long!");
			return FALSE;
		}
		if (i == 0 && text[i] == '-') {
			/* negative sign */
		} else if (g_ascii_isdigit (text[i]) == FALSE) {
			pk_debug ("not a number '%c' in text!", text[i]);
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * pk_strtoint:
 * @text: The text the convert
 * @value: The return numeric return value, or 0 if invalid.
 *
 * Converts a string into a signed integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
pk_strtoint (const gchar *text, gint *value)
{
	gboolean ret;
	ret = pk_strnumber (text);
	if (!ret) {
		*value = 0;
		return FALSE;
	}
	/* ITS4: ignore, we've already checked for validity */
	*value = atoi (text);
	return TRUE;
}

/**
 * pk_strtouint:
 * @text: The text the convert
 * @value: The return numeric return value, or 0 if invalid.
 *
 * Converts a string into a unsigned integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
pk_strtouint (const gchar *text, guint *value)
{
	gboolean ret;
	gint temp;
	ret = pk_strtoint (text, &temp);
	if (ret == FALSE || temp < 0) {
		*value = 0;
		return FALSE;
	}
	*value = (guint) temp;
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
	if (text == NULL) {
		return TRUE;
	}
	if (text[0] == '\0') {
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_strlen:
 * @text: The text to check
 * @max_length: The maximum length of the string
 *
 * This function is a much safer way of doing strlen as it checks for NULL and
 * a stupidly long string.
 * This also modifies the string in place if it is over-range by inserting
 * a NULL at the max_length.
 *
 * Return value: the length of the string, or max_length.
 **/
guint
pk_strlen (gchar *text, guint max_length)
{
	guint length;
	/* ITS4: ignore, not used for allocation and checked */
	length = strlen (text);
	if (length > max_length) {
		text[max_length] = '\0';
		return max_length;
	}
	return length;
}

/**
 * pk_strvalidate:
 * @text: The text to check for validity
 *
 * Tests a string to see if it may be dangerous or invalid.
 *
 * Return value: %TRUE if the string is valid
 **/
gboolean
pk_strvalidate (const gchar *text)
{
	guint i;
	guint length;

	/* ITS4: ignore, not used for allocation and checked for oversize */
	length = strlen (text);
	for (i=0; i<length; i++) {
		if (i > 1024) {
			pk_debug ("input too long!");
			return FALSE;
		}
		if (pk_strvalidate_char (text[i]) == FALSE) {
			pk_debug ("invalid char '%c' in text!", text[i]);
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * pk_strsplit:
 * @id: the ; delimited string to split
 * @parts: how many parts the delimted string should be split into
 *
 * Splits a string into the correct number of parts, checking the correct
 * number of delimiters are present.
 *
 * Return value: a char array is split correctly, %NULL if invalid
 * Note: You need to use g_strfreev on the returned value
 **/
gchar **
pk_strsplit (const gchar *id, guint parts)
{
	gchar **sections = NULL;

	if (id == NULL) {
		pk_warning ("ident is null!");
		goto out;
	}

	/* split by delimeter ';' */
	sections = g_strsplit (id, ";", 0);
	if (g_strv_length (sections) != parts) {
		pk_warning ("ident '%s' is invalid (sections=%d)", id, g_strv_length (sections));
		goto out;
	}

	/* ITS4: ignore, not used for allocation */
	if (pk_strzero (sections[0])) {
		/* name has to be valid */
		pk_warning ("ident first section is empty");
		goto out;
	}

	/* all okay, phew.. */
	return sections;

out:
	/* free sections and return NULL */
	if (sections != NULL) {
		g_strfreev (sections);
	}
	return NULL;
}

/**
 * pk_strequal:
 * @id1: the first item of text to test
 * @id2: the second item of text to test
 *
 * This function is a much safer way of doing strcmp as it checks for
 * NULL first, and returns boolean TRUE, not zero for success.
 *
 * Return value: %TRUE if the string are both non-%NULL and the same.
 **/
gboolean
pk_strequal (const gchar *id1, const gchar *id2)
{
	if (id1 == NULL || id2 == NULL) {
		pk_debug ("string compare invalid '%s' and '%s'", id1, id2);
		return FALSE;
	}
	return (strcmp (id1, id2) == 0);
}

/**
 * pk_strcmp_sections:
 * @id1: the first item of text to test
 * @id2: the second item of text to test
 * @parts: the number of parts each id should have
 * @compare: the leading number of parts to compare
 *
 * We only want to compare some first sections, not all the data when
 * comparing package_id's and transaction_id's.
 *
 * Return value: %TRUE if the strings can be considered the same.
 *
 **/
gboolean
pk_strcmp_sections (const gchar *id1, const gchar *id2, guint parts, guint compare)
{
	gchar **sections1;
	gchar **sections2;
	gboolean ret = FALSE;
	guint i;

	if (id1 == NULL || id2 == NULL) {
		pk_warning ("package id compare invalid '%s' and '%s'", id1, id2);
		return FALSE;
	}
	if (compare > parts) {
		pk_warning ("compare %i > parts %i", compare, parts);
		return FALSE;
	}
	if (compare == parts) {
		pk_debug ("optimize to strcmp");
		return pk_strequal (id1, id2);
	}

	/* split, NULL will be returned if error */
	sections1 = pk_strsplit (id1, parts);
	sections2 = pk_strsplit (id2, parts);

	/* check we split okay */
	if (sections1 == NULL) {
		pk_warning ("string id compare sections1 invalid '%s'", id1);
		goto out;
	}
	if (sections2 == NULL) {
		pk_warning ("string id compare sections2 invalid '%s'", id2);
		goto out;
	}

	/* only compare preceeding sections */
	for (i=0; i<compare; i++) {
		if (pk_strequal (sections1[i], sections2[i]) == FALSE) {
			goto out;
		}
	}
	ret = TRUE;

out:
	g_strfreev (sections1);
	g_strfreev (sections2);
	return ret;
}

/**
 * pk_strpad:
 * @data: the input string
 * @length: the desired length of the output string, with padding
 *
 * Returns the text padded to a length with spaces. If the string is
 * longer than length then a longer string is returned.
 *
 * Return value: The padded string
 **/
gchar *
pk_strpad (const gchar *data, guint length)
{
	gint size;
	gchar *text;
	gchar *padding;

	if (data == NULL) {
		return g_strnfill (length, ' ');
	}

	/* ITS4: ignore, only used for formatting */
	size = (length - strlen(data));
	if (size <= 0) {
		return g_strdup (data);
	}

	padding = g_strnfill (size, ' ');
	text = g_strdup_printf ("%s%s", data, padding);
	g_free (padding);
	return text;
}

/**
 * pk_strpad_extra:
 * @data: the input string
 * @length: the desired length of the output string, with padding
 * @extra: if we are running with a deficit, we might have a positive offset
 *
 * This function pads a string, but allows a follow-on value. This is useful
 * if the function is being used to print columns of text, and one oversize
 * one has to be absorbed into the next where possible.
 *
 * Return value: The padded string
 **/
gchar *
pk_strpad_extra (const gchar *data, guint length, guint *extra)
{
	gint size;
	gchar *text;

	/* can we just do the simple version? */
	if (data == NULL || extra == NULL) {
		return pk_strpad (data, length);
	}

	/* work out what we want to do */
	size = length - *extra;

	if (size < 0) {
		size = 0;
	}

	/* do the padding */
	text = pk_strpad (data, size);

	/* ITS4: ignore, we know pk_strpad is null terminated */
	*extra = strlen (text) - size;

	return text;
}

/**
 * pk_delay_yield:
 * @delay: the desired delay in seconds
 *
 * Return value: success
 **/
gboolean
pk_delay_yield (gfloat delay)
{
	GTimer *timer;
	gdouble elapsed;
	guint count = 0;

	pk_debug ("started task");
	timer = g_timer_new ();
	do {
		g_usleep (10);
		g_thread_yield ();
		elapsed = g_timer_elapsed (timer, NULL);
		if (++count % 10000 == 0) {
			pk_debug ("elapsed %.2f", elapsed);
		}
	} while (elapsed < delay);
	g_timer_destroy (timer);
	return TRUE;
}

/**
 * pk_strbuild_va:
 * @first_element: The first string item, or NULL
 * @args: the va_list
 *
 * This function converts a va_list into a string in a safe and efficient way,
 * e.g. pk_strbuild_va("foo","bar","baz") == "foo bar baz"
 *
 * Return value: the single string
 **/
gchar *
pk_strbuild_va (const gchar *first_element, va_list *args)
{
	const gchar *element;
	GString *string;

	/* shortcut */
	if (pk_strzero (first_element)) {
		return NULL;
	}

	/* set the first entry and a space */
	string = g_string_new (first_element);
	g_string_append_c (string, ' ');

	/* do all elements */
	while (TRUE) {
		element = va_arg (*args, const gchar *);

		/* are we at the end? Is this safe? */
		if (element == NULL) {
			break;
		}

		/* Ignore empty elements */
		if (*element == '\0') {
			continue;
		}

		g_string_append (string, element);
		g_string_append_c (string, ' ');
	}

	/* remove last char */
	g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

static gchar *
pk_strbuild_test (const gchar *first_element, ...)
{
	va_list args;
	gchar *text;

	/* get the argument list */
	va_start (args, first_element);
	text = pk_strbuild_va (first_element, &args);
	va_end (args);

	return text;
}

void
libst_common (LibSelfTest *test)
{
	gboolean ret;
	gchar **array;
	gchar *text_safe;
	const gchar *temp;
	guint length;
	gint value;
	guint uvalue;
	gchar *present;
	guint seconds;

	if (libst_start (test, "PkCommon", CLASS_AUTO) == FALSE) {
		return;
	}

	pk_delay_yield (2.0);


	/************************************************************
	 ****************        test distro-id        **************
	 ************************************************************/
	libst_title (test, "get distro id");
	text_safe = pk_get_distro_id ();
	if (text_safe != NULL) {
		libst_success (test, "distro_id=%s", text_safe);
	} else {
		libst_failed (test, NULL);
	}
	g_free (text_safe);

	/************************************************************
	 ****************        build var args        **************
	 ************************************************************/
	libst_title (test, "build_va NULL");
	text_safe = pk_strbuild_test (NULL);
	if (text_safe == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "build_va blank");
	text_safe = pk_strbuild_test ("", NULL);
	if (text_safe == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "incorrect ret '%s'", text_safe);
	}

	/************************************************************/
	libst_title (test, "build_va single");
	text_safe = pk_strbuild_test ("richard", NULL);
	if (pk_strequal (text_safe, "richard")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "incorrect ret '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "build_va double");
	text_safe = pk_strbuild_test ("richard", "hughes", NULL);
	if (pk_strequal (text_safe, "richard hughes")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "incorrect ret '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "build_va double with space");
	text_safe = pk_strbuild_test ("richard", "", "hughes", NULL);
	if (pk_strequal (text_safe, "richard hughes")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "incorrect ret '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "build_va triple");
	text_safe = pk_strbuild_test ("richard", "phillip", "hughes", NULL);
	if (pk_strequal (text_safe, "richard phillip hughes")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "incorrect ret '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************
	 ****************        validate text         **************
	 ************************************************************/
	libst_title (test, "validate correct char 1");
	ret = pk_strvalidate_char ('a');
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "validate correct char 2");
	ret = pk_strvalidate_char ('~');
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "validate incorrect char");
	ret = pk_strvalidate_char ('$');
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "validate incorrect text");
	ret = pk_strvalidate ("richard$hughes");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "validate correct text");
	ret = pk_strvalidate ("richardhughes");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************
	 ****************          Zero            ******************
	 ************************************************************/
	temp = NULL;
	libst_title (test, "test strzero (null)");
	ret = pk_strzero (NULL);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed null");
	}

	/************************************************************/
	libst_title (test, "test strzero (null first char)");
	ret = pk_strzero ("");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed null");
	}

	/************************************************************/
	libst_title (test, "test strzero (long string)");
	ret = pk_strzero ("Richard");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "zero length word!");
	}

	/************************************************************
	 ****************          splitting         ****************
	 ************************************************************/
	libst_title (test, "test pass 1");
	array = pk_strsplit ("foo", 1);
	if (array != NULL &&
	    pk_strequal (array[0], "foo")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "got %s", array[0]);
	}
	g_strfreev (array);

	/************************************************************/
	libst_title (test, "test pass 2");
	array = pk_strsplit ("foo;moo", 2);
	if (array != NULL &&
	    pk_strequal (array[0], "foo") &&
	    pk_strequal (array[1], "moo")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "got %s, %s", array[0], array[1]);
	}
	g_strfreev (array);

	/************************************************************/
	libst_title (test, "test pass 3");
	array = pk_strsplit ("foo;moo;bar", 3);
	if (array != NULL &&
	    pk_strequal (array[0], "foo") &&
	    pk_strequal (array[1], "moo") &&
	    pk_strequal (array[2], "bar")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "got %s, %s, %s, %s", array[0], array[1], array[2], array[3]);
	}
	g_strfreev (array);

	/************************************************************/
	libst_title (test, "test on real packageid");
	array = pk_strsplit ("kde-i18n-csb;4:3.5.8~pre20071001-0ubuntu1;all;", 4);
	if (array != NULL &&
	    pk_strequal (array[0], "kde-i18n-csb") &&
	    pk_strequal (array[1], "4:3.5.8~pre20071001-0ubuntu1") &&
	    pk_strequal (array[2], "all") &&
	    pk_strequal (array[3], "")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "got %s, %s, %s, %s", array[0], array[1], array[2], array[3]);
	}
	g_strfreev (array);

	/************************************************************/
	libst_title (test, "test on short packageid");
	array = pk_strsplit ("kde-i18n-csb;4:3.5.8~pre20071001-0ubuntu1;;", 4);
	if (array != NULL &&
	    pk_strequal (array[0], "kde-i18n-csb") &&
	    pk_strequal (array[1], "4:3.5.8~pre20071001-0ubuntu1") &&
	    pk_strequal (array[2], "") &&
	    pk_strequal (array[3], "")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "got %s, %s, %s, %s", array[0], array[1], array[2], array[3]);
	}
	g_strfreev (array);

	/************************************************************/
	libst_title (test, "test fail under");
	array = pk_strsplit ("foo;moo", 1);
	if (array == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "test fail over");
	array = pk_strsplit ("foo;moo", 3);
	if (array == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "test fail missing first");
	array = pk_strsplit (";moo", 2);
	if (array == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "id strcmp pass");
	ret = pk_strequal ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;fedora");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "id strcmp fail");
	ret = pk_strequal ("moo;0.0.1;i386;fedora", "moo;0.0.2;i386;fedora");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_title (test, "id equal pass (same)");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;fedora", 4, 3);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_title (test, "id equal pass (parts==match)");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;fedora", 4, 4);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_title (test, "id equal pass (different)");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;data", 4, 3);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_title (test, "id equal fail1");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "moo;0.0.2;x64;fedora", 4, 3);
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_title (test, "id equal fail2");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "gnome;0.0.2;i386;fedora", 4, 3);
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_title (test, "id equal fail3");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "moo;0.0.3;i386;fedora", 4, 3);
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_title (test, "id equal fail (match too high)");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "moo;0.0.3;i386;fedora", 4, 5);
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************
	 ****************          strlen          ******************
	 ************************************************************/
	libst_title (test, "strlen bigger");
	text_safe = g_strdup ("123456789");
	length = pk_strlen (text_safe, 20);
	if (length == 9 && pk_strequal (text_safe, "123456789")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the strlen %i,'%s'", length, text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "strlen smaller");
	text_safe = g_strdup ("123456789");
	length = pk_strlen (text_safe, 5);
	if (length == 5 && pk_strequal (text_safe, "12345")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the strlen %i,'%s'", length, text_safe);
	}
	g_free (text_safe);

	/************************************************************
	 ****************         Padding          ******************
	 ************************************************************/
	libst_title (test, "pad smaller");
	text_safe = pk_strpad ("richard", 10);
	if (pk_strequal (text_safe, "richard   ")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the padd '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "pad NULL");
	text_safe = pk_strpad (NULL, 10);
	if (pk_strequal (text_safe, "          ")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the padd '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "pad nothing");
	text_safe = pk_strpad ("", 10);
	if (pk_strequal (text_safe, "          ")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the padd '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "pad over");
	text_safe = pk_strpad ("richardhughes", 10);
	if (pk_strequal (text_safe, "richardhughes")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the padd '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "pad zero");
	text_safe = pk_strpad ("rich", 0);
	if (pk_strequal (text_safe, "rich")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the padd '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************
	 ****************         Padding          ******************
	 ************************************************************/
	libst_title (test, "pad smaller, no extra");
	length = 0;
	text_safe = pk_strpad_extra ("richard", 10, &length);
	if (length == 0 && pk_strequal (text_safe, "richard   ")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the padd '%s', extra %i", text_safe, length);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "pad over, no extra");
	length = 0;
	text_safe = pk_strpad_extra ("richardhughes", 10, &length);
	if (length == 3 && pk_strequal (text_safe, "richardhughes")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the padd '%s', extra %i", text_safe, length);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "pad smaller, 1 extra");
	length = 1;
	text_safe = pk_strpad_extra ("richard", 10, &length);
	if (length == 0 && pk_strequal (text_safe, "richard  ")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the padd '%s', extra %i", text_safe, length);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "pad over, 1 extra");
	length = 1;
	text_safe = pk_strpad_extra ("richardhughes", 10, &length);
	if (length == 4 && pk_strequal (text_safe, "richardhughes")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the padd '%s', extra %i", text_safe, length);
	}
	g_free (text_safe);

	/************************************************************
	 ****************       REPLACE CHARS      ******************
	 ************************************************************/
	libst_title (test, "test replace unsafe (okay)");
	text_safe = pk_strsafe ("Richard Hughes");
	if (pk_strequal (text_safe, "Richard Hughes")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the replace unsafe '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "test replace UTF8 unsafe (okay)");
	text_safe = pk_strsafe ("Gölas");
	if (pk_strequal (text_safe, "Gölas")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the replace unsafe '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "test replace unsafe (one invalid)");
	text_safe = pk_strsafe ("Richard\tHughes");
	if (pk_strequal (text_safe, "Richard Hughes")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the replace unsafe '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "test replace unsafe (one invalid 2)");
	text_safe = pk_strsafe ("Richard\"Hughes\"");
	if (pk_strequal (text_safe, "Richard Hughes ")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the replace unsafe '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	libst_title (test, "test replace unsafe (multiple invalid)");
	text_safe = pk_strsafe ("'Richard\"Hughes\"");
	if (pk_strequal (text_safe, " Richard Hughes ")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the replace unsafe '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************
	 **************       Check for numbers      ****************
	 ************************************************************/
	libst_title (test, "check number valid");
	ret = pk_strnumber ("123");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check number valid");
	ret = pk_strnumber ("-123");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check number zero");
	ret = pk_strnumber ("0");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check number oversize");
	ret = pk_strnumber ("123456891234");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check number NULL");
	ret = pk_strnumber (NULL);
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check number blank");
	ret = pk_strnumber ("");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check number not negative");
	ret = pk_strnumber ("503-");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check number positive");
	ret = pk_strnumber ("+503");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check number random chars");
	ret = pk_strnumber ("dave");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************
	 **************        Convert numbers       ****************
	 ************************************************************/
	libst_title (test, "convert valid number");
	ret = pk_strtoint ("234", &value);
	if (ret && value == 234) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "value is %i", value);
	}

	/************************************************************/
	libst_title (test, "convert negative valid number");
	ret = pk_strtoint ("-234", &value);
	if (ret && value == -234) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "value is %i", value);
	}

	/************************************************************/
	libst_title (test, "don't convert invalid number");
	ret = pk_strtoint ("dave", &value);
	if (ret == FALSE && value == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "value is %i", value);
	}

	/************************************************************/
	libst_title (test, "convert valid uint number");
	ret = pk_strtouint ("234", &uvalue);
	if (ret && uvalue == 234) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "value is %i", uvalue);
	}

	/************************************************************/
	libst_title (test, "convert invalid uint number");
	ret = pk_strtouint ("-234", &uvalue);
	if (ret == FALSE && uvalue == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "value is %i", uvalue);
	}

	/************************************************************
	 **************            iso8601           ****************
	 ************************************************************/
	libst_title (test, "get present iso8601");
	present = pk_iso8601_present ();
	if (present != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "present is NULL");
	}

	g_usleep (2000000);

	/************************************************************/
	libst_title (test, "get difference in iso8601");
	seconds = pk_iso8601_difference (present);
	if (seconds == 2) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "seconds is wrong, %i", seconds);
	}

	/************************************************************/
	g_free (present);

	libst_end (test);
}
#endif

