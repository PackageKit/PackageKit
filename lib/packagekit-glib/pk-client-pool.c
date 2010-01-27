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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:pk-client-pool
 * @short_description: A pool of clients that can be treated as one abstract client
 *
 * These provide a way to do many async methods without keeping track of each one --
 * all the destruction is handled transparently.
 */

#include "config.h"

#include <glib/gi18n.h>

#include "egg-debug.h"

#include <packagekit-glib/pk-client.h>
#include <packagekit-glib/pk-common.h>
#include <packagekit-glib/pk-client-pool.h>

static void     pk_client_pool_finalize	(GObject            *object);

#define PK_CLIENT_POOL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CLIENT_POOL, PkClientPoolPrivate))

typedef struct {
	gchar		*signal_name;
	GCallback	 c_handler;
	GObject		*object;
} PkClientPoolObj;

/**
 * PkClientPoolPrivate:
 *
 * Private #PkClientPool data
 **/
struct _PkClientPoolPrivate
{
	GPtrArray		*client_array;
	GPtrArray		*obj_array;
};

G_DEFINE_TYPE (PkClientPool, pk_client_pool, PK_TYPE_OBJ_LIST)

/**
 * pk_client_pool_get_size:
 * @pool: the %PkClientPool object
 *
 * This returns the number of clients held in the pool
 *
 * Return value: the size of the pool
 **/
guint
pk_client_pool_get_size	(PkClientPool *pool)
{
	g_return_val_if_fail (PK_IS_CLIENT_POOL (pool), 0);
	return pool->priv->client_array->len;
}

/**
 * pk_client_pool_remove:
 * @pool: the %PkClientPool object
 * @client: the %PkClient object to remove
 *
 * Removes a %PkClient instance that is not going to be run, or failed to be run.
 *
 * Return value: %TRUE if the client was removed.
 **/
gboolean
pk_client_pool_remove (PkClientPool *pool, PkClient *client)
{
	PkClientPoolObj *obj;
	gboolean ret;
	guint i;

	g_return_val_if_fail (PK_IS_CLIENT_POOL (pool), FALSE);
	g_return_val_if_fail (client != NULL, FALSE);

	egg_debug ("client %p removed from pool", client);
	ret = g_ptr_array_remove (pool->priv->client_array, client);
	if (!ret) {
		egg_warning ("failed to remove %p", client);
		goto out;
	}

	/* disconnect all objects */
	for (i=0; i<pool->priv->obj_array->len; i++) {
		obj = g_ptr_array_index (pool->priv->obj_array, i);
		g_signal_handlers_disconnect_by_func (client, obj->c_handler, obj->object);
	}

	/* unref object */
	g_object_unref (client);
out:
	return ret;
}

/**
 * pk_client_pool_destroy_cb:
 **/
static void
pk_client_pool_destroy_cb (PkClient *client, PkClientPool *pool)
{
	pk_client_pool_remove (pool, client);
}

/**
 * pk_client_pool_create:
 * @pool: the %PkClientPool object
 *
 * This creates a %PkClient instance and puts it in the pool. It also connects
 * up and previously connected methods.
 *
 * Return value: a %PkClient instance, or %NULL for an error. You must free this using g_object_unref() when done.
 **/
PkClient *
pk_client_pool_create (PkClientPool *pool)
{
	PkClient *client;
	PkClientPoolObj *obj;
	guint i;

	g_return_val_if_fail (PK_IS_CLIENT_POOL (pool), NULL);

	client = pk_client_new ();

	/* we unref the client on destroy */
	g_signal_connect (client, "destroy",
			  G_CALLBACK (pk_client_pool_destroy_cb), pool);

	/* connect up all signals already added */
	for (i=0; i<pool->priv->obj_array->len; i++) {
		obj = g_ptr_array_index (pool->priv->obj_array, i);
		egg_debug ("connecting up %s to client", obj->signal_name);
		g_signal_connect (client, obj->signal_name, obj->c_handler, obj->object);
	}

	/* add to array */
	g_ptr_array_add (pool->priv->client_array, g_object_ref (client));
	egg_debug ("added %p to pool", client);

	return client;
}

/**
 * pk_client_pool_find_obj:
 **/
static PkClientPoolObj *
pk_client_pool_find_obj (PkClientPool *pool, const gchar *signal_name)
{
	PkClientPoolObj *obj;
	guint i;

	/* disconnect on all objects */
	for (i=0; i<pool->priv->obj_array->len; i++) {
		obj = g_ptr_array_index (pool->priv->obj_array, i);
		if (g_strcmp0 (obj->signal_name, signal_name) == 0)
			goto out;
	}

	/* not found */
	obj = NULL;
out:
	return obj;
}

/**
 * pk_client_pool_free_obj:
 **/
static void
pk_client_pool_free_obj (PkClientPoolObj *obj)
{
	/* only unref the object if it is valid */
	if (obj->object != NULL)
		g_object_unref (obj->object);
	g_free (obj->signal_name);
	g_free (obj);
}

/**
 * pk_client_pool_disconnect:
 * @pool: the %PkClientPool object
 * @signal_name: the signal name, e.g. "finished"
 *
 * This disconnects up a signal from all the clients already in the pool.
 *
 * Return value: %TRUE if the signal was found and removed
 **/
