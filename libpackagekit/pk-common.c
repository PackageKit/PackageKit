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

#include <glib/gi18n.h>

#include "egg-debug.h"
#include "egg-string.h"

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
		egg_warning ("failed to parse '%s'", isodate);
		return 0;
	}
	g_get_current_time (&timeval_now);

	/* work out difference */
	time = timeval_now.tv_sec - timeval_then.tv_sec;

	return time;
}

/**
 * pk_iso8601_from_date:
 * @date: a %GDate to convert
 *
 * Return value: If valid then a new ISO8601 date, else NULL
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
 **/
GDate *
pk_iso8601_to_date (const gchar *iso_date)
{
	gboolean ret;
	guint retval;
	guint d, m, y;
	GTimeVal time;
	GDate *date = NULL;

	if (egg_strzero (iso_date))
		goto out;

	/* try to parse complete ISO8601 date */
	ret = g_time_val_from_iso8601 (iso_date, &time);
	if (ret) {
		date = g_date_new ();
		g_date_set_time_val (date, &time);
		goto out;
	}

	/* g_time_val_from_iso8601() blows goats and won't
	 * accept a valid ISO8601 formatted date without a
	 * time value - try and parse this case */
	retval = sscanf (iso_date, "%i-%i-%i", &y, &m, &d);
	if (retval == 3) {
		date = g_date_new_dmy (d, m, y);
		goto out;
	}
	egg_warning ("could not parse '%s'", iso_date);
out:
	return date;
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
		egg_warning ("text '%s' was not valid UTF8!", text);
		return NULL;
	}

	/* rip out any insane characters */
	delimiters = "\\\f\r\t\"";
	text_safe = g_strdup (text);
	g_strdelimit (text_safe, delimiters, ' ');
	return text_safe;
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

	/* maximum size is 1024 */
	length = egg_strlen (text, 1024);
	if (length > 1024) {
		egg_warning ("input too long: %u", length);
		return FALSE;
	}

	for (i=0; i<length; i++) {
		if (pk_strvalidate_char (text[i]) == FALSE) {
			egg_warning ("invalid char '%c' in text!", text[i]);
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * pk_ptr_array_to_argv:
 * @array: the GPtrArray of strings
 *
 * Form a composite string array of strings.
 * The data in the GPtrArray is copied.
 *
 * Return value: the string array, or %NULL if invalid
 **/
gchar **
pk_ptr_array_to_argv (GPtrArray *array)
{
	gchar **strv_array;
	const gchar *value_temp;
	guint i;

	g_return_val_if_fail (array != NULL, NULL);

	/* copy the array to a strv */
	strv_array = g_new0 (gchar *, array->len + 2);
	for (i=0; i<array->len; i++) {
		value_temp = (const gchar *) g_ptr_array_index (array, i);
		strv_array[i] = g_strdup (value_temp);
	}
	/* set the last element to NULL */
	strv_array[i] = NULL;

	return strv_array;
}

/**
 * pk_argv_to_ptr_array:
 * @array: the gchar** array of strings
 *
 * Form a GPtrArray array of strings.
 * The data in the array is copied.
 *
 * Return value: the string array, or %NULL if invalid
 **/
GPtrArray *
pk_argv_to_ptr_array (gchar **array)
{
	guint i;
	guint length;
	GPtrArray *parray;

	g_return_val_if_fail (array != NULL, NULL);

	parray = g_ptr_array_new ();
	length = g_strv_length (array);
	for (i=0; i<length; i++) {
		g_ptr_array_add (parray, g_strdup (array[i]));
	}
	return parray;
}


/**
 * pk_va_list_to_argv_string:
 **/
static void
pk_va_list_to_argv_string (GPtrArray *ptr_array, const gchar *string)
{
	gchar **array;
	guint length;
	guint i;

	/* split the string up by spaces */
	array = g_strsplit (string, "|", 0);

	/* for each */
	length = g_strv_length (array);
	for (i=0; i<length; i++) {
		g_ptr_array_add (ptr_array, g_strdup (array[i]));
	}
	g_strfreev (array);
}

/**
 * pk_va_list_to_argv:
 * @string_first: the first string
 * @args: any subsequant string's
 *
 * Form a composite string array of string, with a special twist;
 * if the entry contains a '|', then it is split as seporate parts
 * of the array.
 *
 * Return value: the string array, or %NULL if invalid
 **/
gchar **
pk_va_list_to_argv (const gchar *string_first, va_list *args)
{
	GPtrArray *ptr_array;
	gchar **array;
	gchar *value_temp;
	guint i;

	g_return_val_if_fail (args != NULL, NULL);
	g_return_val_if_fail (string_first != NULL, NULL);

	/* find how many elements we have in a temp array */
	ptr_array = g_ptr_array_new ();
	pk_va_list_to_argv_string (ptr_array, string_first);

	/* process all the va_list entries */
	for (i=0;; i++) {
		value_temp = va_arg (*args, gchar *);
		/* end of array */
		if (value_temp == NULL) break;

		/* split the string up by spaces */
		pk_va_list_to_argv_string (ptr_array, value_temp);
	}

	/* convert the array to a strv type */
	array = pk_ptr_array_to_argv (ptr_array);

	/* get rid of the array, and free the contents */
	g_ptr_array_free (ptr_array, TRUE);
	return array;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static gchar **
pk_va_list_to_argv_test (const gchar *first_element, ...)
{
	va_list args;
	gchar **array;

	/* get the argument list */
	va_start (args, first_element);
	array = pk_va_list_to_argv (first_element, &args);
	va_end (args);

	return array;
}

void
pk_common_test (EggTest *test)
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

	if (!egg_test_start (test, "PkCommon"))
		return;

	/************************************************************
	 ****************        test distro-id        **************
	 ************************************************************/
	egg_test_title (test, "get distro id");
	text_safe = pk_get_distro_id ();
	if (text_safe != NULL) {
		egg_test_success (test, "distro_id=%s", text_safe);
	} else
		egg_test_failed (test, NULL);
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "egg_strequal same argument");
	temp = "dave";
	if (egg_strequal (temp, temp))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "incorrect ret when both same");
	}

	/************************************************************/
	egg_test_title (test, "egg_strequal both const");
	if (egg_strequal ("dave", "dave"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "incorrect ret when both same");
	}

	/************************************************************
	 ****************      splitting va_list       **************
	 ************************************************************/
	egg_test_title (test, "va_list_to_argv single");
	array = pk_va_list_to_argv_test ("richard", NULL);
	if (egg_strequal (array[0], "richard") &&
	    array[1] == NULL)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "incorrect array '%s'", array[0]);
	}
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "va_list_to_argv triple");
	array = pk_va_list_to_argv_test ("richard", "phillip", "hughes", NULL);
	if (egg_strequal (array[0], "richard") &&
	    egg_strequal (array[1], "phillip") &&
	    egg_strequal (array[2], "hughes") &&
	    array[3] == NULL)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "incorrect array '%s','%s','%s'", array[0], array[1], array[2]);
	}
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "va_list_to_argv triple with space first");
	array = pk_va_list_to_argv_test ("richard|phillip", "hughes", NULL);
	if (egg_strequal (array[0], "richard") &&
	    egg_strequal (array[1], "phillip") &&
	    egg_strequal (array[2], "hughes") &&
	    array[3] == NULL)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "incorrect array '%s','%s','%s'", array[0], array[1], array[2]);
	}
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "va_list_to_argv triple with space second");
	array = pk_va_list_to_argv_test ("richard", "phillip|hughes", NULL);
	if (egg_strequal (array[0], "richard") &&
	    egg_strequal (array[1], "phillip") &&
	    egg_strequal (array[2], "hughes") &&
	    array[3] == NULL)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "incorrect array '%s','%s','%s'", array[0], array[1], array[2]);
	}
	g_strfreev (array);

	/************************************************************
	 ****************        validate text         **************
	 ************************************************************/
	egg_test_title (test, "validate correct char 1");
	ret = pk_strvalidate_char ('a');
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "validate correct char 2");
	ret = pk_strvalidate_char ('~');
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "validate incorrect char");
	ret = pk_strvalidate_char ('$');
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "validate incorrect text");
	ret = pk_strvalidate ("richard$hughes");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "validate correct text");
	ret = pk_strvalidate ("richardhughes");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************
	 ****************          Zero            ******************
	 ************************************************************/
	temp = NULL;
	egg_test_title (test, "test strzero (null)");
	ret = egg_strzero (NULL);
	if (ret)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed null");
	}

	/************************************************************/
	egg_test_title (test, "test strzero (null first char)");
	ret = egg_strzero ("");
	if (ret)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed null");
	}

	/************************************************************/
	egg_test_title (test, "test strzero (long string)");
	ret = egg_strzero ("Richard");
	if (!ret)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "zero length word!");
	}

	/************************************************************/
	egg_test_title (test, "id strcmp pass");
	ret = egg_strequal ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;fedora");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "id strcmp fail");
	ret = egg_strequal ("moo;0.0.1;i386;fedora", "moo;0.0.2;i386;fedora");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************
	 ****************          strlen          ******************
	 ************************************************************/
	egg_test_title (test, "strlen bigger");
	length = egg_strlen ("123456789", 20);
	if (length == 9)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the strlen %i", length);
	}

	/************************************************************/
	egg_test_title (test, "strlen smaller");
	length = egg_strlen ("123456789", 5);
	if (length == 5)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the strlen %i", length);
	}

	/************************************************************/
	egg_test_title (test, "strlen correct");
	length = egg_strlen ("123456789", 9);
	if (length == 9)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the strlen %i", length);
	}

	/************************************************************
	 ****************         Replace          ******************
	 ************************************************************/
	egg_test_title (test, "replace start");
	text_safe = egg_strreplace ("richard\nhughes", "r", "e");
	if (egg_strequal (text_safe, "eichaed\nhughes"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the replace '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "replace none");
	text_safe = egg_strreplace ("richard\nhughes", "dave", "e");
	if (egg_strequal (text_safe, "richard\nhughes"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the replace '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "replace end");
	text_safe = egg_strreplace ("richard\nhughes", "s", "e");
	if (egg_strequal (text_safe, "richard\nhughee"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the replace '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "replace unicode");
	text_safe = egg_strreplace ("richard\n- hughes", "\n- ", "\n• ");
	if (egg_strequal (text_safe, "richard\n• hughes"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the replace '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************
	 ****************       REPLACE CHARS      ******************
	 ************************************************************/
	egg_test_title (test, "test replace unsafe (okay)");
	text_safe = pk_strsafe ("Richard Hughes");
	if (egg_strequal (text_safe, "Richard Hughes"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the replace unsafe '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "test replace UTF8 unsafe (okay)");
	text_safe = pk_strsafe ("Gölas");
	if (egg_strequal (text_safe, "Gölas"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the replace unsafe '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "test replace unsafe (one invalid)");
	text_safe = pk_strsafe ("Richard\tHughes");
	if (egg_strequal (text_safe, "Richard Hughes"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the replace unsafe '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "test replace unsafe (one invalid 2)");
	text_safe = pk_strsafe ("Richard\"Hughes\"");
	if (egg_strequal (text_safe, "Richard Hughes "))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the replace unsafe '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "test replace unsafe (multiple invalid)");
	text_safe = pk_strsafe (" Richard\"Hughes\"");
	if (egg_strequal (text_safe, " Richard Hughes "))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the replace unsafe '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************
	 **************       Check for numbers      ****************
	 ************************************************************/
	egg_test_title (test, "check number valid");
	ret = egg_strnumber ("123");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check number valid");
	ret = egg_strnumber ("-123");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check number zero");
	ret = egg_strnumber ("0");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check number oversize");
	ret = egg_strnumber ("123456891234");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check number NULL");
	ret = egg_strnumber (NULL);
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check number blank");
	ret = egg_strnumber ("");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check number not negative");
	ret = egg_strnumber ("503-");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check number positive");
	ret = egg_strnumber ("+503");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check number random chars");
	ret = egg_strnumber ("dave");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************
	 **************        Convert numbers       ****************
	 ************************************************************/
	egg_test_title (test, "convert valid number");
	ret = egg_strtoint ("234", &value);
	if (ret && value == 234)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "value is %i", value);
	}

	/************************************************************/
	egg_test_title (test, "convert negative valid number");
	ret = egg_strtoint ("-234", &value);
	if (ret && value == -234)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "value is %i", value);
	}

	/************************************************************/
	egg_test_title (test, "don't convert invalid number");
	ret = egg_strtoint ("dave", &value);
	if (ret == FALSE && value == 0)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "value is %i", value);
	}

	/************************************************************/
	egg_test_title (test, "convert valid uint number");
	ret = egg_strtouint ("234", &uvalue);
	if (ret && uvalue == 234)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "value is %i", uvalue);
	}

	/************************************************************/
	egg_test_title (test, "convert invalid uint number");
	ret = egg_strtouint ("-234", &uvalue);
	if (ret == FALSE && uvalue == 0)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "value is %i", uvalue);
	}

	/************************************************************
	 **************            iso8601           ****************
	 ************************************************************/
	egg_test_title (test, "get present iso8601");
	present = pk_iso8601_present ();
	if (present != NULL)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "present is NULL");
	}

	g_usleep (2000000);

	/************************************************************/
	egg_test_title (test, "get difference in iso8601");
	seconds = pk_iso8601_difference (present);
	if (seconds == 2)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "seconds is wrong, %i", seconds);
	}

	/************************************************************/
	g_free (present);

	egg_test_end (test);
}
#endif

