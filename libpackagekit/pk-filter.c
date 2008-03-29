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
 * SECTION:pk-filter
 * @short_description: Common filter functions for PackageKit
 *
 * This file contains functions that may be useful.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>

#include "pk-debug.h"
#include "pk-filter.h"
#include "pk-common.h"
#include "pk-enum.h"

/**
 * pk_filter_check:
 * @filter: A text failter to test
 *
 * Tests a compound filter to see if every element is correct and if it well
 * formed.
 *
 * Return value: %TRUE if the filter is valid
 **/
gboolean
pk_filter_check (const gchar *filter)
{
	gchar **sections;
	guint i;
	guint length;
	gboolean ret;

	if (filter == NULL) {
		pk_warning ("filter null");
		return FALSE;
	}
	if (pk_strzero (filter)) {
		pk_warning ("filter zero length");
		return FALSE;
	}

	/* split by delimeter ';' */
	sections = g_strsplit (filter, ";", 0);
	length = g_strv_length (sections);
	ret = FALSE;
	for (i=0; i<length; i++) {
		/* only one wrong part is enough to fail the filter */
		if (pk_strzero (sections[i])) {
			goto out;
		}
		if (pk_filter_enum_from_text (sections[i]) == PK_FILTER_ENUM_UNKNOWN) {
			goto out;
		}
	}
	ret = TRUE;
out:
	g_strfreev (sections);
	return ret;
}

/**
 * pk_filter_set_all:
 * @filter: the #PkFilter object
 *
 * Return value: %TRUE if the #PkFilter object was reset.
 **/
gboolean
pk_filter_set_all (PkFilter *filter, gboolean value)
{
	if (filter == NULL) {
		pk_warning ("no filter");
		return FALSE;
	}
	filter->installed = value;
	filter->not_installed = value;
	filter->devel = value;
	filter->not_devel = value;
	filter->gui = value;
	filter->not_gui = value;
	filter->supported = value;
	filter->not_supported = value;
	filter->visible = value;
	filter->not_visible = value;
	filter->basename = value;
	filter->not_basename = value;
	filter->newest = value;
	filter->not_newest = value;
	return TRUE;
}

/**
 * pk_filter_new:
 *
 * Creates a new #PkFilter object with default values
 *
 * Return value: a new #PkFilter object
 **/
PkFilter *
pk_filter_new (void)
{
	PkFilter *filter;
	filter = g_new0 (PkFilter, 1);
	pk_filter_set_all (filter, FALSE);
	return filter;
}

/**
 * pk_filter_new_from_string:
 * @filter: the text to pre-fill the object
 *
 * Creates a new #PkFilter object with values taken from the supplied id.
 *
 * Return value: a new #PkFilter object, or NULL in event of an error
 **/
PkFilter *
pk_filter_new_from_string (const gchar *filter_text)
{
	gchar **sections;
	PkFilter *filter = NULL;
	gboolean ret = TRUE;
	guint i = 0;

	/* check for nothing */
	if (pk_strzero (filter_text)) {
		pk_warning ("invalid blank filter (do you mean 'none'?)");
		return NULL;
	}

	/* check for nothing */
	if (pk_strequal (filter_text, "none")) {
		pk_debug ("shortcut for speed");
		filter = pk_filter_new ();
		/* 'none' is a really bad name, it should really be 'all' */
		pk_filter_set_all (filter, TRUE);
		return filter;
	}

	sections = g_strsplit (filter_text, ";", -1);
	if (sections == NULL) {
		pk_warning ("failed to split");
		return NULL;
	}

	/* create new object, all set FALSE */
	filter = pk_filter_new ();

	/* by default we pass something, unless it's present in the negative */
	pk_filter_set_all (filter, TRUE);

	while (sections[i]) {
		if (pk_strequal (sections[i], "installed")) {
			filter->not_installed = FALSE;
		} else if (pk_strequal (sections[i], "~installed")) {
			filter->installed = FALSE;
		} else if (pk_strequal (sections[i], "devel")) {
			filter->not_devel = FALSE;
		} else if (pk_strequal (sections[i], "~devel")) {
			filter->devel = FALSE;
		} else if (pk_strequal (sections[i], "gui")) {
			filter->not_gui = FALSE;
		} else if (pk_strequal (sections[i], "~gui")) {
			filter->gui = FALSE;
		} else if (pk_strequal (sections[i], "supported")) {
			filter->not_supported = FALSE;
		} else if (pk_strequal (sections[i], "~supported")) {
			filter->supported = FALSE;
		} else if (pk_strequal (sections[i], "visible")) {
			filter->not_visible = FALSE;
		} else if (pk_strequal (sections[i], "~visible")) {
			filter->visible = FALSE;
		} else if (pk_strequal (sections[i], "basename")) {
			filter->not_basename = FALSE;
		} else if (pk_strequal (sections[i], "~basename")) {
			filter->basename = FALSE;
		} else if (pk_strequal (sections[i], "newest")) {
			filter->not_newest = FALSE;
		} else if (pk_strequal (sections[i], "~newest")) {
			filter->newest = FALSE;
		} else {
			pk_warning ("element '%s' not recognised", sections[i]);
			ret = FALSE;
		}
		i++;
	}

	/* failed parsing */
	if (!ret) {
		pk_warning ("invalid filter '%s'", filter_text);
		pk_filter_free (filter);
		filter = NULL;
		goto out;
	}

	/* all OK */
out:
	g_strfreev (sections);
	return filter;
}

