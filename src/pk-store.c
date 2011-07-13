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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "pk-store.h"

#define PK_STORE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_STORE, PkStorePrivate))

struct PkStorePrivate
{
	GHashTable		*hash_string;
	GHashTable		*hash_strv;
	GHashTable		*hash_pointer;
	GHashTable		*hash_array;
};

G_DEFINE_TYPE (PkStore, pk_store, G_TYPE_OBJECT)

/**
 * pk_store_set_string:
 **/
gboolean
pk_store_set_string (PkStore *store, const gchar *key, const gchar *data)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_STORE (store), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* valid, but do nothing */
	if (data == NULL)
		return FALSE;

	/* does already exist? */
	ret = g_hash_table_remove (store->priv->hash_string, key);
	if (ret)
		g_debug ("already set data for %s, so removing", key);

	g_debug ("saving '%s' for %s", data, key);
	g_hash_table_insert (store->priv->hash_string, g_strdup (key), (gpointer) g_strdup (data));
	return TRUE;
}

/**
 * pk_store_set_strv:
 **/
gboolean
pk_store_set_strv (PkStore *store, const gchar *key, gchar **data)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_STORE (store), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* valid, but do nothing */
	if (data == NULL)
		return FALSE;

	/* does already exist? */
	ret = g_hash_table_remove (store->priv->hash_strv, key);
	if (ret)
		g_debug ("already set data for %s, so removing", key);

	g_debug ("saving %p for %s", data, key);
	g_hash_table_insert (store->priv->hash_strv, g_strdup (key), (gpointer) g_strdupv (data));
	return TRUE;
}

/**
 * pk_store_set_array:
 **/
gboolean
pk_store_set_array (PkStore *store, const gchar *key, GPtrArray *data)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_STORE (store), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* valid, but do nothing */
	if (data == NULL)
		return FALSE;

	/* does already exist? */
	ret = g_hash_table_remove (store->priv->hash_array, key);
	if (ret)
		g_debug ("already set data for %s, so removing", key);

	g_debug ("saving %p for %s", data, key);
	g_hash_table_insert (store->priv->hash_array, g_strdup (key), (gpointer) data);
	return TRUE;
}

/**
 * pk_store_set_uint:
 **/
gboolean
pk_store_set_uint (PkStore *store, const gchar *key, guint data)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_STORE (store), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* does already exist? */
	ret = g_hash_table_remove (store->priv->hash_pointer, key);
	if (ret)
		g_debug ("already set data for %s, so removing", key);

	g_debug ("saving %i for %s", data, key);
	g_hash_table_insert (store->priv->hash_pointer, g_strdup (key), GINT_TO_POINTER (data+1));
	return TRUE;
}

/**
 * pk_store_set_bool:
 **/
gboolean
pk_store_set_bool (PkStore *store, const gchar *key, gboolean data)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_STORE (store), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* does already exist? */
	ret = g_hash_table_remove (store->priv->hash_pointer, key);
	if (ret)
		g_debug ("already set data for %s, so removing", key);

	g_debug ("saving %i for %s", data, key);
	g_hash_table_insert (store->priv->hash_pointer, g_strdup (key), GINT_TO_POINTER (data+1));
	return TRUE;
}

/**
 * pk_store_set_pointer:
 **/
gboolean
pk_store_set_pointer (PkStore *store, const gchar *key, gpointer data)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_STORE (store), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	/* does already exist? */
	ret = g_hash_table_remove (store->priv->hash_pointer, key);
	if (ret)
		g_debug ("already set data for %s, so removing", key);

	g_debug ("saving %p for %s", data, key);
	g_hash_table_insert (store->priv->hash_pointer, g_strdup (key), GINT_TO_POINTER(GPOINTER_TO_INT(data)+1));
	return TRUE;
}

/**
 * pk_store_get_string:
 **/
const gchar *
pk_store_get_string (const PkStore *store, const gchar *key)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_STORE (store), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* does already exist? */
	value = g_hash_table_lookup (store->priv->hash_string, (gpointer) key);
	if (value == NULL) {
		g_warning ("not set data for %s", key);
		return NULL;
	}
	return (const gchar *) value;
}

