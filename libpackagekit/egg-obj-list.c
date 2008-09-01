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

#include "egg-obj-list.h"

#define EGG_OBJ_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), EGG_TYPE_OBJ_LIST, EggObjListPrivate))

struct EggObjListPrivate
{
	EggObjListNewFunc	 func_new;
	EggObjListCopyFunc	 func_copy;
	EggObjListFreeFunc	 func_free;
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
 * egg_obj_list_index:
 * @list: a valid #EggObjList instance
 * @index: the element to return
 *
 * Gets an object from the list
 **/
const gpointer
egg_obj_list_index (EggObjList *list, guint index)
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
#ifdef EGG_BUILD_TESTS
#include <libselftest.h>

void
libst_obj_list (LibSelfTest *test)
{
	EggObjList *list;
	gchar *text;
	gint value;

	if (libst_start (test, "EggObjList", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get an instance");
	list = egg_obj_list_new ();
	if (list != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	g_object_unref (list);

	libst_end (test);
}
#endif

