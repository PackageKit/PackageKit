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
 * SECTION:pk-enum
 * @short_description: Functions for converting strings to enum and vice-versa
 *
 * This file contains functions to convert to and from enumerated types.
 */

#include "config.h"

#include <glib.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-bitfield.h>

#include "egg-debug.h"

/**
 * pk_bitfield_contain_priority:
 * @values: a valid bitfield instance
 * @value: the values we are searching for
 *
 * Finds elements in a list, but with priority going to the preceeding entry
 *
 * Return value: The return enumerated type, or -1 if none are found
 *
 * Since: 0.5.2
 **/
gint
pk_bitfield_contain_priority (PkBitfield values, gint value, ...)
{
	va_list args;
	guint i;
	gint value_temp;
	gint retval = -1;

	/* we must query at least one thing */
	if (pk_bitfield_contain (values, value))
		return value;

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
		if (value_temp == -1)
			break;
	}
	va_end (args);

	return retval;
}

/**
 * pk_bitfield_from_enums:
 * @value: the values we want to add to the bitfield
 *
 * Return value: The return bitfield, or 0 if invalid
 *
 * Since: 0.5.2
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
 * pk_role_bitfield_to_string:
 * @roles: The enumerated type values
 *
 * Converts a enumerated type bitfield to its text representation
 *
 * Return value: the enumerated constant value, e.g. "install-file;update-system"
 *
 * Since: 0.5.2
 **/
gchar *
pk_role_bitfield_to_string (PkBitfield roles)
{
	GString *string;
	guint i;

	string = g_string_new ("");
	for (i=0; i<PK_ROLE_ENUM_LAST; i++) {
		if ((roles & pk_bitfield_value (i)) == 0)
			continue;
		g_string_append_printf (string, "%s;", pk_role_enum_to_string (i));
	}
	/* do we have a no bitfield? \n */
	if (string->len == 0) {
		egg_warning ("not valid!");
		g_string_append (string, pk_role_enum_to_string (PK_ROLE_ENUM_UNKNOWN));
	} else {
		/* remove last \n */
		g_string_set_size (string, string->len - 1);
	}
	return g_string_free (string, FALSE);
}

/**
 * pk_role_bitfield_from_string:
 * @roles: the enumerated constant value, e.g. "available;~gui"
 *
 * Converts text representation to its enumerated type bitfield
 *
 * Return value: The enumerated type values, or 0 for invalid
 *
 * Since: 0.5.2
 **/
PkBitfield
pk_role_bitfield_from_string (const gchar *roles)
{
	PkBitfield roles_enum = 0;
	gchar **split;
	guint length;
	guint i;
	PkRoleEnum role;

	split = g_strsplit (roles, ";", 0);
	if (split == NULL) {
		egg_warning ("unable to split");
		goto out;
	}

	length = g_strv_length (split);
	for (i=0; i<length; i++) {
		role = pk_role_enum_from_string (split[i]);
		if (role == PK_ROLE_ENUM_UNKNOWN) {
			roles_enum = 0;
			break;
		}
		roles_enum += pk_bitfield_value (role);
	}
out:
	g_strfreev (split);
	return roles_enum;
}

/**
 * pk_groups_bitfield_to_string:
 * @groups: The enumerated type values
 *
 * Converts a enumerated type bitfield to its text representation
 *
 * Return value: the enumerated constant value, e.g. "gnome;kde"
 *
 * Since: 0.5.2
 **/
gchar *
pk_group_bitfield_to_string (PkBitfield groups)
{
	GString *string;
	guint i;

	string = g_string_new ("");
	for (i=0; i<PK_GROUP_ENUM_LAST; i++) {
		if ((groups & pk_bitfield_value (i)) == 0)
			continue;
		g_string_append_printf (string, "%s;", pk_group_enum_to_string (i));
	}
	/* do we have a no bitfield? \n */
	if (string->len == 0) {
		egg_warning ("not valid!");
		g_string_append (string, pk_group_enum_to_string (PK_GROUP_ENUM_UNKNOWN));
	} else {
		/* remove last \n */
		g_string_set_size (string, string->len - 1);
	}
	return g_string_free (string, FALSE);
}

/**
 * pk_group_bitfield_from_string:
 * @groups: the enumerated constant value, e.g. "available;~gui"
 *
 * Converts text representation to its enumerated type bitfield
 *
 * Return value: The enumerated type values, or 0 for invalid
 *
 * Since: 0.5.2
 **/
PkBitfield
pk_group_bitfield_from_string (const gchar *groups)
{
	PkBitfield groups_enum = 0;
	gchar **split;
	guint length;
	guint i;
	PkGroupEnum group;

	split = g_strsplit (groups, ";", 0);
	if (split == NULL) {
		egg_warning ("unable to split");
		goto out;
	}

	length = g_strv_length (split);
	for (i=0; i<length; i++) {
		group = pk_group_enum_from_string (split[i]);
		if (group == PK_GROUP_ENUM_UNKNOWN) {
			groups_enum = 0;
			break;
		}
		groups_enum += pk_bitfield_value (group);
	}
out:
	g_strfreev (split);
	return groups_enum;
}

