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

#include "pk-debug.h"
#include "pk-common.h"
#include "pk-update-detail.h"
#include "pk-update-detail-cache.h"

#define PK_UPDATE_DETAIL_CACHE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_UPDATE_DETAIL_CACHE, PkUpdateDetailCachePrivate))

struct PkUpdateDetailCachePrivate
{
	GPtrArray		*array;
};

G_DEFINE_TYPE (PkUpdateDetailCache, pk_update_detail_cache, G_TYPE_OBJECT)
static gpointer pk_update_detail_cache_object = NULL;

/**
 * pk_update_detail_cache_invalidate:
 **/
gboolean
pk_update_detail_cache_invalidate (PkUpdateDetailCache *cache)
{
	g_return_val_if_fail (PK_IS_UPDATE_DETAIL_CACHE (cache), FALSE);
	return TRUE;
}

/**
 * pk_update_detail_cache_add_item:
 **/
gboolean
pk_update_detail_cache_add_item (PkUpdateDetailCache *cache, PkUpdateDetail *detail)
{
	g_return_val_if_fail (PK_IS_UPDATE_DETAIL_CACHE (cache), FALSE);
	g_return_val_if_fail (detail != NULL, FALSE);

	g_ptr_array_add (cache->priv->array, detail);
	return TRUE;
}

/**
 * pk_update_detail_cache_get_item:
 **/
PkUpdateDetail *
pk_update_detail_cache_get_item (PkUpdateDetailCache *cache, const gchar *package_id)
{
	guint i;
	guint len;
	PkUpdateDetail *detail;

	g_return_val_if_fail (PK_IS_UPDATE_DETAIL_CACHE (cache), NULL);
	g_return_val_if_fail (package_id != NULL, NULL);

	len = cache->priv->array->len;
	for (i=0; i<len; i++) {
		detail = (PkUpdateDetail *) g_ptr_array_index (cache->priv->array, i);
		if (pk_strequal (package_id, detail->package_id)) {
			return detail;
		}
	}
	/* bahh, found nothing */
	return NULL;
}

/**
 * pk_update_detail_cache_finalize:
 **/
static void
pk_update_detail_cache_finalize (GObject *object)
{
	guint i;
	guint len;
	PkUpdateDetail *detail;
	PkUpdateDetailCache *cache;
	g_return_if_fail (PK_IS_UPDATE_DETAIL_CACHE (object));
	cache = PK_UPDATE_DETAIL_CACHE (object);

	//TODO: FREE!
	len = cache->priv->array->len;
	for (i=0; i<len; i++) {
		detail = (PkUpdateDetail *) g_ptr_array_index (cache->priv->array, i);
		pk_update_detail_free (detail);
	}
	g_ptr_array_free (cache->priv->array, FALSE);

	G_OBJECT_CLASS (pk_update_detail_cache_parent_class)->finalize (object);
}

/**
 * pk_update_detail_cache_class_init:
 **/
static void
pk_update_detail_cache_class_init (PkUpdateDetailCacheClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_update_detail_cache_finalize;
	g_type_class_add_private (klass, sizeof (PkUpdateDetailCachePrivate));
}

/**
 * pk_update_detail_cache_init:
 *
 * initializes the update_detail_cache class. NOTE: We expect cache objects
 * to *NOT* be removed or added during the session.
 * We only control the first cache object if there are more than one.
 **/
static void
pk_update_detail_cache_init (PkUpdateDetailCache *cache)
{
	cache->priv = PK_UPDATE_DETAIL_CACHE_GET_PRIVATE (cache);
	cache->priv->array = g_ptr_array_new ();
}

/**
 * pk_update_detail_cache_new:
 * Return value: A new cache class instance.
 **/
PkUpdateDetailCache *
pk_update_detail_cache_new (void)
{
	if (pk_update_detail_cache_object != NULL) {
		g_object_ref (pk_update_detail_cache_object);
	} else {
		pk_update_detail_cache_object = g_object_new (PK_TYPE_UPDATE_DETAIL_CACHE, NULL);
		g_object_add_weak_pointer (pk_update_detail_cache_object, &pk_update_detail_cache_object);
	}
	return PK_UPDATE_DETAIL_CACHE (pk_update_detail_cache_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_update_detail_cache (LibSelfTest *test)
{
	PkUpdateDetailCache *cache;
	gchar *text;
	gint value;

	if (libst_start (test, "PkUpdateDetailCache", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get an instance");
	cache = pk_update_detail_cache_new ();
	if (cache != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get the default backend");
	text = pk_update_detail_cache_get_string (cache, "DefaultBackend");
	if (text != PK_UPDATE_DETAIL_CACHE_VALUE_STRING_MISSING) {
		libst_success (test, "got default backend '%s'", text);
	} else {
		libst_failed (test, "got NULL!");
	}
	g_free (text);

	g_object_unref (cache);

	libst_end (test);
}
#endif