/**
 * pk_store_get_strv:
 **/
gchar **
pk_store_get_strv (const PkStore *store, const gchar *key)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_STORE (store), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* does already exist? */
	value = g_hash_table_lookup (store->priv->hash_strv, (gpointer) key);
	if (value == NULL) {
		g_warning ("not set data for %s", key);
		return NULL;
	}
	return (gchar **) value;
}

/**
 * pk_store_get_array:
 **/
const GPtrArray *
pk_store_get_array (const PkStore *store, const gchar *key)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_STORE (store), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* does already exist? */
	value = g_hash_table_lookup (store->priv->hash_array, (gpointer) key);
	if (value == NULL) {
		g_warning ("not set data for %s", key);
		return NULL;
	}
	return (GPtrArray *) value;
}

/**
 * pk_store_get_uint:
 **/
guint
pk_store_get_uint (const PkStore *store, const gchar *key)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_STORE (store), 0);
	g_return_val_if_fail (key != NULL, 0);

	/* does already exist? */
	value = g_hash_table_lookup (store->priv->hash_pointer, (gpointer) key);
	if (value == NULL) {
		g_warning ("not set data for %s", key);
		return 0;
	}
	/* we do the +1/-1 as NULL also means missing in the hash table */
	return GPOINTER_TO_INT (value)-1;
}

/**
 * pk_store_get_bool:
 **/
gboolean
pk_store_get_bool (const PkStore *store, const gchar *key)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_STORE (store), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* does already exist? */
	value = g_hash_table_lookup (store->priv->hash_pointer, (gpointer) key);
	if (value == NULL) {
		g_warning ("not set data for %s", key);
		return FALSE;
	}
	return GPOINTER_TO_INT (value)-1;
}

/**
 * pk_store_get_pointer:
 **/
gpointer
pk_store_get_pointer (const PkStore *store, const gchar *key)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_STORE (store), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* does already exist? */
	value = g_hash_table_lookup (store->priv->hash_pointer, (gpointer) key);
	if (value == NULL) {
		g_warning ("not set data for %s", key);
		return NULL;
	}
	return GINT_TO_POINTER(GPOINTER_TO_INT(value)-1);
}

/**
 * pk_store_reset:
 **/
gboolean
pk_store_reset (PkStore *store)
{
	g_return_val_if_fail (PK_IS_STORE (store), FALSE);

	g_hash_table_remove_all (store->priv->hash_pointer);
	g_hash_table_remove_all (store->priv->hash_string);
	g_hash_table_remove_all (store->priv->hash_strv);
	g_hash_table_remove_all (store->priv->hash_array);
	return TRUE;
}

/**
 * pk_free_ptr_array:
 **/
static void
pk_free_ptr_array (gpointer data)
{
	g_ptr_array_free ((GPtrArray *) data, TRUE);
}

/**
 * pk_store_finalize:
 **/
static void
pk_store_finalize (GObject *object)
{
	PkStore *store;
	g_return_if_fail (PK_IS_STORE (object));
	store = PK_STORE (object);

	g_hash_table_unref (store->priv->hash_string);
	g_hash_table_unref (store->priv->hash_strv);
	g_hash_table_unref (store->priv->hash_pointer);
	g_hash_table_unref (store->priv->hash_array);

	G_OBJECT_CLASS (pk_store_parent_class)->finalize (object);
}

/**
 * pk_store_class_init:
 **/
static void
pk_store_class_init (PkStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_store_finalize;
	g_type_class_add_private (klass, sizeof (PkStorePrivate));
}

/**
 * pk_store_init:
 **/
static void
pk_store_init (PkStore *store)
{
	store->priv = PK_STORE_GET_PRIVATE (store);
	store->priv->hash_string = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	store->priv->hash_strv = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_strfreev);
	store->priv->hash_pointer = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	store->priv->hash_array = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, pk_free_ptr_array);
}

/**
 * pk_store_new:
 * Return value: A new store class store.
 **/
PkStore *
pk_store_new (void)
{
	PkStore *store;
	store = g_object_new (PK_TYPE_STORE, NULL);
	return PK_STORE (store);
}

