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
 * SECTION:pk-enum-list
 * @short_description: Common functions to manifulate lists of enumerated types
 *
 * This file contains functions that can manage lists of enumerated values of
 * different types.
 * These functions will be much quicker than manipulating strings directly.
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

#include "pk-common.h"
#include "pk-debug.h"
#include "pk-enum.h"
#include "pk-enum-list.h"

static void     pk_enum_list_class_init		(PkEnumListClass *klass);
static void     pk_enum_list_init		(PkEnumList      *enum_list);
static void     pk_enum_list_finalize		(GObject         *object);

#define PK_ENUM_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_ENUM_LIST, PkEnumListPrivate))

/**
 * PkEnumListPrivate:
 *
 * Private #PkEnumList data
 **/
struct _PkEnumListPrivate
{
	PkEnumListType		 type;
	GPtrArray		*data;
};

G_DEFINE_TYPE (PkEnumList, pk_enum_list, G_TYPE_OBJECT)

/**
 * pk_enum_list_set_type:
 * @elist: a valid #PkEnumList instance
 * @type: the type of list this should be
 *
 * This function sets the type of list. You don't /need/ to use this function,
 * but is required if you print or get the list as we need to know what
 * pk_xxxx_enum_to_text function to use for each part.
 *
 * Return value: %TRUE if we set the list type
 **/
gboolean
pk_enum_list_set_type (PkEnumList *elist, PkEnumListType type)
{
	g_return_val_if_fail (elist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENUM_LIST (elist), FALSE);
	elist->priv->type = type;
	return TRUE;
}

/**
 * pk_enum_list_append_multiple:
 * @elist: a valid #PkEnumList instance
 * @value: the initial value
 *
 * Set a many items into a list in one method. Always terminate the enum
 * list with the value -1
 *
 * Return value: %TRUE if we set the data
 **/
gboolean
pk_enum_list_append_multiple (PkEnumList *elist, gint value, ...)
{
	va_list args;
	guint i;
	guint value_temp;

	g_return_val_if_fail (elist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENUM_LIST (elist), FALSE);

	/* create a new list. A list must have at least one entry */
	g_ptr_array_add (elist->priv->data, GINT_TO_POINTER(value));

	/* process the valist */
	va_start (args, value);
	for (i=0;; i++) {
		value_temp = va_arg (args, gint);
		if (value_temp == -1) break;
		g_ptr_array_add (elist->priv->data, GUINT_TO_POINTER(value_temp));
	}
	va_end (args);

	return TRUE;
}

/**
 * pk_enum_list_contains_priority:
 * @elist: a valid #PkEnumList instance
 * @value: the values we are searching for
 *
 * Finds elements in a list, but with priority going to the preceeding entry
 *
 * Return value: The return enumerated type, or -1 if none are found
 **/