/**
 * pk_filter_bitfield_to_string:
 * @filters: The enumerated type values
 *
 * Converts a enumerated type bitfield to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available;~gui"
 *
 * Since: 0.5.2
 **/
gchar *
pk_filter_bitfield_to_string (PkBitfield filters)
{
	GString *string;
	guint i;

	/* shortcut */
	if (filters == 0)
		return g_strdup (pk_filter_enum_to_string (PK_FILTER_ENUM_NONE));

	string = g_string_new ("");
	for (i=0; i<PK_FILTER_ENUM_LAST; i++) {
		if ((filters & pk_bitfield_value (i)) == 0)
			continue;
		g_string_append_printf (string, "%s;", pk_filter_enum_to_string (i));
	}
	/* do we have a 'none' filter? \n */
	if (string->len == 0) {
		egg_warning ("not valid!");
		g_string_append (string, pk_filter_enum_to_string (PK_FILTER_ENUM_NONE));
	} else {
		/* remove last \n */
		g_string_set_size (string, string->len - 1);
	}
	return g_string_free (string, FALSE);
}

/**
 * pk_filter_bitfield_from_string:
 * @filters: the enumerated constant value, e.g. "available;~gui"
 *
 * Converts text representation to its enumerated type bitfield, or 0 for invalid
 *
 * Return value: The enumerated type values
 *
 * Since: 0.5.2
 **/
