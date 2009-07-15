/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#include <glib-object.h>

#include "pk-main.h"
#include "pk-store.h"

#define PK_STORE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_STORE, PkStorePrivate))

struct PkStorePrivate
{
	GHashTable		*data;
};

G_DEFINE_TYPE (PkStore, pk_store, G_TYPE_OBJECT)

/**
 * pk_store_lookup_plugin:
 **/
PkPlugin *
pk_store_lookup_plugin (PkStore	*store, NPP instance)
{
	PkPlugin *plugin;

	g_return_val_if_fail (PK_IS_STORE (store), NULL);

	/* find plugin for this instance */
	plugin = g_hash_table_lookup (store->priv->data, instance);

	return plugin;
}

/**
 * pk_store_add_plugin:
 **/
gboolean
pk_store_add_plugin (PkStore *store, NPP instance, PkPlugin *plugin)
{
	PkPlugin *plugin_tmp;
	gboolean ret = TRUE;

	/* check it's not already here */
	plugin_tmp = pk_store_lookup_plugin (store, instance);
	if (plugin_tmp != NULL) {
		pk_warning ("already added plugin <%p> for instance [%p]", plugin_tmp, instance);
		ret = FALSE;
		goto out;
	}

	/* it's no, so add it */
	pk_debug ("adding plugin <%p> for instance [%p]", plugin, instance);

	g_hash_table_insert (store->priv->data, instance, g_object_ref (plugin));
out:
	return ret;
}

/**
 * pk_store_remove_plugin:
 **/
gboolean
pk_store_remove_plugin (PkStore *store, NPP instance)
{
	gboolean ret;

	pk_debug ("removing plugin for instance [%p]", instance);

	/* remove from hash (also unrefs) */
	ret = g_hash_table_remove (store->priv->data, instance);
	if (!ret) {
		pk_warning ("nothing to remove for instance [%p]", instance);
		goto out;
	}
out:
	return ret;
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

	g_hash_table_unref (store->priv->data);

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
	store->priv->data = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
}

/**
 * pk_store_new:
 * Return value: A new store_install class instance.
 **/
PkStore *
pk_store_new (void)
{
	PkStore *store;
	store = g_object_new (PK_TYPE_STORE, NULL);
	return PK_STORE (store);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_store_install (EggTest *test)
{
	PkStore *store;

	if (!egg_test_start (test, "PkStore"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	store = pk_store_new ();
	if (store != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	g_object_unref (store);

	egg_test_end (test);
}
#endif