gint
pk_enum_list_contains_priority (PkEnumList *elist, gint value, ...)
{
	va_list args;
	guint i;
	guint value_temp;
	gint retval = -1;

	g_return_val_if_fail (elist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENUM_LIST (elist), FALSE);

	/* we must query at least one thing */
	if (pk_enum_list_contains (elist, value) == TRUE) {
		return value;
	}

	/* process the valist */
	va_start (args, value);
	for (i=0;; i++) {
		value_temp = va_arg (args, gint);
		/* do we have this one? */
		if (pk_enum_list_contains (elist, value_temp) == TRUE) {
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
 * pk_enum_list_from_string:
 * @elist: a valid #PkEnumList instance
 * @enums: a text representation of the list, e.g. "search-name;search-details"
 *
 * Set the list with a seed string. Converting the seed string once allows us
 * to deal with raw enumerated integers, which is often much faster.
 *
 * Return value: %TRUE if we appended the data
 **/
gboolean
pk_enum_list_from_string (PkEnumList *elist, const gchar *enums)
{
	gchar **sections;
	guint i;
	guint value_temp = 0;

	g_return_val_if_fail (elist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENUM_LIST (elist), FALSE);

	if (enums == NULL) {
		pk_warning ("enums null");
		return FALSE;
	}

	/* check if we have nothing */
	if (pk_strequal (enums, "none") == TRUE) {
		pk_debug ("no values");
		return TRUE;
	}

	/* split by delimeter ';' */
	sections = g_strsplit (enums, ";", 0);
	for (i=0; sections[i]; i++) {
		if (elist->priv->type == PK_ENUM_LIST_TYPE_ROLE) {
			value_temp = pk_role_enum_from_text (sections[i]);
		} else if (elist->priv->type == PK_ENUM_LIST_TYPE_GROUP) {
			value_temp = pk_group_enum_from_text (sections[i]);
		} else if (elist->priv->type == PK_ENUM_LIST_TYPE_FILTER) {
			value_temp = pk_filter_enum_from_text (sections[i]);
		} else if (elist->priv->type == PK_ENUM_LIST_TYPE_STATUS) {
			value_temp = pk_status_enum_from_text (sections[i]);
		} else {
			pk_error ("unknown type %i (did you use pk_enum_list_set_type?)", elist->priv->type);
		}
		g_ptr_array_add (elist->priv->data, GUINT_TO_POINTER(value_temp));
	}
	g_strfreev (sections);
	return TRUE;
}

/**
 * pk_enum_list_get_item_text:
 **/
static const gchar *
pk_enum_list_get_item_text (PkEnumList *elist, guint value)
{
	const gchar *text = NULL;

	g_return_val_if_fail (elist != NULL, NULL);
	g_return_val_if_fail (PK_IS_ENUM_LIST (elist), NULL);

	if (elist->priv->type == PK_ENUM_LIST_TYPE_ROLE) {
		text = pk_role_enum_to_text (value);
	} else if (elist->priv->type == PK_ENUM_LIST_TYPE_GROUP) {
		text = pk_group_enum_to_text (value);
	} else if (elist->priv->type == PK_ENUM_LIST_TYPE_FILTER) {
		text = pk_filter_enum_to_text (value);
	} else if (elist->priv->type == PK_ENUM_LIST_TYPE_STATUS) {
		text = pk_status_enum_to_text (value);
	} else {
		pk_warning ("unknown type %i (did you use pk_enum_list_set_type?)", elist->priv->type);
	}
	return text;
}

/**
 * pk_enum_list_to_string:
 * @elist: a valid #PkEnumList instance
 *
 * Converts the enumerated list back to a string.
 *
 * Return value: A string representing the enumerated list,
 *  e.g. "search-name;search-details"
 **/
gchar *
pk_enum_list_to_string (PkEnumList *elist)
{
	guint i;
	GString *string;
	guint value;
	guint length;
	const gchar *text = NULL;

	g_return_val_if_fail (elist != NULL, NULL);
	g_return_val_if_fail (PK_IS_ENUM_LIST (elist), NULL);

	length = elist->priv->data->len;
	if (length == 0) {
		return g_strdup ("none");
	}

	string = g_string_new ("");
	for (i=0; i<length; i++) {
		value = GPOINTER_TO_UINT (g_ptr_array_index (elist->priv->data, i));
		text = pk_enum_list_get_item_text (elist, value);
		g_string_append (string, text);
		g_string_append (string, ";");
	}

	/* remove last ';' */
	g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

/**
 * pk_enum_list_print:
 * @elist: a valid #PkEnumList instance
 *
 * Prints the enumerated list. This is most useful for debugging.
 *
 * Return value: %TRUE for success.
 **/
gboolean
pk_enum_list_print (PkEnumList *elist)
{
	guint i;
	guint value;
	const gchar *text = NULL;

	g_return_val_if_fail (elist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENUM_LIST (elist), FALSE);

	if (elist->priv->type == PK_ENUM_LIST_TYPE_ROLE) {
		g_print ("Printing actions:\n");
	} else if (elist->priv->type == PK_ENUM_LIST_TYPE_GROUP) {
		g_print ("Printing groups:\n");
	} else if (elist->priv->type == PK_ENUM_LIST_TYPE_FILTER) {
		g_print ("Printing filters:\n");
	} else if (elist->priv->type == PK_ENUM_LIST_TYPE_STATUS) {
		g_print ("Printing status:\n");
	}
	for (i=0; i<elist->priv->data->len; i++) {
		value = GPOINTER_TO_UINT (g_ptr_array_index (elist->priv->data, i));
		text = pk_enum_list_get_item_text (elist, value);
		g_print ("%s\n", text);
	}

	return TRUE;
}

/**
 * pk_enum_list_size:
 * @elist: a valid #PkEnumList instance
 *
 * Return value: the size of the enumerated list.
 **/
guint
pk_enum_list_size (PkEnumList *elist)
{
	g_return_val_if_fail (elist != NULL, 0);
	g_return_val_if_fail (PK_IS_ENUM_LIST (elist), 0);
	return elist->priv->data->len;
}

/**
 * pk_enum_list_get_item:
 * @elist: a valid #PkEnumList instance
 * @item: the item number of the list
 *
 * Return value: the enum value for this position, or zero if error.
 **/
guint
pk_enum_list_get_item (PkEnumList *elist, guint item)
{
	g_return_val_if_fail (elist != NULL, 0);
	g_return_val_if_fail (PK_IS_ENUM_LIST (elist), 0);
	if (item >= elist->priv->data->len) {
		pk_warning ("getting item over length");
		return 0;
	}
	return GPOINTER_TO_UINT (g_ptr_array_index (elist->priv->data, item));
}

/**
 * pk_enum_list_append:
 * @elist: a valid #PkEnumList instance
 * @value: the value to add
 *
 * Set a single item into a list.
 *
 * Return value: %TRUE if we set the data, %FALSE if it already existed
 **/
gboolean
pk_enum_list_append (PkEnumList *elist, guint value)
{
	guint i;

	g_return_val_if_fail (elist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENUM_LIST (elist), FALSE);

	for (i=0; i<elist->priv->data->len; i++) {
		if (GPOINTER_TO_UINT (g_ptr_array_index (elist->priv->data, i)) == value) {
			pk_debug ("trying to append item already in list");
			return FALSE;
		}
	}
	g_ptr_array_add (elist->priv->data, GUINT_TO_POINTER(value));
	return TRUE;
}

/**
 * pk_enum_list_remove:
 * @elist: a valid #PkEnumList instance
 * @value: the value to add
 *
 * Removes a single item from a list.
 *
 * Return value: %TRUE if we set the data, %FALSE if it did not exist
 **/
gboolean
pk_enum_list_remove (PkEnumList *elist, guint value)
{
	guint i;

	g_return_val_if_fail (elist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENUM_LIST (elist), FALSE);

	for (i=0; i<elist->priv->data->len; i++) {
		if (GPOINTER_TO_UINT (g_ptr_array_index (elist->priv->data, i)) == value) {
			g_ptr_array_remove_index (elist->priv->data, i);
			return TRUE;
		}
	}
	pk_debug ("cannot find item '%s'", pk_enum_list_get_item_text (elist, value));
	return FALSE;
}

/**
 * pk_enum_list_contains:
 * @elist: a valid #PkEnumList instance
 * @value: the value to search for
 *
 * Searches the list looking for a specific value.
 *
 * Return value: %TRUE if we found the data in the list
 **/
gboolean
pk_enum_list_contains (PkEnumList *elist, guint value)
{
	guint i;

	g_return_val_if_fail (elist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENUM_LIST (elist), FALSE);

	for (i=0; i<elist->priv->data->len; i++) {
		if (GPOINTER_TO_UINT (g_ptr_array_index (elist->priv->data, i)) == value) {
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * pk_enum_list_class_init:
 **/
static void
pk_enum_list_class_init (PkEnumListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_enum_list_finalize;
	g_type_class_add_private (klass, sizeof (PkEnumListPrivate));
}

/**
 * pk_enum_list_init:
 **/
static void
pk_enum_list_init (PkEnumList *elist)
{
	elist->priv = PK_ENUM_LIST_GET_PRIVATE (elist);
	elist->priv->data = g_ptr_array_new ();
	elist->priv->type = PK_ENUM_LIST_TYPE_UNKNOWN;
}

/**
 * pk_enum_list_finalize:
 **/
static void
pk_enum_list_finalize (GObject *object)
{
	PkEnumList *elist;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_ENUM_LIST (object));
	elist = PK_ENUM_LIST (object);
	g_return_if_fail (elist->priv != NULL);

	g_ptr_array_free (elist->priv->data, TRUE);

	G_OBJECT_CLASS (pk_enum_list_parent_class)->finalize (object);
}

/**
 * pk_enum_list_new:
 **/
PkEnumList *
pk_enum_list_new (void)
{
	PkEnumList *elist;
	elist = g_object_new (PK_TYPE_ENUM_LIST, NULL);
	return PK_ENUM_LIST (elist);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_enum_list (LibSelfTest *test)
{
	if (libst_start (test, "PkEnumList", CLASS_AUTO) == FALSE) {
		return;
	}

	PkEnumList *elist;
	gboolean ret;
	gchar *text;
	guint value;

	/************************************************************
	 ****************          ENUM         ******************
	 ************************************************************/
	libst_title (test, "get a new PkEnumList object");
	elist = pk_enum_list_new ();
	if (elist != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "set action builder");
	ret = pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_ROLE);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get empty list");
	text = pk_enum_list_to_string (elist);
	if (pk_strequal (text, "none") == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid '%s', should be 'none'", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "get empty size");
	value = pk_enum_list_size (elist);
	if (value == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid size %i, should be 0", value);
	}

	/************************************************************/
	libst_title (test, "append single");
	ret = pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_NAME);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "append duplicate");
	ret = pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_NAME);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get single size");
	value = pk_enum_list_size (elist);
	if (value == 1) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid size %i, should be 1", value);
	}

	/************************************************************/
	libst_title (test, "check item");
	value = pk_enum_list_get_item (elist, 0);
	if (value == PK_ROLE_ENUM_SEARCH_NAME) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid item %i, should be PK_ROLE_ENUM_SEARCH_NAME", value);
	}

	/************************************************************/
	libst_title (test, "get single list");
	text = pk_enum_list_to_string (elist);
	if (pk_strequal (text, "search-name") == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid '%s', should be 'search-name'", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "add multiple");
	ret = pk_enum_list_append_multiple (elist, PK_ROLE_ENUM_SEARCH_DETAILS, PK_ROLE_ENUM_SEARCH_GROUP, -1);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get multiple size");
	value = pk_enum_list_size (elist);
	if (value == 3) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid size %i, should be 3", value);
	}


	/************************************************************/
	libst_title (test, "priority check missing");
	value = pk_enum_list_contains_priority (elist, PK_ROLE_ENUM_SEARCH_FILE, -1);
	if (value == -1) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "returned priority %i when should be missing", value);
	}

	/************************************************************/
	libst_title (test, "priority check first");
	value = pk_enum_list_contains_priority (elist, PK_ROLE_ENUM_SEARCH_GROUP, -1);
	if (value == PK_ROLE_ENUM_SEARCH_GROUP) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "returned wrong value; %i", value);
	}

	/************************************************************/
	libst_title (test, "priority check second, correct");
	value = pk_enum_list_contains_priority (elist, PK_ROLE_ENUM_SEARCH_FILE, PK_ROLE_ENUM_SEARCH_GROUP, -1);
	if (value == PK_ROLE_ENUM_SEARCH_GROUP) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "returned wrong value; %i", value);
	}

	/************************************************************/
	libst_title (test, "get multiple list");
	text = pk_enum_list_to_string (elist);
	if (pk_strequal (text, "search-name;search-details;search-group") == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid '%s', should be 'search-name;search-details;search-group'", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "remove single");
	ret = pk_enum_list_remove (elist, PK_ROLE_ENUM_SEARCH_DETAILS);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "remove duplicate single");
	ret = pk_enum_list_remove (elist, PK_ROLE_ENUM_SEARCH_DETAILS);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get size after remove");
	value = pk_enum_list_size (elist);
	if (value == 2) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid size %i, should be 2", value);
	}

	g_object_unref (elist);

	/************************************************************/
	libst_title (test, "set none enum list");
	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_ROLE);
	pk_enum_list_from_string (elist, "none");
	if (elist->priv->data->len == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid length %i", elist->priv->data->len);
	}

	/************************************************************/
	libst_title (test, "get none enum list");
	text = pk_enum_list_to_string (elist);
	if (pk_strequal (text, "none") == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid '%s', should be 'none'", text);
	}
	g_free (text);


	g_object_unref (elist);
	libst_end (test);
}
#endif