PkBitfield
pk_filter_bitfield_from_string (const gchar *filters)
{
	PkBitfield filters_enum = 0;
	gchar **split;
	guint length;
	guint i;
	PkFilterEnum filter;

	split = g_strsplit (filters, ";", 0);
	if (split == NULL) {
		egg_warning ("unable to split");
		goto out;
	}

	length = g_strv_length (split);
	for (i=0; i<length; i++) {
		filter = pk_filter_enum_from_string (split[i]);
		if (filter == PK_FILTER_ENUM_UNKNOWN) {
			filters_enum = 0;
			break;
		}
		filters_enum += pk_bitfield_value (filter);
	}
out:
	g_strfreev (split);
	return filters_enum;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_bitfield_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	gchar *text;
	PkBitfield filter;
	gint value;
	PkBitfield values;

	if (!egg_test_start (test, "PkBitfield"))
		return;

	/************************************************************/
	egg_test_title (test, "check we can convert filter bitfield to text (none)");
	text = pk_filter_bitfield_to_string (pk_bitfield_value (PK_FILTER_ENUM_NONE));
	if (g_strcmp0 (text, "none") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "text was %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "check we can invert a bit 1 -> 0");
	values = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) | pk_bitfield_value (PK_FILTER_ENUM_NOT_NEWEST);
	pk_bitfield_invert (values, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	if (values == pk_bitfield_value (PK_FILTER_ENUM_NOT_NEWEST))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "values were %" PK_BITFIELD_FORMAT, values);

	/************************************************************/
	egg_test_title (test, "check we can invert a bit 0 -> 1");
	values = 0;
	pk_bitfield_invert (values, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	if (values == pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "values were %" PK_BITFIELD_FORMAT, values);

	/************************************************************/
	egg_test_title (test, "check we can convert filter bitfield to text (single)");
	text = pk_filter_bitfield_to_string (pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT));
	if (g_strcmp0 (text, "~devel") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "text was %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "check we can convert filter bitfield to text (plural)");
	text = pk_filter_bitfield_to_string (pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) |
					   pk_bitfield_value (PK_FILTER_ENUM_GUI) |
					   pk_bitfield_value (PK_FILTER_ENUM_NEWEST));
	if (g_strcmp0 (text, "~devel;gui;newest") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "text was %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "check we can convert filter text to bitfield (none)");
	filter = pk_filter_bitfield_from_string ("none");
	if (filter == pk_bitfield_value (PK_FILTER_ENUM_NONE))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "filter was %" PK_BITFIELD_FORMAT, filter);

	/************************************************************/
	egg_test_title (test, "check we can convert filter text to bitfield (single)");
	filter = pk_filter_bitfield_from_string ("~devel");
	if (filter == pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "filter was %" PK_BITFIELD_FORMAT, filter);

	/************************************************************/
	egg_test_title (test, "check we can convert filter text to bitfield (plural)");
	filter = pk_filter_bitfield_from_string ("~devel;gui;newest");
	if (filter == (pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) |
		       pk_bitfield_value (PK_FILTER_ENUM_GUI) |
		       pk_bitfield_value (PK_FILTER_ENUM_NEWEST)))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "filter was %" PK_BITFIELD_FORMAT, filter);

	/************************************************************/
	egg_test_title (test, "check we can add / remove bitfield");
	filter = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) |
		 pk_bitfield_value (PK_FILTER_ENUM_GUI) |
		 pk_bitfield_value (PK_FILTER_ENUM_NEWEST);
	pk_bitfield_add (filter, PK_FILTER_ENUM_NOT_FREE);
	pk_bitfield_remove (filter, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	text = pk_filter_bitfield_to_string (filter);
	if (g_strcmp0 (text, "gui;~free;newest") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "text was %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "check we can test enum presence");
	filter = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) |
		 pk_bitfield_value (PK_FILTER_ENUM_GUI) |
		 pk_bitfield_value (PK_FILTER_ENUM_NEWEST);
	if (pk_bitfield_contain (filter, PK_FILTER_ENUM_NOT_DEVELOPMENT))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong boolean");

	/************************************************************/
	egg_test_title (test, "check we can test enum false-presence");
	if (!pk_bitfield_contain (filter, PK_FILTER_ENUM_FREE))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong boolean");

	/************************************************************/
	egg_test_title (test, "check we can add / remove bitfield to nothing");
	filter = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT);
	pk_bitfield_remove (filter, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	text = pk_filter_bitfield_to_string (filter);
	if (g_strcmp0 (text, "none") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "text was %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "role bitfield from enums (unknown)");
	values = pk_bitfield_from_enums (PK_ROLE_ENUM_UNKNOWN, -1);
	if (values == pk_bitfield_value (PK_ROLE_ENUM_UNKNOWN))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "returned bitfield %" PK_BITFIELD_FORMAT, values);

	/************************************************************/
	egg_test_title (test, "role bitfield from enums (random)");
	values = pk_bitfield_from_enums (PK_ROLE_ENUM_SEARCH_GROUP, PK_ROLE_ENUM_SEARCH_DETAILS, -1);
	if (values == (pk_bitfield_value (PK_ROLE_ENUM_SEARCH_DETAILS) |
		       pk_bitfield_value (PK_ROLE_ENUM_SEARCH_GROUP)))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "returned bitfield %" PK_BITFIELD_FORMAT, values);

	/************************************************************/
	egg_test_title (test, "group bitfield from enums (unknown)");
	values = pk_bitfield_from_enums (PK_GROUP_ENUM_UNKNOWN, -1);
	if (values == pk_bitfield_value (PK_GROUP_ENUM_UNKNOWN))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "returned bitfield %" PK_BITFIELD_FORMAT, values);

	/************************************************************/
	egg_test_title (test, "group bitfield from enums (random)");
	values = pk_bitfield_from_enums (PK_GROUP_ENUM_ACCESSIBILITY, -1);
	if (values == (pk_bitfield_value (PK_GROUP_ENUM_ACCESSIBILITY)))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "returned bitfield %" PK_BITFIELD_FORMAT, values);

	/************************************************************/
	egg_test_title (test, "group bitfield to text (unknown)");
	values = pk_bitfield_from_enums (PK_GROUP_ENUM_UNKNOWN, -1);
	text = pk_group_bitfield_to_string (values);
	if (g_strcmp0 (text, "unknown") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "returned bitfield text %s (%" PK_BITFIELD_FORMAT ")", text, values);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "group bitfield to text (first and last)");
	values = pk_bitfield_from_enums (PK_GROUP_ENUM_ACCESSIBILITY, PK_GROUP_ENUM_UNKNOWN, -1);
	text = pk_group_bitfield_to_string (values);
	if (g_strcmp0 (text, "unknown;accessibility") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "returned bitfield text %s (%" PK_BITFIELD_FORMAT ")", text, values);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "group bitfield to text (random)");
	values = pk_bitfield_from_enums (PK_GROUP_ENUM_UNKNOWN, PK_GROUP_ENUM_REPOS, -1);
	text = pk_group_bitfield_to_string (values);
	if (g_strcmp0 (text, "unknown;repos") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "returned bitfield text %s (%" PK_BITFIELD_FORMAT ")", text, values);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "priority check missing");
	values = pk_bitfield_value (PK_ROLE_ENUM_SEARCH_DETAILS) |
		 pk_bitfield_value (PK_ROLE_ENUM_SEARCH_GROUP);
	value = pk_bitfield_contain_priority (values, PK_ROLE_ENUM_SEARCH_FILE, -1);
	if (value == -1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "returned priority %i when should be missing", value);

	/************************************************************/
	egg_test_title (test, "priority check first");
	value = pk_bitfield_contain_priority (values, PK_ROLE_ENUM_SEARCH_GROUP, -1);
	if (value == PK_ROLE_ENUM_SEARCH_GROUP)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "returned wrong value; %i", value);

	/************************************************************/
	egg_test_title (test, "priority check second, correct");
	value = pk_bitfield_contain_priority (values, PK_ROLE_ENUM_SEARCH_FILE, PK_ROLE_ENUM_SEARCH_GROUP, -1);
	if (value == PK_ROLE_ENUM_SEARCH_GROUP)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "returned wrong value; %i", value);

	egg_test_end (test);
}
#endif

