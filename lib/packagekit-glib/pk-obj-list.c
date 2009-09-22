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
#include <packagekit-glib/pk-obj-list.h>

#include "egg-debug.h"

#define PK_OBJ_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_OBJ_LIST, PkObjListPrivate))

struct PkObjListPrivate
{
	PkObjListNewFunc	 func_new;
	PkObjListCopyFunc	 func_copy;
	PkObjListFreeFunc	 func_free;
	PkObjListCompareFunc	 func_compare;
	PkObjListEqualFunc	 func_equal;
	PkObjListToStringFunc	 func_to_string;
	PkObjListFromStringFunc func_from_string;
	GPtrArray		*array;
};

G_DEFINE_TYPE (PkObjList, pk_obj_list, G_TYPE_OBJECT)

/**
 * pk_obj_list_set_new:
 * @list: a valid #PkObjList instance
 * @func: typedef'd function
 *
 * Adds a creation func
 **/
void
pk_obj_list_set_new (PkObjList *list, PkObjListNewFunc func)
{
	g_return_if_fail (PK_IS_OBJ_LIST (list));
	list->priv->func_new = func;
}

/**
 * pk_obj_list_set_copy:
 * @list: a valid #PkObjList instance
 * @func: typedef'd function
 *
 * Adds a copy func
 **/
void
pk_obj_list_set_copy (PkObjList *list, PkObjListCopyFunc func)
{
	g_return_if_fail (PK_IS_OBJ_LIST (list));
	list->priv->func_copy = func;
}

/**
 * pk_obj_list_set_free:
 * @list: a valid #PkObjList instance
 * @func: typedef'd function
 *
 * Adds a free func
 **/
void
pk_obj_list_set_free (PkObjList *list, PkObjListFreeFunc func)
{
	g_return_if_fail (PK_IS_OBJ_LIST (list));
	list->priv->func_free = func;
}

/**
 * pk_obj_list_set_compare:
 * @list: a valid #PkObjList instance
 * @func: typedef'd function
 *
 * Adds a compare func
 **/
void
pk_obj_list_set_compare (PkObjList *list, PkObjListCompareFunc func)
{
	g_return_if_fail (PK_IS_OBJ_LIST (list));
	list->priv->func_compare = func;
}

/**
 * pk_obj_list_set_equal:
 * @list: a valid #PkObjList instance
 * @func: typedef'd function
 *
 * Adds a equal func
 **/
void
pk_obj_list_set_equal (PkObjList *list, PkObjListEqualFunc func)
{
	g_return_if_fail (PK_IS_OBJ_LIST (list));
	list->priv->func_equal = func;
}

/**
 * pk_obj_list_set_to_string:
 * @list: a valid #PkObjList instance
 * @func: typedef'd function
 *
 * Adds a to string func
 **/
void
pk_obj_list_set_to_string (PkObjList *list, PkObjListToStringFunc func)
{
	g_return_if_fail (PK_IS_OBJ_LIST (list));
	list->priv->func_to_string = func;
}

/**
 * pk_obj_list_set_from_string:
 * @list: a valid #PkObjList instance
 * @func: typedef'd function
 *
 * Adds a from string func
 **/
void
pk_obj_list_set_from_string (PkObjList *list, PkObjListFromStringFunc func)
{
	g_return_if_fail (PK_IS_OBJ_LIST (list));
	list->priv->func_from_string = func;
}

/**
 * pk_obj_list_get_array:
 * @list: a valid #PkObjList instance
 *
 * Gets a GPtrArray representation of the package list
 **/
const GPtrArray	*
pk_obj_list_get_array (const PkObjList *list)
{
	g_return_val_if_fail (PK_IS_OBJ_LIST (list), NULL);
	return list->priv->array;
}

/**
 * pk_obj_list_sort:
 * @list: a valid #PkObjList instance
 *
 * Sorts the package list
 **/
void
pk_obj_list_sort (PkObjList *list, GCompareFunc sort_func)
{
	g_return_if_fail (PK_IS_OBJ_LIST (list));
	g_ptr_array_sort (list->priv->array, sort_func);
}

/**
 * pk_obj_list_clear:
 * @list: a valid #PkObjList instance
 *
 * Clears the package list
 **/
void
pk_obj_list_clear (PkObjList *list)
{
	GPtrArray *array;
	PkObjListFreeFunc func_free;

	g_return_if_fail (PK_IS_OBJ_LIST (list));

	array = list->priv->array;
	func_free = list->priv->func_free;
	if (func_free != NULL)
		g_ptr_array_foreach (array, (GFunc) func_free, NULL);
	if (array->len > 0)
		g_ptr_array_remove_range (array, 0, array->len);
	list->len = 0;
}

