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

#ifdef CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include <glib/gi18n.h>
#include <glib.h>

#include "egg-debug.h"
#include "pk-common.h"
#include "pk-update-detail-obj.h"
#include "pk-update-detail-list.h"

#define PK_UPDATE_DETAIL_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_UPDATE_DETAIL_LIST, PkUpdateDetailListPrivate))

struct PkUpdateDetailListPrivate
{
	GPtrArray		*array;
};

G_DEFINE_TYPE (PkUpdateDetailList, pk_update_detail_list, G_TYPE_OBJECT)
static gpointer pk_update_detail_list_object = NULL;

/**
 * pk_update_detail_list_clear:
 * @list: a valid #PkUpdateDetailList instance
 *
 * Clears the package list
 **/
gboolean
pk_update_detail_list_clear (PkUpdateDetailList *list)
{
	g_return_val_if_fail (PK_IS_UPDATE_DETAIL_LIST (list), FALSE);
	return TRUE;
}

/**
 * pk_update_detail_list_add_obj:
 * @list: a valid #PkUpdateDetailList instance
 * @obj: a valid #PkUpdateDetailObj object
 *
 * Adds a copy of the object to the list
 **/
gboolean
pk_update_detail_list_add_obj (PkUpdateDetailList *list, const PkUpdateDetailObj *obj)
{
	PkUpdateDetailObj *obj_new;
	const PkUpdateDetailObj *obj_found;
	g_return_val_if_fail (PK_IS_UPDATE_DETAIL_LIST (list), FALSE);
	g_return_val_if_fail (obj != NULL, FALSE);

	/* are we already in the cache? */
	obj_found = pk_update_detail_list_get_obj (list, obj->id);
	if (obj_found != NULL) {
		egg_debug ("already in list: %s", obj->id->name);
		return FALSE;
	}

	obj_new = pk_update_detail_obj_copy (obj);
	g_ptr_array_add (list->priv->array, obj_new);
	return TRUE;
}

/**
 * pk_update_detail_list_get_obj:
 * @list: a valid #PkUpdateDetailList instance
 * @id: A #PkPackageId of the item to match
 *
 * Gets an object from the list
 **/
const PkUpdateDetailObj *
pk_update_detail_list_get_obj (PkUpdateDetailList *list, const PkPackageId *id)
{
	guint i;
	guint len;
	PkUpdateDetailObj *obj;

	g_return_val_if_fail (PK_IS_UPDATE_DETAIL_LIST (list), NULL);
	g_return_val_if_fail (id != NULL, NULL);

	len = list->priv->array->len;
	for (i=0; i<len; i++) {
		obj = (PkUpdateDetailObj *) g_ptr_array_index (list->priv->array, i);
		if (pk_package_id_equal (id, obj->id)) {
			return obj;
		}
	}
	/* bahh, found nothing */
	return NULL;
}

/**
 * pk_update_detail_list_finalize:
 * @object: a valid #PkUpdateDetailList instance
 **/
static void
pk_update_detail_list_finalize (GObject *object)
{
	guint i;
	guint len;
	PkUpdateDetailObj *obj;
	PkUpdateDetailList *list;
	g_return_if_fail (PK_IS_UPDATE_DETAIL_LIST (object));
	list = PK_UPDATE_DETAIL_LIST (object);

	/* free the list */
	len = list->priv->array->len;
	for (i=0; i<len; i++) {
		obj = (PkUpdateDetailObj *) g_ptr_array_index (list->priv->array, i);
		pk_update_detail_obj_free (obj);
	}
	g_ptr_array_free (list->priv->array, FALSE);

	G_OBJECT_CLASS (pk_update_detail_list_parent_class)->finalize (object);
}

/**
 * pk_update_detail_list_class_init:
 * @klass: a valid #PkUpdateDetailListClass instance
 **/
static void
pk_update_detail_list_class_init (PkUpdateDetailListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_update_detail_list_finalize;
	g_type_class_add_private (klass, sizeof (PkUpdateDetailListPrivate));
}

/**
 * pk_update_detail_list_init:
 * @list: a valid #PkUpdateDetailList instance
 *
 * Initializes the update_detail_list class.
 **/
static void
pk_update_detail_list_init (PkUpdateDetailList *list)
{
	list->priv = PK_UPDATE_DETAIL_LIST_GET_PRIVATE (list);
	list->priv->array = g_ptr_array_new ();
}

/**
 * pk_update_detail_list_new:
 *
 * Return value: A new list class instance.
 **/
PkUpdateDetailList *
pk_update_detail_list_new (void)
{
	if (pk_update_detail_list_object != NULL) {
		g_object_ref (pk_update_detail_list_object);
	} else {
		pk_update_detail_list_object = g_object_new (PK_TYPE_UPDATE_DETAIL_LIST, NULL);
		g_object_add_weak_pointer (pk_update_detail_list_object, &pk_update_detail_list_object);
	}
	return PK_UPDATE_DETAIL_LIST (pk_update_detail_list_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_update_detail_list (LibSelfTest *test)
{
	PkUpdateDetailList *list;
	gchar *text;
	gint value;

	if (!libst_start (test, "PkUpdateDetailList"))
		return;

	/************************************************************/
	libst_title (test, "get an instance");
	list = pk_update_detail_list_new ();
	if (list != NULL)
		libst_success (test, NULL);
	else
		libst_failed (test, NULL);

	/************************************************************/
	libst_title (test, "get the default backend");
	text = pk_update_detail_list_get_string (list, "DefaultBackend");
	if (text != PK_UPDATE_DETAIL_LIST_VALUE_STRING_MISSING) {
		libst_success (test, "got default backend '%s'", text);
	} else {
		libst_failed (test, "got NULL!");
	}
	g_free (text);

	g_object_unref (list);

	libst_end (test);
}
#endif

