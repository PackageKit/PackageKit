/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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
#include "pk-enum.h"
#include "pk-enum-list.h"

static void     pk_enum_list_class_init		(PkEnumListClass *klass);
static void     pk_enum_list_init		(PkEnumList      *enum_list);
static void     pk_enum_list_finalize		(GObject         *object);

#define PK_ENUM_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_ENUM_LIST, PkEnumListPrivate))

struct PkEnumListPrivate
{
	PkEnumListType		 type;
	GPtrArray		*data;
};

G_DEFINE_TYPE (PkEnumList, pk_enum_list, G_TYPE_OBJECT)

/**
 * pk_enum_list_set_type:
 **/
gboolean
pk_enum_list_set_type (PkEnumList *elist, PkEnumListType type)
{
	elist->priv->type = type;
	return TRUE;
}

/**
 * pk_enum_list_append_multiple:
 **/
gboolean
pk_enum_list_append_multiple (PkEnumList *elist, guint value, ...)
{
	va_list args;
	guint i;
	guint value_temp;

	/* create a new list. A list must have at least one entry */
	g_ptr_array_add (elist->priv->data, GUINT_TO_POINTER(value));

	/* process the valist */
	va_start (args, value);
	for (i=0;; i++) {
		value_temp = va_arg (args, guint);
		if (value_temp == 0) break;
		g_ptr_array_add (elist->priv->data, GUINT_TO_POINTER(value_temp));
	}
	va_end (args);

	return TRUE;
}


/**
 * pk_enum_list_from_string:
 **/
gboolean
pk_enum_list_from_string (PkEnumList *elist, const gchar *enums)
{
	gchar **sections;
	guint i;
	guint value_temp = 0;

	if (enums == NULL) {
		pk_warning ("enums null");
		return FALSE;
	}

	/* split by delimeter ';' */
	sections = g_strsplit (enums, ";", 0);
	for (i=0; sections[i]; i++) {
		if (elist->priv->type == PK_ENUM_LIST_TYPE_ACTION) {
			value_temp = pk_action_enum_from_text (sections[i]);
		} else {
			pk_error ("unknown type %i (did you use pk_enum_list_set_type?)", elist->priv->type);
		}
		g_ptr_array_add (elist->priv->data, GUINT_TO_POINTER(value_temp));
	}
	g_strfreev (sections);
	return TRUE;
}

/**
 * pk_enum_list_to_string:
 **/
gchar *
pk_enum_list_to_string (PkEnumList *elist)
{
	guint i;
	GString *string;
	guint value;
	guint length;
	const gchar *text = NULL;

	length = elist->priv->data->len;
	if (length == 0) {
		return g_strdup ("none");
	}

	string = g_string_new ("");
	for (i=0; i<length; i++) {
		value = GPOINTER_TO_UINT (g_ptr_array_index (elist->priv->data, i));
		if (elist->priv->type == PK_ENUM_LIST_TYPE_ACTION) {
			text = pk_action_enum_to_text (value);
		} else {
			pk_error ("unknown type %i (did you use pk_enum_list_set_type?)", elist->priv->type);
		}
		g_string_append (string, text);
		g_string_append (string, ";");
	}

	/* remove last ';' */
	g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

/**
 * pk_enum_list_print:
 **/
gboolean
pk_enum_list_print (PkEnumList *elist)
{
	guint i;
	guint value;
	const gchar *text = NULL;

	if (elist->priv->type == PK_ENUM_LIST_TYPE_ACTION) {
		g_print ("Printing actions:\n");
	}
	for (i=0; i<elist->priv->data->len; i++) {
		value = GPOINTER_TO_UINT (g_ptr_array_index (elist->priv->data, i));
		if (elist->priv->type == PK_ENUM_LIST_TYPE_ACTION) {
			text = pk_action_enum_to_text (value);
		} else {
			pk_error ("unknown type %i (did you use pk_enum_list_set_type?)", elist->priv->type);
		}
		g_print ("%s\n", text);
	}

	return TRUE;
}

/**
 * pk_enum_list_append:
 **/
gboolean
pk_enum_list_append (PkEnumList *elist, guint value)
{
	g_ptr_array_add (elist->priv->data, GUINT_TO_POINTER(value));
	return TRUE;
}

/**
 * pk_enum_list_contains:
 **/
gboolean
pk_enum_list_contains (PkEnumList *elist, guint value)
{
	guint i;
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
	ret = pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_ACTION);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get empty list");
	text = pk_enum_list_to_string (elist);
	if (strcmp (text, "none") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid '%s', should be 'none'", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "append single");
	ret = pk_enum_list_append (elist, PK_ACTION_ENUM_SEARCH_NAME);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get single list");
	text = pk_enum_list_to_string (elist);
	if (strcmp (text, "search-name") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid '%s', should be 'search-name'", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "add multiple");
	ret = pk_enum_list_append_multiple (elist, PK_ACTION_ENUM_SEARCH_DETAILS, PK_ACTION_ENUM_SEARCH_GROUP, 0);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get multiple list");
	text = pk_enum_list_to_string (elist);
	if (strcmp (text, "search-name;search-details;search-group") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid '%s', should be 'search-name;search-details;search-group'", text);
	}
	g_free (text);

	g_object_unref (elist);
	libst_end (test);
}
#endif

