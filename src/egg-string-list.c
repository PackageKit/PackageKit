/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>

#include "egg-debug.h"
#include "egg-obj-list.h"
#include "egg-string.h"
#include "egg-string-list.h"

static void     egg_string_list_class_init	(EggStringListClass	*klass);
static void     egg_string_list_init		(EggStringList		*string_list);
static void     egg_string_list_finalize	(GObject		*object);

#define EGG_STRING_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), EGG_TYPE_STRING_LIST, EggStringListPrivate))

G_DEFINE_TYPE (EggStringList, egg_string_list, EGG_TYPE_OBJ_LIST)

/**
 * egg_string_list_add_strv:
 **/
void
egg_string_list_add_strv (EggStringList *list, gchar **data)
{
	guint i;
	guint len;
	len = g_strv_length (data);
	for (i=0; i<len; i++)
		egg_obj_list_add (EGG_OBJ_LIST(list), data[i]);
}

/**
 * egg_string_list_index:
 **/
inline const gchar *
egg_string_list_index (const EggStringList *list, guint index)
{
	return (const gchar *) egg_obj_list_index (EGG_OBJ_LIST(list), index);
}

/**
 * egg_string_list_print:
 **/
void
egg_string_list_print (EggStringList *list)
{
	guint i;
	const gchar *data;

	for (i=0; i<EGG_OBJ_LIST(list)->len; i++) {
		data = egg_string_list_index (list, i);
		egg_debug ("list[%i] = %s", i, data);
	}
}

/**
 * egg_string_list_from_string_func:
 **/
static gpointer
egg_string_list_from_string_func (const gchar *data)
{
	return (gpointer) g_strdup (data);
}

/**
 * egg_string_list_to_string_func:
 **/
static gchar *
egg_string_list_to_string_func (gconstpointer data)
{
	return g_strdup (data);
}

/**
 * egg_string_list_class_init:
 * @klass: The EggStringListClass
 **/
static void
egg_string_list_class_init (EggStringListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = egg_string_list_finalize;
}

/**
 * egg_string_list_init:
 * @list: This class instance
 **/
static void
egg_string_list_init (EggStringList *list)
{
	egg_obj_list_set_compare (EGG_OBJ_LIST(list), (EggObjListCompareFunc) g_strcmp0);
	egg_obj_list_set_copy (EGG_OBJ_LIST(list), (EggObjListCopyFunc) g_strdup);
	egg_obj_list_set_free (EGG_OBJ_LIST(list), (EggObjListFreeFunc) g_free);
	egg_obj_list_set_to_string (EGG_OBJ_LIST(list), (EggObjListToStringFunc) egg_string_list_to_string_func);
	egg_obj_list_set_from_string (EGG_OBJ_LIST(list), (EggObjListFromStringFunc) egg_string_list_from_string_func);
}

/**
 * egg_string_list_finalize:
 * @object: The object to finalize
 **/
static void
egg_string_list_finalize (GObject *object)
{
	G_OBJECT_CLASS (egg_string_list_parent_class)->finalize (object);
}

/**
 * egg_string_list_new:
 *
 * Return value: a new EggStringList object.
 **/
EggStringList *
egg_string_list_new (void)
{
	EggStringList *list;
	list = g_object_new (EGG_TYPE_STRING_LIST, NULL);
	return EGG_STRING_LIST (list);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_string_list_test (EggTest *test)
{
	EggStringList *list;
	EggStringList *list2;

	if (!egg_test_start (test, "EggStringList"))
		return;

	/************************************************************/
	egg_test_title (test, "create new list");
	list = egg_string_list_new ();
	egg_test_assert (test, list != NULL);

	/************************************************************/
	egg_test_title (test, "length zero");
	egg_test_assert (test, EGG_OBJ_LIST(list)->len == 0);

	/************************************************************/
	egg_test_title (test, "add stuff to list");
	egg_obj_list_add (EGG_OBJ_LIST(list), "dave");
	egg_obj_list_add (EGG_OBJ_LIST(list), "mark");
	egg_obj_list_add (EGG_OBJ_LIST(list), "foo");
	egg_obj_list_add (EGG_OBJ_LIST(list), "foo");
	egg_obj_list_add (EGG_OBJ_LIST(list), "bar");
	egg_test_assert (test, EGG_OBJ_LIST(list)->len == 5);

	/************************************************************/
	egg_test_title (test, "create second list");
	list2 = egg_string_list_new ();
	egg_obj_list_add (EGG_OBJ_LIST(list2), "mark");
	egg_test_assert (test, EGG_OBJ_LIST(list2)->len == 1);

	/************************************************************/
	egg_test_title (test, "append the lists");
	egg_obj_list_add_list (EGG_OBJ_LIST(list), EGG_OBJ_LIST(list2));
	egg_test_assert (test, EGG_OBJ_LIST(list)->len == 6);

	/************************************************************/
	egg_test_title (test, "remove duplicates");
	egg_obj_list_remove_duplicate (EGG_OBJ_LIST(list));
	egg_test_assert (test, EGG_OBJ_LIST(list)->len == 4);

	/************************************************************/
	egg_test_title (test, "remove one list from another");
	egg_obj_list_add_list (EGG_OBJ_LIST(list), EGG_OBJ_LIST(list2)); //dave,mark,foo,bar,mark
	egg_obj_list_remove_list (EGG_OBJ_LIST(list), EGG_OBJ_LIST(list2));
	egg_test_assert (test, EGG_OBJ_LIST(list)->len == 3);

	g_object_unref (list2);
	g_object_unref (list);
	egg_test_end (test);
}
#endif

