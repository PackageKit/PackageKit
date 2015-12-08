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

#include "egg-debug.h"
#include "pk-cache.h"
#include "pk-conf.h"

#define PK_CACHE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CACHE, PkCachePrivate))

struct PkCachePrivate
{
	PkConf			*conf;
	gboolean		 use_update_cache;
	GPtrArray		*updates_cache;
};

G_DEFINE_TYPE (PkCache, pk_cache, G_TYPE_OBJECT)
static gpointer pk_cache_object = NULL;

/**
 * pk_cache_get_updates:
 **/
GPtrArray *
pk_cache_get_updates (PkCache *cache)
{
	g_return_val_if_fail (PK_IS_CACHE (cache), NULL);

	/* do not use */
	if (!cache->priv->use_update_cache) {
		egg_debug ("not using cache");
		return NULL;
	}

	return cache->priv->updates_cache;
}

/**
 * pk_cache_set_updates:
 **/
gboolean
pk_cache_set_updates (PkCache *cache, GPtrArray *list)
{
	g_return_val_if_fail (PK_IS_CACHE (cache), FALSE);
	g_return_val_if_fail (list != NULL, FALSE);

	/* do not use */
	if (!cache->priv->use_update_cache) {
		egg_debug ("not using cache");
		return FALSE;
	}

	/* do this in case we have old data */
	pk_cache_invalidate (cache);

	egg_debug ("reffing updates cache");
	cache->priv->updates_cache = g_ptr_array_ref (list);
	return TRUE;
}

/**
 * pk_cache_invalidate:
 **/
gboolean
pk_cache_invalidate (PkCache *cache)
{
	g_return_val_if_fail (PK_IS_CACHE (cache), FALSE);

	egg_debug ("unreffing updates cache");
	if (cache->priv->updates_cache != NULL) {
		g_ptr_array_unref (cache->priv->updates_cache);
		cache->priv->updates_cache = NULL;
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

	if (cache->priv->updates_cache != NULL)
		g_ptr_array_unref (cache->priv->updates_cache);

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
	cache->priv->updates_cache = NULL;
	cache->priv->conf = pk_conf_new ();
	cache->priv->use_update_cache = pk_conf_get_bool (cache->priv->conf, "UseUpdateCacheBROKEN");
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

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_cache (EggTest *test)
{
	PkCache *cache;

	if (!egg_test_start (test, "PkCache"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	cache = pk_cache_new ();
	if (cache != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	g_object_unref (cache);

	egg_test_end (test);
}
#endif

