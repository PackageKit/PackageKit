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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <glib.h>

#include "egg-debug.h"
#include "egg-obj-list.h"

#define EGG_OBJ_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), EGG_TYPE_OBJ_LIST, EggObjListPrivate))

struct EggObjListPrivate
{
	EggObjListNewFunc	 func_new;
	EggObjListCopyFunc	 func_copy;
	EggObjListFreeFunc	 func_free;
	EggObjListToStringFunc	 func_to_string;
	EggObjListFromStringFunc func_from_string;
	GPtrArray		*array;
};

G_DEFINE_TYPE (EggObjList, egg_obj_list, G_TYPE_OBJECT)

/**
 * egg_obj_list_set_new:
 * @list: a valid #EggObjList instance
 * @func: typedef'd function
 *
 * Adds a creation func
 **/
void
egg_obj_list_set_new (EggObjList *list, EggObjListNewFunc func)
{
	g_return_if_fail (EGG_IS_OBJ_LIST (list));
	list->priv->func_new = func;
}

/**
 * egg_obj_list_set_copy:
 * @list: a valid #EggObjList instance
 * @func: typedef'd function
 *
 * Adds a copy func
 **/
void
egg_obj_list_set_copy (EggObjList *list, EggObjListCopyFunc func)
{
	g_return_if_fail (EGG_IS_OBJ_LIST (list));
	list->priv->func_copy = func;
}

/**
 * egg_obj_list_set_free:
 * @list: a valid #EggObjList instance
 * @func: typedef'd function
 *
 * Adds a free func
 **/
void
egg_obj_list_set_free (EggObjList *list, EggObjListFreeFunc func)
{
	g_return_if_fail (EGG_IS_OBJ_LIST (list));
	list->priv->func_free = func;
}

/**
 * egg_obj_list_set_to_string:
 * @list: a valid #EggObjList instance
 * @func: typedef'd function
 *
 * Adds a to string func
 **/
void
egg_obj_list_set_to_string (EggObjList *list, EggObjListToStringFunc func)
{
	g_return_if_fail (EGG_IS_OBJ_LIST (list));
	list->priv->func_to_string = func;
}

/**
 * egg_obj_list_set_from_string:
 * @list: a valid #EggObjList instance
 * @func: typedef'd function
 *
 * Adds a from string func
 **/
void
egg_obj_list_set_from_string (EggObjList *list, EggObjListFromStringFunc func)
{
	g_return_if_fail (EGG_IS_OBJ_LIST (list));
	list->priv->func_from_string = func;
}

/**
 * egg_obj_list_clear:
 * @list: a valid #EggObjList instance
 *
 * Clears the package list
 **/
void
egg_obj_list_clear (EggObjList *list)
{
	guint i;
	gpointer obj;
	GPtrArray *array;
	EggObjListFreeFunc func_free;

	g_return_if_fail (EGG_IS_OBJ_LIST (list));

	array = list->priv->array;
	func_free = list->priv->func_free;
	for (i=0; i<array->len; i++) {
		obj = g_ptr_array_index (array, i);
		if (func_free != NULL)
			func_free (obj);
		g_ptr_array_remove (array, obj);
	}
	list->len = 0;
}

/**
 * egg_obj_list_print:
 * @list: a valid #EggObjList instance
 *
 * Prints the package list
 **/
void
egg_obj_list_print (EggObjList *list)
{
	guint i;
	gpointer obj;
	GPtrArray *array;
	gchar *text;
	EggObjListToStringFunc func_to_string;

	g_return_if_fail (list->priv->func_to_string != NULL);
	g_return_if_fail (EGG_IS_OBJ_LIST (list));

	array = list->priv->array;
	func_to_string = list->priv->func_to_string;
	for (i=0; i<array->len; i++) {
		obj = g_ptr_array_index (array, i);
		text = func_to_string (obj);
		g_print ("(%i)\t%s\n", i, text);
		g_free (text);
	}
}

/**
 * egg_obj_list_to_string:
 * @list: a valid #EggObjList instance
 *
 * Converts the list to a newline delimited string
 **/
gchar *
egg_obj_list_to_string (EggObjList *list)
{
	guint i;
	gpointer obj;
	GPtrArray *array;
	gchar *text;
	EggObjListToStringFunc func_to_string;
	GString *string;

	g_return_val_if_fail (list->priv->func_to_string != NULL, NULL);
	g_return_val_if_fail (EGG_IS_OBJ_LIST (list), NULL);

	array = list->priv->array;
	func_to_string = list->priv->func_to_string;
	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		obj = g_ptr_array_index (array, i);
		text = func_to_string (obj);
		g_string_append_printf (string, "%s\n", text);
		g_free (text);
	}
	/* remove trailing newline */
	if (string->len != 0)
		g_string_set_size (string, string->len-1);

	return g_string_free (string, FALSE);
}

