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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include <glib/gi18n.h>
#include <glib.h>

#include "pk-cache.h"
#include "pk-conf.h"

#define PK_CACHE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CACHE, PkCachePrivate))

struct PkCachePrivate
{
	PkResults		*get_updates;
};

G_DEFINE_TYPE (PkCache, pk_cache, G_TYPE_OBJECT)
static gpointer pk_cache_object = NULL;

/**
 * pk_cache_get_results:
 **/
PkResults *
pk_cache_get_results (PkCache *cache, PkRoleEnum role)
{
	g_return_val_if_fail (PK_IS_CACHE (cache), NULL);

	/* only support GetUpdates */
	if (role != PK_ROLE_ENUM_GET_UPDATES) {
		g_debug ("only caching update lists");
		return FALSE;
	}

	return cache->priv->get_updates;
}

/**
 * pk_cache_set_results:
 *
 * If you add to this method to support other calls than GetUpdates then be sure
 * to add the required signals to pk_transaction_try_emit_cache().
 **/
gboolean
pk_cache_set_results (PkCache *cache, PkRoleEnum role, PkResults *results)
{
	g_return_val_if_fail (PK_IS_CACHE (cache), FALSE);
	g_return_val_if_fail (results != NULL, FALSE);

	/* only support GetUpdates */
	if (role != PK_ROLE_ENUM_GET_UPDATES) {
		g_debug ("only caching update lists");
		return FALSE;
	}

	/* do this in case we have old data */
	pk_cache_invalidate (cache);

	g_debug ("reffing updates cache");
	cache->priv->get_updates = g_object_ref (results);
	return TRUE;
}

/**
 * pk_cache_invalidate:
 **/
gboolean
pk_cache_invalidate (PkCache *cache)
{
	g_return_val_if_fail (PK_IS_CACHE (cache), FALSE);

	g_debug ("unreffing updates cache");
	if (cache->priv->get_updates != NULL) {
		g_object_unref (cache->priv->get_updates);
		cache->priv->get_updates = NULL;
	}
	return TRUE;
}

/**
 * pk_cache_finalize:
 **/
static void
pk_cache_finalize (GObject *object)
{
	PkCache *cache;
	g_return_if_fail (PK_IS_CACHE (object));
	cache = PK_CACHE (object);

	if (cache->priv->get_updates != NULL)
		g_object_unref (cache->priv->get_updates);

	G_OBJECT_CLASS (pk_cache_parent_class)->finalize (object);
}

/**
 * pk_cache_class_init:
 **/
static void
pk_cache_class_init (PkCacheClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_cache_finalize;
	g_type_class_add_private (klass, sizeof (PkCachePrivate));
}

/**
 * pk_cache_init:
 *
 * initializes the cache class. NOTE: We expect cache objects
 * to *NOT* be removed or added during the session.
 * We only control the first cache object if there are more than one.
 **/
static void
pk_cache_init (PkCache *cache)
{
	cache->priv = PK_CACHE_GET_PRIVATE (cache);
	cache->priv->get_updates = NULL;
}

/**
 * pk_cache_new:
 * Return value: A new cache class instance.
 **/
PkCache *
pk_cache_new (void)
{
	if (pk_cache_object != NULL) {
		g_object_ref (pk_cache_object);
	} else {
		pk_cache_object = g_object_new (PK_TYPE_CACHE, NULL);
		g_object_add_weak_pointer (pk_cache_object, &pk_cache_object);
	}
	return PK_CACHE (pk_cache_object);
}