/**
 * pk_obj_list_print:
 * @list: a valid #PkObjList instance
 *
 * Prints the package list
 **/
void
pk_obj_list_print (PkObjList *list)
{
	guint i;
	gpointer obj;
	GPtrArray *array;
	gchar *text;
	PkObjListToStringFunc func_to_string;

	g_return_if_fail (list->priv->func_to_string != NULL);
	g_return_if_fail (PK_IS_OBJ_LIST (list));

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
 * pk_obj_list_to_string:
 * @list: a valid #PkObjList instance
 *
 * Converts the list to a newline delimited string
 **/
gchar *
pk_obj_list_to_string (PkObjList *list)
{
	guint i;
	gpointer obj;
	GPtrArray *array;
	gchar *text;
	PkObjListToStringFunc func_to_string;
	GString *string;

	g_return_val_if_fail (list->priv->func_to_string != NULL, NULL);
	g_return_val_if_fail (PK_IS_OBJ_LIST (list), NULL);

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
 * pk_obj_list_add:
 * @list: a valid #PkObjList instance
 * @obj: a valid #gpointer object
 *
 * Adds a copy of the object to the list
 **/
void
pk_obj_list_add (PkObjList *list, gconstpointer obj)
{
	gpointer obj_new;

	g_return_if_fail (PK_IS_OBJ_LIST (list));
	g_return_if_fail (obj != NULL);
	g_return_if_fail (list->priv->func_copy != NULL);

	/* TODO: are we already in the list? */
	obj_new = list->priv->func_copy (obj);
	g_ptr_array_add (list->priv->array, obj_new);
	list->len = list->priv->array->len;
}

/**
 * pk_obj_list_add_list:
 *
 * Makes a deep copy of the list
 **/
void
pk_obj_list_add_list (PkObjList *list, const PkObjList *data)
{
	guint i;
	gconstpointer obj;

	g_return_if_fail (PK_IS_OBJ_LIST (list));
	g_return_if_fail (PK_IS_OBJ_LIST (data));

	/* add data items to list */
	for (i=0; i < data->len; i++) {
		obj = pk_obj_list_index (data, i);
		pk_obj_list_add (list, obj);
	}
}

/**
 * pk_obj_list_add_array:
 *
 * Makes a deep copy of the data in the array.
 * The data going into the list MUST be the correct type,
 * else bad things will happen.
 **/
void
pk_obj_list_add_array (PkObjList *list, const GPtrArray *data)
{
	guint i;
	gconstpointer obj;

	g_return_if_fail (PK_IS_OBJ_LIST (list));

	/* add data items to list */
	for (i=0; i < data->len; i++) {
		obj = g_ptr_array_index (data, i);
		pk_obj_list_add (list, obj);
	}
}

/**
 * pk_obj_list_add_strv:
 *
 * Makes a deep copy of the data in the array.
 * The data going into the list MUST be the correct type,
 * else bad things will happen.
 **/
void
pk_obj_list_add_strv (PkObjList *list, gpointer **data)
{
	guint i;
	guint len;
	len = g_strv_length ((gchar**)data);
	for (i=0; i<len; i++)
		pk_obj_list_add (list, data[i]);
}

/**
 * pk_obj_list_remove_list:
 *
 * Makes a deep copy of the list
 **/
void
pk_obj_list_remove_list (PkObjList *list, const PkObjList *data)
{
	guint i;
	gconstpointer obj;

	g_return_if_fail (PK_IS_OBJ_LIST (list));
	g_return_if_fail (PK_IS_OBJ_LIST (data));

	/* remove data items from list */
	for (i=0; i < data->len; i++) {
		obj = pk_obj_list_index (data, i);
		pk_obj_list_remove (list, obj);
	}
}

/**
 * pk_obj_list_obj_equal:
 * @list: a valid #PkObjList instance
 * @obj: a valid #gpointer object
 *
 * Return value: the object
 *
 * Removes an item from a list
 **/
static gboolean
pk_obj_list_obj_equal (PkObjList *list, gconstpointer obj1, gconstpointer obj2)
{
	/* use equal */
	if (list->priv->func_equal != NULL)
		return list->priv->func_equal (obj1, obj2);

	/* use compare */
	if (list->priv->func_compare != NULL)
		return list->priv->func_compare (obj1, obj2) == 0;

	/* don't use helper function */
	return obj1 == obj2;
}

/**
 * pk_obj_list_remove_duplicate:
 *
 * Removes duplicate entries
 **/
void
pk_obj_list_remove_duplicate (PkObjList *list)
{
	guint i, j;
	gconstpointer obj1;
	gconstpointer obj2;

	for (i=0; i<list->len; i++) {
		obj1 = pk_obj_list_index (list, i);
		for (j=0; j<list->len; j++) {
			if (i == j)
				break;
			obj2 = pk_obj_list_index (list, j);
			if (pk_obj_list_obj_equal (list, obj1, obj2))
				pk_obj_list_remove_index (list, i);
		}
	}
}

/**
 * pk_obj_list_find_obj:
 * @list: a valid #PkObjList instance
 * @obj: a valid #gconstpointer object
 *
 * Return value: the object
 *
 * Finds an item in a list
 **/
static gpointer
pk_obj_list_find_obj (PkObjList *list, gconstpointer obj)
{
	guint i;
	gconstpointer obj_tmp;

	/* usual case, don't compare in the fast path */
	if (list->priv->func_compare == NULL && list->priv->func_equal == NULL) {
		for (i=0; i < list->len; i++) {
			obj_tmp = pk_obj_list_index (list, i);
			if (obj_tmp == obj)
				return (gpointer) obj_tmp;
		}
		return NULL;
	}

	/* use a comparison function */
	for (i=0; i < list->len; i++) {
		obj_tmp = pk_obj_list_index (list, i);
		if (pk_obj_list_obj_equal (list, obj_tmp, obj))
			return (gpointer) obj_tmp;
	}

	/* nothing found */
	return NULL;
}

/**
 * pk_obj_list_exists:
 * @list: a valid #PkObjList instance
 * @obj: a valid #gconstpointer object
 *
 * Return value: the object
 *
 * Finds an item in a list
 **/
gboolean
pk_obj_list_exists (PkObjList *list, gconstpointer obj)
{
	gconstpointer obj_tmp;
	obj_tmp = pk_obj_list_find_obj (list, obj);
	return (obj_tmp != NULL);
}

/**
 * pk_obj_list_remove:
 * @list: a valid #PkObjList instance
 * @obj: a valid #gpointer object
 *
 * Return value: TRUE is we removed something
 *
 * Removes all the items from a list matching obj
 **/
gboolean
pk_obj_list_remove (PkObjList *list, gconstpointer obj)
{
	gboolean ret;
	gpointer obj_new;
	gboolean found = FALSE;

	g_return_val_if_fail (PK_IS_OBJ_LIST (list), FALSE);
	g_return_val_if_fail (obj != NULL, FALSE);
	g_return_val_if_fail (list->priv->func_free != NULL, FALSE);

	do {
		/* get the object */
		obj_new = pk_obj_list_find_obj (list, obj);
		if (obj_new == NULL)
			break;

		/* try to remove */
		ret = g_ptr_array_remove (list->priv->array, obj_new);

		/* no compare function, and pointer not found */
		if (!ret)
			break;

		found = TRUE;
		list->priv->func_free (obj_new);
		list->len = list->priv->array->len;
	} while (ret);

	return found;
}

/**
 * pk_obj_list_remove_index:
 * @list: a valid #PkObjList instance
 * @idx: the number to remove
 *
 * Return value: TRUE is we removed something
 *
 * Removes an item from a list
 **/
gboolean
pk_obj_list_remove_index (PkObjList *list, guint idx)
{
	gpointer obj;

	g_return_val_if_fail (PK_IS_OBJ_LIST (list), FALSE);
	g_return_val_if_fail (list->priv->func_free != NULL, FALSE);

	/* get the object */
	obj = g_ptr_array_remove_index (list->priv->array, idx);
	if (obj == NULL)
		return FALSE;
	list->priv->func_free (obj);
	list->len = list->priv->array->len;
	return TRUE;
}

/**
 * pk_obj_list_to_file:
 * @list: a valid #PkObjList instance
 * @filename: a filename
 *
 * Saves a copy of the list to a file
 **/
gboolean
pk_obj_list_to_file (PkObjList *list, const gchar *filename)
{
	guint i;
	gconstpointer obj;
	gchar *part;
	GString *string;
	gboolean ret = TRUE;
	GError *error = NULL;
	PkObjListToStringFunc func_to_string;

	g_return_val_if_fail (PK_IS_OBJ_LIST (list), FALSE);
	g_return_val_if_fail (list->priv->func_to_string != NULL, FALSE);

	func_to_string = list->priv->func_to_string;

	/* generate data */
	string = g_string_new ("");
	for (i=0; i<list->len; i++) {
		obj = pk_obj_list_index (list, i);
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
 * pk_obj_list_from_file:
 * @list: a valid #PkObjList instance
 * @filename: a filename
 *
 * Appends the list from a file
 **/
gboolean
pk_obj_list_from_file (PkObjList *list, const gchar *filename)
{
	gboolean ret;
	GError *error = NULL;
	gchar *data = NULL;
	gchar **parts = NULL;
	guint i;
	guint length;
	gpointer obj;
	PkObjListFreeFunc func_free;
	PkObjListFromStringFunc func_from_string;

	g_return_val_if_fail (PK_IS_OBJ_LIST (list), FALSE);
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
			pk_obj_list_add (list, obj);
		func_free (obj);
	}

out:
	g_strfreev (parts);
	g_free (data);

	return ret;
}

/**
 * pk_obj_list_index:
 * @list: a valid #PkObjList instance
 * @idx: the element to return
 *
 * Gets an object from the list
 **/
gconstpointer
pk_obj_list_index (const PkObjList *list, guint idx)
{
	gconstpointer obj;

	g_return_val_if_fail (PK_IS_OBJ_LIST (list), NULL);

	obj = g_ptr_array_index (list->priv->array, idx);
	return obj;
}

/**
 * pk_obj_list_finalize:
 * @object: a valid #PkObjList instance
 **/
static void
pk_obj_list_finalize (GObject *object)
{
	PkObjList *list;
	g_return_if_fail (PK_IS_OBJ_LIST (object));
	list = PK_OBJ_LIST (object);

	pk_obj_list_clear (list);
	g_ptr_array_free (list->priv->array, TRUE);

	G_OBJECT_CLASS (pk_obj_list_parent_class)->finalize (object);
}

/**
 * pk_obj_list_class_init:
 * @klass: a valid #PkObjListClass instance
 **/
static void
pk_obj_list_class_init (PkObjListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_obj_list_finalize;
	g_type_class_add_private (klass, sizeof (PkObjListPrivate));
}

/**
 * pk_obj_list_init:
 * @list: a valid #PkObjList instance
 *
 * Initializes the obj_list class.
 **/
static void
pk_obj_list_init (PkObjList *list)
{
	list->priv = PK_OBJ_LIST_GET_PRIVATE (list);
	list->priv->func_new = NULL;
	list->priv->func_copy = NULL;
	list->priv->func_free = NULL;
	list->priv->func_compare = NULL;
	list->priv->func_to_string = NULL;
	list->priv->func_from_string = NULL;
	list->priv->array = g_ptr_array_new ();
	list->len = list->priv->array->len;
}

/**
 * pk_obj_list_new:
 *
 * Return value: A new list class instance.
 **/
PkObjList *
pk_obj_list_new (void)
{
	PkObjList *list;
	list = g_object_new (PK_TYPE_OBJ_LIST, NULL);
	return PK_OBJ_LIST (list);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"
#include <packagekit-glib/pk-category-obj.h>

void
pk_obj_list_test (EggTest *test)
{
	PkObjList *list;
	PkCategoryObj *obj;

	if (!egg_test_start (test, "PkObjList"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	list = pk_obj_list_new ();
	egg_test_assert (test, list != NULL);

	/* test object */
	obj = pk_category_obj_new_from_data ("dave", "dave-moo", "name", "This is a name", "help");

	egg_test_title (test, "correct size");
	egg_test_assert (test, list->len == 0);

	/* setup list type */
	pk_obj_list_set_free (list, (PkObjListFreeFunc) pk_category_obj_free);
	pk_obj_list_set_copy (list, (PkObjListCopyFunc) pk_category_obj_copy);

	/* add to list */
	pk_obj_list_add (list, obj);

	egg_test_title (test, "correct size");
	egg_test_assert (test, list->len == 1);

	/* add to list */
	pk_obj_list_add (list, obj);

	egg_test_title (test, "correct size");
	egg_test_assert (test, list->len == 2);

	pk_obj_list_clear (list);

	egg_test_title (test, "correct size");
	egg_test_assert (test, list->len == 0);

	/* add to list */
	pk_obj_list_add (list, obj);

	g_object_unref (list);

	pk_category_obj_free (obj);
	egg_test_end (test);
}
#endif
