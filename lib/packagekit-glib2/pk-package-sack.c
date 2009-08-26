/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offsack: 8 -*-
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

/**
 * SECTION:pk-package-sack
 * @short_description: TODO
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <dbus/dbus-glib.h>

#include <packagekit-glib2/pk-package-sack.h>
#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>

#include "egg-debug.h"

static void     pk_package_sack_finalize	(GObject     *object);

#define PK_PACKAGE_SACK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PACKAGE_SACK, PkPackageSackPrivate))

/**
 * PkPackageSackPrivate:
 *
 * Private #PkPackageSack data
 **/
struct _PkPackageSackPrivate
{
	GPtrArray		*array;
	PkClient		*client;
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_ID,
	PROP_LAST
};

G_DEFINE_TYPE (PkPackageSack, pk_package_sack, G_TYPE_OBJECT)

/**
 * pk_package_sack_get_size:
 * @sack: a valid #PkPackageSack instance
 *
 * Gets the number of packages in the sack
 *
 * Return value: the number of packages in the sack
 **/
guint
pk_package_sack_get_size (PkPackageSack *sack)
{
	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);

	return sack->priv->array->len;
}

/**
 * pk_package_sack_get_index:
 * @sack: a valid #PkPackageSack instance
 * @i: the instance to get
 *
 * Gets a packages from the sack
 *
 * Return value: a %PkPackage instance
 **/
PkPackage *
pk_package_sack_get_index (PkPackageSack *sack, guint i)
{
	PkPackage *package = NULL;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);

	/* index invalid */
	if (i >= sack->priv->array->len)
		goto out;

	/* get object */
	package = g_object_ref (g_ptr_array_index (sack->priv->array, i));
out:
	return package;
}

/**
 * pk_package_sack_add_package:
 * @sack: a valid #PkPackageSack instance
 * @package: a valid #PkPackage instance
 *
 * Adds a package to the sack.
 *
 * Return value: %TRUE if the package was added to the sack
 **/
gboolean
pk_package_sack_add_package (PkPackageSack *sack, PkPackage *package)
{
	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (PK_IS_PACKAGE (package), FALSE);

	/* add to array */
	g_ptr_array_add (sack->priv->array, g_object_ref (package));

	return TRUE;
}

/**
 * pk_package_sack_add_package:
 * @sack: a valid #PkPackageSack instance
 * @package_id: a package_id descriptor
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Adds a package reference to the sack.
 *
 * Return value: %TRUE if the package was added to the sack
 **/
gboolean
pk_package_sack_add_package_by_id (PkPackageSack *sack, const gchar *package_id, GError **error)
{
	PkPackage *package;
	gboolean ret;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create new object */
	package = pk_package_new ();
	ret = pk_package_set_id (package, package_id, error);
	if (!ret) {
		g_object_unref (package);
		goto out;
	}
	
	/* add to array */
	g_ptr_array_add (sack->priv->array, package);
out:
	return ret;
}

/**
 * pk_package_sack_remove_package:
 * @sack: a valid #PkPackageSack instance
 * @package: a valid #PkPackage instance
 * @package_id: a package_id descriptor
 *
 * Removes a package reference from the sack. The pointers have to match exactly.
 *
 * Return value: %TRUE if the package was removed from the sack
 **/
gboolean
pk_package_sack_remove_package (PkPackageSack *sack, PkPackage *package)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (PK_IS_PACKAGE (package), FALSE);

	/* remove from array */
	ret = g_ptr_array_remove (sack->priv->array, package);

	return ret;
}

/**
 * pk_package_sack_remove_package_by_id:
 * @sack: a valid #PkPackageSack instance
 * @package_id: a package_id descriptor
 *
 * Removes a package reference from the sack. As soon as one package is removed
 * the search is stopped.
 *
 * Return value: %TRUE if the package was removed to the sack
 **/
gboolean
pk_package_sack_remove_package_by_id (PkPackageSack *sack, const gchar *package_id)
{
	PkPackage *package;
	const gchar *id;
	gboolean ret = FALSE;
	guint i;
	guint len;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	len = sack->priv->array->len;
	for (i=0; i<len; i++) {
		package = g_ptr_array_index (sack->priv->array, i);
		id = pk_package_get_id (package);
		if (g_strcmp0 (package_id, id) == 0) {
			g_ptr_array_remove_index (sack->priv->array, i);
			ret = TRUE;
			break;
		}
	}

	return ret;
}

/**
 * pk_package_sack_get_package_ids:
 **/
static gchar **
pk_package_sack_get_package_ids (PkPackageSack *sack)
{
	const gchar *id;
	gchar **package_ids;
	guint i;
	guint len;
	PkPackage *package;

	/* create array of package_ids */
	len = sack->priv->array->len;
	package_ids = g_new0 (gchar *, len+1);
	for (i=0; i<len; i++) {
		package = g_ptr_array_index (sack->priv->array, i);
		id = pk_package_get_id (package);
		package_ids[i] = g_strdup (id);
	}

	return package_ids;
}

typedef struct {
	PkRoleEnum		 role;
	gchar			**package_ids;
	PkPackageSack		*sack;
} PkPackageSackAction;

/**
 * pk_client_resolve_cb:
 **/
static void
pk_client_resolve_cb (GObject *object, GAsyncResult *result, PkPackageSackAction *action)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results;

	results = pk_client_resolve_finish (client, result, &error);
	if (results == NULL) {
		egg_warning ("failed to resolve: %s", error->message);
		g_error_free (error);
		goto out;
	}

	egg_warning ("results = %p", results);
	g_object_unref (results);