/**
 * egg_obj_list_add:
 * @list: a valid #EggObjList instance
 * @obj: a valid #gpointer object
 *
 * Adds a copy of the object to the list
 **/
void
egg_obj_list_add (EggObjList *list, const gpointer obj)
{
	gpointer obj_new;

	g_return_if_fail (EGG_IS_OBJ_LIST (list));
	g_return_if_fail (obj != NULL);
	g_return_if_fail (list->priv->func_copy != NULL);

	/* TODO: are we already in the list? */
	obj_new = list->priv->func_copy (obj);
	g_ptr_array_add (list->priv->array, obj_new);
	list->len = list->priv->array->len;
}

/**
 * egg_package_list_add_list:
 *
 * Makes a deep copy of the list
 **/
void
egg_package_list_add_list (EggObjList *list, const EggObjList *data)
{
	guint i;
	gpointer obj;

	g_return_if_fail (EGG_IS_OBJ_LIST (list));
	g_return_if_fail (EGG_IS_OBJ_LIST (data));

	/* add data items to list */
	for (i=0; i < data->len; i++) {
		obj = egg_obj_list_index (data, i);
		egg_obj_list_add (list, obj);
	}
}

/**
 * egg_obj_list_remove:
 * @list: a valid #EggObjList instance
 * @obj: a valid #gpointer object
 *
 * Return value: TRUE is we removed something
 *
 * Removes an item from a list
 **/
gboolean
egg_obj_list_remove (EggObjList *list, const gpointer obj)
{
	gboolean ret;
	gpointer obj_new;

	g_return_val_if_fail (EGG_IS_OBJ_LIST (list), FALSE);
	g_return_val_if_fail (obj != NULL, FALSE);
	g_return_val_if_fail (list->priv->func_free != NULL, FALSE);

	/* the pointers point to the same thing */
	obj_new = (gpointer) obj;
	ret = g_ptr_array_remove (list->priv->array, obj_new);
	if (!ret)
		return FALSE;
	list->priv->func_free (obj_new);
	list->len = list->priv->array->len;
	return TRUE;
}

/**
 * egg_obj_list_remove_index:
 * @list: a valid #EggObjList instance
 * @index: the number to remove
 *
 * Return value: TRUE is we removed something
 *
 * Removes an item from a list
 **/
gboolean
egg_obj_list_remove_index (EggObjList *list, guint index)
{
	gpointer obj;

	g_return_val_if_fail (EGG_IS_OBJ_LIST (list), FALSE);
	g_return_val_if_fail (list->priv->func_free != NULL, FALSE);

	/* get the object */
	obj = g_ptr_array_remove_index (list->priv->array, index);
	if (obj == NULL)
		return FALSE;
	list->priv->func_free (obj);
	list->len = list->priv->array->len;
	return TRUE;
}

/**
 * egg_obj_list_to_file:
 * @list: a valid #EggObjList instance
 * @filename: a filename
 *
 * Saves a copy of the list to a file
 **/
gboolean
egg_obj_list_to_file (EggObjList *list, const gchar *filename)
{
	guint i;
	gpointer obj;
	gchar *part;
	GString *string;
	gboolean ret = TRUE;
	GError *error = NULL;
	EggObjListFreeFunc func_free;
	EggObjListToStringFunc func_to_string;

	g_return_val_if_fail (EGG_IS_OBJ_LIST (list), FALSE);
	g_return_val_if_fail (list->priv->func_to_string != NULL, FALSE);
	g_return_val_if_fail (list->priv->func_free != NULL, FALSE);

	func_free = list->priv->func_free;
	func_to_string = list->priv->func_to_string;

	/* generate data */
	string = g_string_new ("");
	for (i=0; i<list->len; i++) {
		obj = egg_obj_list_index (list, i);
		part = func_to_string (obj);
		if (part == NULL) {
			ret = FALSE;
			break;
		}
		g_string_append_printf (string, "%s\n", part);
		g_free (part);
	}
	part = g_string_free (string, FALSE);

	/* we failed to convert to string */
	if (!ret) {
		egg_warning ("failed to convert");
		goto out;
	}

	/* save to disk */
	ret = g_file_set_contents (filename, part, -1, &error);
	if (!ret) {
		egg_warning ("failed to set data: %s", error->message);
		g_error_free (error);
		goto out;
	}
	egg_debug ("saved %s", filename);

out:
	g_free (part);
	return ret;
}

