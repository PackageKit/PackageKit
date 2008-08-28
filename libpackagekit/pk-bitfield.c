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
 * SECTION:pk-enum
 * @short_description: Functions for converting strings to enum and vice-versa
 *
 * This file contains functions to convert to and from enumerated types.
 */

#include "config.h"

#include <stdlib.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-common.h"
#include "pk-enum.h"
#include "pk-bitfield.h"

/**
 * pk_bitfield_contain_priority:
 * @values: a valid bitfield instance
 * @value: the values we are searching for
 *
 * Finds elements in a list, but with priority going to the preceeding entry
 *
 * Return value: The return enumerated type, or -1 if none are found
 **/
gint
pk_bitfield_contain_priority (PkBitfield values, gint value, ...)
{
	va_list args;
	guint i;
	guint value_temp;
	gint retval = -1;

	/* we must query at least one thing */
	if (pk_bitfield_contain (values, value)) {
		return value;
	}

	/* process the valist */
	va_start (args, value);
	for (i=0;; i++) {
		value_temp = va_arg (args, gint);
		/* do we have this one? */
		if (pk_bitfield_contain (values, value_temp)) {
			retval = value_temp;
			break;
		}
		/* end of the list */
		if (value_temp == -1) {
			break;
		}
	}
	va_end (args);

	return retval;
}

/**
 * pk_bitfield_from_enums:
 * @value: the values we want to add to the bitfield
 *
 * Return value: The return bitfield, or 0 if invalid
 **/
PkBitfield
pk_bitfield_from_enums (gint value, ...)
{
	va_list args;
	guint i;
	gint value_temp;
	PkBitfield values;

	/* we must query at least one thing */
	values = pk_bitfield_value (value);

	/* process the valist */
	va_start (args, value);
	for (i=0;; i++) {
		value_temp = va_arg (args, gint);
		if (value_temp == -1)
			break;
		values += pk_bitfield_value (value_temp);
	}
	va_end (args);

	return values;
}

/**
 * pk_roles_bitfield_to_text:
 * @roles: The enumerated type values
 *
 * Converts a enumerated type bitfield to its text representation
 *
 * Return value: the enumerated constant value, e.g. "install-file;update-system"
 **/
gchar *
pk_role_bitfield_to_text (PkBitfield roles)
{
	GString *string;
	guint i;

	string = g_string_new ("");
	for (i=0; i<PK_ROLE_ENUM_UNKNOWN; i++) {
		if ((roles & pk_bitfield_value (i)) == 0) {
			continue;
		}
		g_string_append_printf (string, "%s;", pk_role_enum_to_text (i));
	}
	/* do we have a no bitfield? \n */
	if (string->len == 0) {
		egg_warning ("not valid!");
		g_string_append (string, pk_role_enum_to_text (PK_ROLE_ENUM_UNKNOWN));
	} else {
		/* remove last \n */
		g_string_set_size (string, string->len - 1);
	}
	return g_string_free (string, FALSE);
}

/**
 * pk_role_bitfield_from_text:
 * @roles: the enumerated constant value, e.g. "available;~gui"
 *
 * Converts text representation to its enumerated type bitfield
 *
 * Return value: The enumerated type values
 **/
PkBitfield
pk_role_bitfield_from_text (const gchar *roles)
{
	PkBitfield roles_enum = 0;
	gchar **split;
	guint length;
	guint i;

	split = g_strsplit (roles, ";", 0);
	if (split == NULL) {
		egg_warning ("unable to split");
		goto out;
	}

	length = g_strv_length (split);
	for (i=0; i<length; i++) {
		roles_enum += pk_bitfield_value (pk_role_enum_from_text (split[i]));
	}
out:
	g_strfreev (split);
	return roles_enum;
}

/**
 * pk_groups_bitfield_to_text:
 * @groups: The enumerated type values
 *
 * Converts a enumerated type bitfield to its text representation
 *
 * Return value: the enumerated constant value, e.g. "gnome;kde"
 **/