/**
 * pk_filter_to_string:
 * @filter: A #PkFilter object
 *
 * Return value: returns a string representation of #PkFilter.
 **/
gchar *
pk_filter_to_string (PkFilter *filter)
{
	GString *string;
	gchar *filter_text;

	if (filter == NULL) {
		pk_warning ("no filter");
		return NULL;
	}

	string = g_string_new ("");
	if (filter->installed && !filter->not_installed) {
		g_string_append (string, "installed;");
	}
	if (filter->not_installed && !filter->installed) {
		g_string_append (string, "~installed;");
	}
	if (filter->devel && !filter->not_devel) {
		g_string_append (string, "devel;");
	}
	if (filter->not_devel && !filter->devel) {
		g_string_append (string, "~devel;");
	}
	if (filter->gui && !filter->not_gui) {
		g_string_append (string, "gui;");
	}
	if (filter->not_gui && !filter->gui) {
		g_string_append (string, "~gui;");
	}
	if (filter->supported && !filter->not_supported) {
		g_string_append (string, "supported;");
	}
	if (filter->not_supported && !filter->supported) {
		g_string_append (string, "~supported;");
	}
	if (filter->visible && !filter->not_visible) {
		g_string_append (string, "visible;");
	}
	if (filter->not_visible && !filter->visible) {
		g_string_append (string, "~visible;");
	}
	if (filter->basename && !filter->not_basename) {
		g_string_append (string, "basename;");
	}
	if (filter->not_basename && !filter->basename) {
		g_string_append (string, "~basename;");
	}
	if (filter->newest && !filter->not_newest) {
		g_string_append (string, "newest;");
	}
	if (filter->not_newest && !filter->newest) {
		g_string_append (string, "~newest;");
	}

	/* remove trailing ; */
	if (string->len > 0) {
		g_string_set_size (string, string->len-1);
	} else {
		/* this is blank filter */
		g_string_append (string, "none");
	}

	filter_text = g_string_free (string, FALSE);
	return filter_text;
}

/**
 * pk_filter_free:
 * @filter: the #PkFilter object
 *
 * Return value: %TRUE if the #PkFilter object was freed.
 **/
gboolean
pk_filter_free (PkFilter *filter)
{
	if (filter == NULL) {
		pk_warning ("no filter");
		return FALSE;
	}
	g_free (filter);
	return TRUE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_filter (LibSelfTest *test)
{
	gboolean ret;
	PkFilter *filter;
	const gchar *temp;

	if (libst_start (test, "PkFilter", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************
	 ****************      FILTER OBJECT       ******************
	 ************************************************************/

	/************************************************************/
	libst_title (test, "create a blank filter");
	filter = pk_filter_new ();
	if (filter != NULL && filter->installed == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "free a blank filter");
	ret = pk_filter_free (filter);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "create a filter from a string (blank)");
	filter = pk_filter_new_from_string ("none");
	if (filter != NULL && filter->installed && filter->gui &&
			      filter->gui && filter->not_basename && filter->basename) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "create a string from a filter (blank)");
	temp = pk_filter_to_string (filter);
	if (pk_strequal (temp, "none")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "filter '%s'", temp);
	}
	pk_filter_free (filter);

	/************************************************************/
	libst_title (test, "create a filter from a string (composite)");
	filter = pk_filter_new_from_string ("gui;~basename");
	if (filter != NULL && filter->gui && !filter->not_gui && filter->not_basename && !filter->basename) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "create a string from a filter (composite)");
	temp = pk_filter_to_string (filter);
	if (pk_strequal (temp, "gui;~basename")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL, "filter '%s'", filter);
	}

	/************************************************************/
	libst_title (test, "reset a filter");
	pk_filter_set_all (filter, FALSE);
	temp = pk_filter_to_string (filter);
	if (pk_strequal (temp, "none")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	pk_filter_free (filter);

	/************************************************************
	 ****************          FILTERS         ******************
	 ************************************************************/
	temp = NULL;
	libst_title (test, "test a fail filter (null)");
	ret = pk_filter_check (temp);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "";
	libst_title (test, "test a fail filter ()");
	ret = pk_filter_check (temp);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}

	/************************************************************/
	temp = ";";
	libst_title (test, "test a fail filter (;)");
	ret = pk_filter_check (temp);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "moo";
	libst_title (test, "test a fail filter (invalid)");
	ret = pk_filter_check (temp);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "moo;foo";
	libst_title (test, "test a fail filter (invalid, multiple)");
	ret = pk_filter_check (temp);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "gui;;";
	libst_title (test, "test a fail filter (valid then zero length)");
	ret = pk_filter_check (temp);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "none";
	libst_title (test, "test a pass filter (none)");
	ret = pk_filter_check (temp);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "gui";
	libst_title (test, "test a pass filter (single)");
	ret = pk_filter_check (temp);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "devel;~gui";
	libst_title (test, "test a pass filter (multiple)");
	ret = pk_filter_check (temp);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "~gui;~installed";
	libst_title (test, "test a pass filter (multiple2)");
	ret = pk_filter_check (temp);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the filter '%s'", temp);
	}

	libst_end (test);
}
#endif