out:
//	g_free (tid);
	return;
}

/**
 * pk_package_sack_merge_resolve_async:
 * @sack: a valid #PkPackageSack instance
 *
 * Merges in details about packages using resolve.
 **/
void
pk_package_sack_merge_resolve_async (PkPackageSack *sack, GCancellable *cancellable,
			       PkPackageSackFinishedCb callback, gpointer user_data)
{
	PkPackageSackAction *action;

	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));
	g_return_if_fail (callback != NULL);

	/* create new action */
	action = g_new0 (PkPackageSackAction, 1);
//	action->callback = callback;
//	action->user_data = user_data;
	action->role = PK_ROLE_ENUM_RESOLVE;
	action->package_ids = pk_package_sack_get_package_ids (sack);
	action->sack = sack;

	/* get new tid */
	pk_client_resolve_async (sack->priv->client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), action->package_ids, cancellable, (GAsyncReadyCallback) pk_client_resolve_cb, action);
}


/**
 * pk_package_sack_get_property:
 **/
static void
pk_package_sack_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
//	PkPackageSack *sack = PK_PACKAGE_SACK (object);
//	PkPackageSackPrivate *priv = sack->priv;

	switch (prop_id) {
	case PROP_ID:
//		g_value_sack_string (value, priv->id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_package_sack_set_property:
 **/
static void
pk_package_sack_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
//	PkPackageSack *sack = PK_PACKAGE_SACK (object);
//	PkPackageSackPrivate *priv = sack->priv;

	switch (prop_id) {
	case PROP_ID:
//		priv->info = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_package_sack_class_init:
 * @klass: The PkPackageSackClass
 **/
static void
pk_package_sack_class_init (PkPackageSackClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = pk_package_sack_get_property;
	object_class->set_property = pk_package_sack_set_property;
	object_class->finalize = pk_package_sack_finalize;

	/**
	 * PkPackageSack:id:
	 */
	pspec = g_param_spec_string ("id", NULL,
				     "The full package_id, e.g. 'gnome-power-manager;0.1.2;i386;fedora'",
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ID, pspec);

#if 0
	/**
	 * PkPackageSack::changed:
	 * @sack: the #PkPackageSack instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the sack data may have changed.
	 **/
	signals [SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSACK (PkPackageSackClass, changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
#endif

	g_type_class_add_private (klass, sizeof (PkPackageSackPrivate));
}

/**
 * pk_package_sack_init:
 * @sack: This class instance
 **/
static void
pk_package_sack_init (PkPackageSack *sack)
{
	PkPackageSackPrivate *priv;
	sack->priv = PK_PACKAGE_SACK_GET_PRIVATE (sack);
	priv = sack->priv;

	priv->array = g_ptr_array_new_with_free_func (g_object_unref);
	priv->client = pk_client_new ();
}

/**
 * pk_package_sack_finalize:
 * @object: The object to finalize
 **/
static void
pk_package_sack_finalize (GObject *object)
{
	PkPackageSack *sack = PK_PACKAGE_SACK (object);
	PkPackageSackPrivate *priv = sack->priv;

//	g_free (priv->id);
	g_ptr_array_unref (priv->array);
	g_object_unref (priv->client);

	G_OBJECT_CLASS (pk_package_sack_parent_class)->finalize (object);
}

/**
 * pk_package_sack_new:
 *
 * Return value: a new PkPackageSack object.
 **/
PkPackageSack *
pk_package_sack_new (void)
{
	PkPackageSack *sack;
	sack = g_object_new (PK_TYPE_PACKAGE_SACK, NULL);
	return PK_PACKAGE_SACK (sack);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_package_sack_test (EggTest *test)
{
	gboolean ret;
	PkPackageSack *sack;
	const gchar *id;
	gchar *text;
	guint size;

	if (!egg_test_start (test, "PkPackageSack"))
		return;

	/************************************************************/
	egg_test_title (test, "get package_sack");
	sack = pk_package_sack_new ();
	egg_test_assert (test, sack != NULL);

	/************************************************************/
	egg_test_title (test, "get size of unused package sack");
	size = pk_package_sack_get_size (sack);
	egg_test_assert (test, (size == 0));

	/************************************************************/
	egg_test_title (test, "remove package not present");
	ret = pk_package_sack_remove_package_by_id (sack, "moo;moo;moo;moo");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "add package");
	ret = pk_package_sack_add_package_by_id (sack, "moo;moo;moo;moo", NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get size of package sack");
	size = pk_package_sack_get_size (sack);
	egg_test_assert (test, (size == 1));

	/************************************************************/
	egg_test_title (test, "remove package");
	ret = pk_package_sack_remove_package_by_id (sack, "moo;moo;moo;moo");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get size of package sack");
	size = pk_package_sack_get_size (sack);
	egg_test_assert (test, (size == 0));

	/************************************************************/
	egg_test_title (test, "remove already removed package");
	ret = pk_package_sack_remove_package_by_id (sack, "moo;moo;moo;moo");
	egg_test_assert (test, !ret);

//static void
//pk_client_test_copy_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, EggTest *test)
//{
//	egg_test_loop_quit (test);
//}


	pk_package_sack_merge_resolve_async (sack, NULL, (PkPackageSackFinishedCb) &ret, NULL);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "resolved in %i", egg_test_elapsed (test));

	g_object_unref (sack);
out:
	egg_test_end (test);
}
#endif