gchar *
pk_group_bitfield_to_text (PkBitfield groups)
{
	GString *string;
	guint i;

	string = g_string_new ("");
	for (i=0; i<PK_GROUP_ENUM_UNKNOWN; i++) {
		if ((groups & pk_bitfield_value (i)) == 0) {
			continue;
		}
		g_string_append_printf (string, "%s;", pk_group_enum_to_text (i));
	}
	/* do we have a no bitfield? \n */
	if (string->len == 0) {
		egg_warning ("not valid!");
		g_string_append (string, pk_group_enum_to_text (PK_GROUP_ENUM_UNKNOWN));
	} else {
		/* remove last \n */
		g_string_set_size (string, string->len - 1);
	}
	return g_string_free (string, FALSE);
}

/**
 * pk_group_bitfield_from_text:
 * @groups: the enumerated constant value, e.g. "available;~gui"
 *
 * Converts text representation to its enumerated type bitfield
 *
 * Return value: The enumerated type values
 **/
PkBitfield
pk_group_bitfield_from_text (const gchar *groups)
{
	PkBitfield groups_enum = 0;
	gchar **split;
	guint length;
	guint i;

	split = g_strsplit (groups, ";", 0);
	if (split == NULL) {
		egg_warning ("unable to split");
		goto out;
	}

	length = g_strv_length (split);
	for (i=0; i<length; i++) {
		groups_enum += pk_bitfield_value (pk_group_enum_from_text (split[i]));
	}
out:
	g_strfreev (split);
	return groups_enum;
}

/**
 * pk_filter_bitfield_to_text:
 * @filters: The enumerated type values
 *
 * Converts a enumerated type bitfield to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available;~gui"
 **/
gchar *
pk_filter_bitfield_to_text (PkBitfield filters)
{
	GString *string;
	guint i;

	/* shortcut */
	if (filters == 0) {
		return g_strdup (pk_filter_enum_to_text (filters));
	}

	string = g_string_new ("");
	for (i=0; i<PK_FILTER_ENUM_UNKNOWN; i++) {
		if ((filters & pk_bitfield_value (i)) == 0) {
			continue;
		}
		g_string_append_printf (string, "%s;", pk_filter_enum_to_text (i));
	}
	/* do we have a 'none' filter? \n */
	if (string->len == 0) {
		egg_warning ("not valid!");
		g_string_append (string, pk_filter_enum_to_text (PK_FILTER_ENUM_NONE));
	} else {
		/* remove last \n */
		g_string_set_size (string, string->len - 1);
	}
	return g_string_free (string, FALSE);
}

/**
 * pk_filter_bitfield_from_text:
 * @filters: the enumerated constant value, e.g. "available;~gui"
 *
 * Converts text representation to its enumerated type bitfield
 *
 * Return value: The enumerated type values
 **/