gboolean
pk_client_pool_disconnect (PkClientPool *pool, const gchar *signal_name)
{
	gboolean ret = TRUE;
	PkClient *client;
	PkClientPoolObj *obj;
	guint i;

	g_return_val_if_fail (PK_IS_CLIENT_POOL (pool), FALSE);
	g_return_val_if_fail (signal_name != NULL, FALSE);

	/* find signal name */
	obj = pk_client_pool_find_obj (pool, signal_name);
	if (obj == NULL) {
		egg_warning ("failed to find signal name %s", signal_name);
		ret = FALSE;
		goto out;
	}

	egg_debug ("disconnected %s", signal_name);

	/* disconnect on all objects */
	for (i=0; i<pool->priv->client_array->len; i++) {
		client = g_ptr_array_index (pool->priv->client_array, i);
		g_signal_handlers_disconnect_by_func (client, obj->c_handler, obj->object);
	}

	/* remove obj so we don't apply it on new clients */
	g_ptr_array_remove (pool->priv->client_array, obj);
	pk_client_pool_free_obj (obj);

out:
	return ret;
}

/**
 * pk_client_pool_connect:
 * @pool: the %PkClientPool object
 * @signal_name: the signal name, e.g. "finished"
 * @c_handler: the %GCallback for the signal
 * @object: the object to pass to the handler
 *
 * This connects up a signal to all the clients already in the pool.
 *
 * Return value: %TRUE if the signal was setup
 **/
gboolean
pk_client_pool_connect (PkClientPool *pool, const gchar *signal_name, GCallback c_handler, GObject *object)
{
	PkClient *client;
	PkClientPoolObj *obj;
	guint i;
	gboolean ret = TRUE;

	g_return_val_if_fail (PK_IS_CLIENT_POOL (pool), FALSE);
	g_return_val_if_fail (signal_name != NULL, FALSE);

	/* check if signal has already been added */
	obj = pk_client_pool_find_obj (pool, signal_name);
	if (obj != NULL) {
		egg_warning ("already added signal %s", signal_name);
		ret = FALSE;
		goto out;
	}

	egg_debug ("connected %s", signal_name);

	/* add to existing clients */
	for (i=0; i<pool->priv->client_array->len; i++) {
		client = g_ptr_array_index (pool->priv->client_array, i);
		g_signal_connect (client, signal_name, c_handler, object);
	}

	/* save so we can add to future clients */
	obj = g_new0 (PkClientPoolObj, 1);
	obj->signal_name = g_strdup (signal_name);
	obj->c_handler = c_handler;
	obj->object = NULL;
	/* only ref the object if it is valid */
	if (object != NULL)
		obj->object = g_object_ref (object);
	g_ptr_array_add (pool->priv->obj_array, obj);
out:
	return ret;
}

/**
 * pk_client_pool_class_init:
 * @klass: The PkClientPoolClass
 **/
static void
pk_client_pool_class_init (PkClientPoolClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_client_pool_finalize;
	g_type_class_add_private (klass, sizeof (PkClientPoolPrivate));
}

/**
 * pk_client_pool_init:
 **/
static void
pk_client_pool_init (PkClientPool *pool)
{
	g_return_if_fail (pool != NULL);
	g_return_if_fail (PK_IS_CLIENT_POOL (pool));

	pool->priv = PK_CLIENT_POOL_GET_PRIVATE (pool);
	pool->priv->client_array = g_ptr_array_new ();
	pool->priv->obj_array = g_ptr_array_new ();
}

/**
 * pk_client_pool_finalize:
 * @object: The object to finalize
 **/
static void
pk_client_pool_finalize (GObject *object)
{
	PkClientPool *pool;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_CLIENT_POOL (object));
	pool = PK_CLIENT_POOL (object);
	g_return_if_fail (pool->priv != NULL);

	g_ptr_array_foreach (pool->priv->client_array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (pool->priv->client_array, TRUE);
	g_ptr_array_foreach (pool->priv->obj_array, (GFunc) pk_client_pool_free_obj, NULL);
	g_ptr_array_free (pool->priv->obj_array, TRUE);

	G_OBJECT_CLASS (pk_client_pool_parent_class)->finalize (object);
}

/**
 * pk_client_pool_new:
 *
 * Return value: a new PkClientPool object.
 **/
PkClientPool *
pk_client_pool_new (void)
{
	PkClientPool *pool;
	pool = g_object_new (PK_TYPE_CLIENT_POOL, NULL);
	return PK_CLIENT_POOL (pool);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_client_pool_test (EggTest *test)
{
	PkClientPool *pool;
	PkClient *client;
	guint size;

	if (!egg_test_start (test, "PkClientPool"))
		return;

	/************************************************************/
	egg_test_title (test, "create");
	pool = pk_client_pool_new ();
	egg_test_assert (test, pool != NULL);

	/************************************************************/
	egg_test_title (test, "make sure size is zero");
	size = pk_client_pool_get_size (pool);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size: %i", size);

	/************************************************************/
	egg_test_title (test, "create entry");
	client = pk_client_pool_create (pool);
	g_object_unref (client);
	egg_test_assert (test, (client != NULL));

	/************************************************************/
	egg_test_title (test, "make sure size is one");
	size = pk_client_pool_get_size (pool);
	if (size == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size: %i", size);

	g_object_unref (pool);

	egg_test_end (test);
}
#endif