/**
 * egg_obj_list_from_file:
 * @list: a valid #EggObjList instance
 * @filename: a filename
 *
 * Appends the list from a file
 **/
gboolean
egg_obj_list_from_file (EggObjList *list, const gchar *filename)
{
	gboolean ret;
	GError *error = NULL;
	gchar *data = NULL;
	gchar **parts = NULL;
	guint i;
	guint length;
	gpointer obj;
	EggObjListFreeFunc func_free;
	EggObjListFromStringFunc func_from_string;

	g_return_val_if_fail (EGG_IS_OBJ_LIST (list), FALSE);
	g_return_val_if_fail (list->priv->func_from_string != NULL, FALSE);
	g_return_val_if_fail (list->priv->func_free != NULL, FALSE);

	func_free = list->priv->func_free;
	func_from_string = list->priv->func_from_string;

	/* do we exist */
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		egg_debug ("failed to get data from %s as file does not exist", filename);
		goto out;
	}

	/* get contents */
	ret = g_file_get_contents (filename, &data, NULL, &error);
	if (!ret) {
		egg_warning ("failed to get data: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* split by line ending */
	parts = g_strsplit (data, "\n", 0);
	length = g_strv_length (parts);
	if (length == 0) {
		egg_debug ("no data in %s", filename);
		goto out;
	}

	/* add valid entries */
	egg_debug ("loading %i items of data from %s", length, filename);
	for (i=0; i<length-1; i++) {
		obj = func_from_string (parts[i]);
		if (obj != NULL)
			egg_obj_list_add (list, obj);
		func_free (obj);
	}

out:
	g_strfreev (parts);
	g_free (data);

	return ret;
}

/**
 * egg_obj_list_index:
 * @list: a valid #EggObjList instance
 * @index: the element to return
 *
 * Gets an object from the list
 **/
const gpointer
egg_obj_list_index (const EggObjList *list, guint index)
{
	gpointer obj;

	g_return_val_if_fail (EGG_IS_OBJ_LIST (list), NULL);

	obj = g_ptr_array_index (list->priv->array, index);
	return (const gpointer) obj;
}

/**
 * egg_obj_list_finalize:
 * @object: a valid #EggObjList instance
 **/
static void
egg_obj_list_finalize (GObject *object)
{
	EggObjListFreeFunc func_free;
	gpointer obj;
	guint i;
	EggObjList *list;
	GPtrArray *array;
	g_return_if_fail (EGG_IS_OBJ_LIST (object));
	list = EGG_OBJ_LIST (object);

	array = list->priv->array;
	func_free = list->priv->func_free;
	for (i=0; i<array->len; i++) {
		obj = g_ptr_array_index (array, i);
		if (func_free != NULL)
			func_free (obj);
	}
	g_ptr_array_free (array, TRUE);

	G_OBJECT_CLASS (egg_obj_list_parent_class)->finalize (object);
}

/**
 * egg_obj_list_class_init:
 * @klass: a valid #EggObjListClass instance
 **/
static void
egg_obj_list_class_init (EggObjListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = egg_obj_list_finalize;
	g_type_class_add_private (klass, sizeof (EggObjListPrivate));
}

/**
 * egg_obj_list_init:
 * @list: a valid #EggObjList instance
 *
 * Initializes the obj_list class.
 **/
static void
egg_obj_list_init (EggObjList *list)
{
	list->priv = EGG_OBJ_LIST_GET_PRIVATE (list);
	list->priv->func_new = NULL;
	list->priv->func_copy = NULL;
	list->priv->func_free = NULL;
	list->priv->func_to_string = NULL;
	list->priv->func_from_string = NULL;
	list->priv->array = g_ptr_array_new ();
	list->len = list->priv->array->len;
}

/**
 * egg_obj_list_new:
 *
 * Return value: A new list class instance.
 **/
EggObjList *
egg_obj_list_new (void)
{
	EggObjList *list;
	list = g_object_new (EGG_TYPE_OBJ_LIST, NULL);
	return EGG_OBJ_LIST (list);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_obj_list_test (EggTest *test)
{
	EggObjList *list;

	if (!egg_test_start (test, "EggObjList"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	list = egg_obj_list_new ();
	if (list != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	g_object_unref (list);

	egg_test_end (test);
}
#endif