PkBitfield
pk_filter_bitfield_from_text (const gchar *filters)
{
	PkBitfield filters_enum = PK_FILTER_ENUM_NONE;
	gchar **split;
	guint length;
	guint i;

	split = g_strsplit (filters, ";", 0);
	if (split == NULL) {
		egg_warning ("unable to split");
		goto out;
	}

	length = g_strv_length (split);
	for (i=0; i<length; i++) {
		filters_enum += pk_bitfield_value (pk_filter_enum_from_text (split[i]));
	}
out:
	g_strfreev (split);
	return filters_enum;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_bitfield (LibSelfTest *test)
{
	gchar *text;
	PkBitfield filter;
	guint value;
	PkBitfield values;

	if (libst_start (test, "PkBitfield", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "check we can convert filter bitfield to text (none)");
	text = pk_filter_bitfield_to_text (pk_bitfield_value (PK_FILTER_ENUM_NONE));
	if (egg_strequal (text, "none")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "text was %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "check we can convert filter bitfield to text (single)");
	text = pk_filter_bitfield_to_text (pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT));
	if (egg_strequal (text, "~devel")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "text was %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "check we can convert filter bitfield to text (plural)");
	text = pk_filter_bitfield_to_text (pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) |
					   pk_bitfield_value (PK_FILTER_ENUM_GUI) |
					   pk_bitfield_value (PK_FILTER_ENUM_NEWEST));
	if (egg_strequal (text, "~devel;gui;newest")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "text was %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "check we can convert filter text to bitfield (none)");
	filter = pk_filter_bitfield_from_text ("none");
	if (filter == pk_bitfield_value (PK_FILTER_ENUM_NONE)) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "filter was %i", filter);
	}

	/************************************************************/
	libst_title (test, "check we can convert filter text to bitfield (single)");
	filter = pk_filter_bitfield_from_text ("~devel");
	if (filter == pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "filter was %i", filter);
	}

	/************************************************************/
	libst_title (test, "check we can convert filter text to bitfield (plural)");
	filter = pk_filter_bitfield_from_text ("~devel;gui;newest");
	if (filter == (pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) |
		       pk_bitfield_value (PK_FILTER_ENUM_GUI) |
		       pk_bitfield_value (PK_FILTER_ENUM_NEWEST))) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "filter was %i", filter);
	}

	/************************************************************/
	libst_title (test, "check we can add / remove bitfield");
	filter = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) |
		 pk_bitfield_value (PK_FILTER_ENUM_GUI) |
		 pk_bitfield_value (PK_FILTER_ENUM_NEWEST);
	pk_bitfield_add (filter, PK_FILTER_ENUM_NOT_FREE);
	pk_bitfield_remove (filter, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	text = pk_filter_bitfield_to_text (filter);
	if (egg_strequal (text, "gui;~free;newest")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "text was %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "check we can test enum presence");
	filter = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) |
		 pk_bitfield_value (PK_FILTER_ENUM_GUI) |
		 pk_bitfield_value (PK_FILTER_ENUM_NEWEST);
	if (pk_bitfield_contain (filter, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "wrong boolean");
	}
	libst_title (test, "check we can test enum false-presence");
	if (!pk_bitfield_contain (filter, PK_FILTER_ENUM_FREE)) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "wrong boolean");
	}

	/************************************************************/
	libst_title (test, "check we can add / remove bitfield to nothing");
	filter = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT);
	pk_bitfield_remove (filter, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	text = pk_filter_bitfield_to_text (filter);
	if (egg_strequal (text, "none")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "text was %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "bitfield from enums");
	values = pk_bitfield_from_enums (PK_ROLE_ENUM_SEARCH_GROUP, PK_ROLE_ENUM_SEARCH_DETAILS, -1);
	if (values == (pk_bitfield_value (PK_ROLE_ENUM_SEARCH_DETAILS) |
		       pk_bitfield_value (PK_ROLE_ENUM_SEARCH_GROUP))) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "returned bitfield %i", values);
	}

	/************************************************************/
	libst_title (test, "priority check missing");
	values = pk_bitfield_value (PK_ROLE_ENUM_SEARCH_DETAILS) |
		 pk_bitfield_value (PK_ROLE_ENUM_SEARCH_GROUP);
	value = pk_bitfield_contain_priority (values, PK_ROLE_ENUM_SEARCH_FILE, -1);
	if (value == -1) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "returned priority %i when should be missing", value);
	}

	/************************************************************/
	libst_title (test, "priority check first");
	value = pk_bitfield_contain_priority (values, PK_ROLE_ENUM_SEARCH_GROUP, -1);
	if (value == PK_ROLE_ENUM_SEARCH_GROUP) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "returned wrong value; %i", value);
	}

	/************************************************************/
	libst_title (test, "priority check second, correct");
	value = pk_bitfield_contain_priority (values, PK_ROLE_ENUM_SEARCH_FILE, PK_ROLE_ENUM_SEARCH_GROUP, -1);
	if (value == PK_ROLE_ENUM_SEARCH_GROUP) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "returned wrong value; %i", value);
	}

	libst_end (test);
}
#endif

