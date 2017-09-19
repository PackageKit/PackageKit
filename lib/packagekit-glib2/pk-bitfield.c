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
 * SECTION:pk-bitfield
 * @title: PkBitfield
 * @short_description: Bitfield object
 *
 * #PkBitfield provides a method of using enumerations in a bitfield.
 */

#include "config.h"

#include <glib.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-bitfield.h>

/**
 * pk_bitfield_contain_priority:
 * @values: a valid bitfield instance
 * @value: the first value we are searching for
 * @...: additional values to search for, terminated by -1
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
	for (i = 0;; i++) {
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
 * @value: the first value we want to add to the bitfield
 * @...: other values to add, terminated by -1
 *
 * Create a bitfield with the suppied values set.
 *
 * Return value: a #PkBitfield, or 0 if invalid
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
	for (i = 0;; i++) {
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
	for (i = 0; i < PK_ROLE_ENUM_LAST; i++) {
		if ((roles & pk_bitfield_value (i)) == 0)
			continue;
		g_string_append_printf (string, "%s;", pk_role_enum_to_string (i));
	}
	/* do we have a no bitfield? \n */
	if (string->len == 0) {
		g_warning ("not valid!");
		g_string_append (string, pk_role_enum_to_string (PK_ROLE_ENUM_UNKNOWN));
	} else {
		/* remove last \n */
		g_string_set_size (string, string->len - 1);
	}
	return g_string_free (string, FALSE);
}

/**
 * pk_role_bitfield_from_string:
 * @roles: the enumerated constant value, e.g. "search-file;update-system"
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
	guint length;
	guint i;
	PkRoleEnum role;
	g_auto(GStrv) split = NULL;

	split = g_strsplit (roles, ";", 0);
	if (split == NULL) {
		g_warning ("unable to split");
		return 0;
	}

	length = g_strv_length (split);
	for (i = 0; i < length; i++) {
		role = pk_role_enum_from_string (split[i]);
		if (role != PK_ROLE_ENUM_UNKNOWN)
			roles_enum += pk_bitfield_value (role);
	}
	return roles_enum;
}

/**
 * pk_group_bitfield_to_string:
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
	for (i = 0; i < PK_GROUP_ENUM_LAST; i++) {
		if ((groups & pk_bitfield_value (i)) == 0)
			continue;
		g_string_append_printf (string, "%s;", pk_group_enum_to_string (i));
	}
	/* do we have a no bitfield? \n */
	if (string->len == 0) {
		g_warning ("not valid!");
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
	guint length;
	guint i;
	PkGroupEnum group;
	g_auto(GStrv) split = NULL;

	split = g_strsplit (groups, ";", 0);
	if (split == NULL) {
		g_warning ("unable to split");
		return 0;
	}

	length = g_strv_length (split);
	for (i = 0; i < length; i++) {
		group = pk_group_enum_from_string (split[i]);
		if (group != PK_GROUP_ENUM_UNKNOWN)
			groups_enum += pk_bitfield_value (group);
	}
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
	for (i = 0; i < PK_FILTER_ENUM_LAST; i++) {
		if ((filters & pk_bitfield_value (i)) == 0)
			continue;
		g_string_append_printf (string, "%s;", pk_filter_enum_to_string (i));
	}
	/* do we have a 'none' filter? \n */
	if (string->len == 0) {
		g_warning ("not valid!");
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
	guint length;
	guint i;
	PkFilterEnum filter;
	g_auto(GStrv) split = NULL;

	split = g_strsplit (filters, ";", 0);
	if (split == NULL) {
		g_warning ("unable to split");
		return 0;
	}

	length = g_strv_length (split);
	for (i = 0; i < length; i++) {
		filter = pk_filter_enum_from_string (split[i]);
		if (filter != PK_FILTER_ENUM_UNKNOWN)
			filters_enum += pk_bitfield_value (filter);
	}
	return filters_enum;
}

/**
 * pk_transaction_flag_bitfield_to_string:
 * @transaction_flags: The enumerated type values
 *
 * Converts a enumerated type bitfield to its text representation
 *
 * Return value: the enumerated constant value, e.g. "only-trusted;simulate"
 *
 * Since: 0.8.1
 **/
gchar *
pk_transaction_flag_bitfield_to_string (PkBitfield transaction_flags)
{
	GString *string;
	guint i;

	/* shortcut */
	if (transaction_flags == 0)
		return g_strdup (pk_transaction_flag_enum_to_string (PK_TRANSACTION_FLAG_ENUM_NONE));

	string = g_string_new ("");
	for (i = 0; i < PK_TRANSACTION_FLAG_ENUM_LAST; i++) {
		if ((transaction_flags & pk_bitfield_value (i)) == 0)
			continue;
		g_string_append_printf (string, "%s;", pk_transaction_flag_enum_to_string (i));
	}
	/* do we have a 'none' transaction_flag? \n */
	if (string->len == 0) {
		g_warning ("not valid!");
		g_string_append (string, pk_transaction_flag_enum_to_string (PK_TRANSACTION_FLAG_ENUM_NONE));
	} else {
		/* remove last \n */
		g_string_set_size (string, string->len - 1);
	}
	return g_string_free (string, FALSE);
}

/**
 * pk_transaction_flag_bitfield_from_string:
 * @transaction_flags: the enumerated constant value, e.g. "only-trusted;simulate"
 *
 * Converts text representation to its enumerated type bitfield, or 0 for invalid
 *
 * Return value: The enumerated type values
 *
 * Since: 0.8.1
 **/
PkBitfield
pk_transaction_flag_bitfield_from_string (const gchar *transaction_flags)
{
	PkBitfield transaction_flags_enum = 0;
	guint length;
	guint i;
	PkFilterEnum transaction_flag;
	g_auto(GStrv) split = NULL;

	split = g_strsplit (transaction_flags, ";", 0);
	if (split == NULL) {
		g_warning ("unable to split");
		return 0;
	}

	length = g_strv_length (split);
	for (i = 0; i < length; i++) {
		transaction_flag = pk_transaction_flag_enum_from_string (split[i]);
		transaction_flags_enum += pk_bitfield_value (transaction_flag);
	}
	return transaction_flags_enum;
}
