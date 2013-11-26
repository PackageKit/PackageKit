/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include <glib/gi18n.h>
#include <glib.h>

#include "pk-conf.h"

#define PK_CONF_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CONF, PkConfPrivate))

struct PkConfPrivate
{
	GKeyFile		*keyfile;
};

G_DEFINE_TYPE (PkConf, pk_conf, G_TYPE_OBJECT)
static gpointer pk_conf_object = NULL;

/**
 * pk_conf_set_bool:
 **/
void
pk_conf_set_bool (PkConf *conf, const gchar *key, gboolean value)
{
	g_return_if_fail (PK_IS_CONF (conf));
	g_key_file_set_boolean (conf->priv->keyfile,
				PK_CONF_GROUP_NAME, key, value);
}

/**
 * pk_conf_set_string:
 **/
void
pk_conf_set_string (PkConf *conf, const gchar *key, const gchar *value)
{
	g_return_if_fail (PK_IS_CONF (conf));
	g_key_file_set_string (conf->priv->keyfile,
			       PK_CONF_GROUP_NAME, key, value);
}

/**
 * pk_conf_get_string:
 **/
gchar *
pk_conf_get_string (PkConf *conf, const gchar *key)
{
	gchar *value = NULL;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_CONF (conf), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	value = g_key_file_get_string (conf->priv->keyfile,
				       PK_CONF_GROUP_NAME, key, &error);
	if (value == NULL) {
		g_debug ("%s read error: %s", key, error->message);
		g_error_free (error);
	}
	return value;
}

/**
 * pk_conf_get_strv:
 *
 * Returns: (transfer none):
 **/
gchar **
pk_conf_get_strv (PkConf *conf, const gchar *key)
{
	gchar **value = NULL;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_CONF (conf), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	value = g_key_file_get_string_list (conf->priv->keyfile,
					    PK_CONF_GROUP_NAME, key,
					    NULL, &error);
	if (value == NULL) {
		g_debug ("%s read error: %s", key, error->message);
		g_error_free (error);
	}
	return value;
}

/**
 * pk_conf_get_int:
 **/
gint
pk_conf_get_int (PkConf *conf, const gchar *key)
{
	GError *error = NULL;
	gint value;

	g_return_val_if_fail (PK_IS_CONF (conf), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	value = g_key_file_get_integer (conf->priv->keyfile,
					PK_CONF_GROUP_NAME, key, &error);
	if (error != NULL) {
		/* set to missing value */
		value = PK_CONF_VALUE_INT_MISSING;
		g_debug ("%s read error: %s", key, error->message);
		g_error_free (error);
	}
	return value;
}

/**
 * pk_conf_get_bool:
 **/
gboolean
pk_conf_get_bool (PkConf *conf, const gchar *key)
{
	gboolean value;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_CONF (conf), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	value = g_key_file_get_boolean (conf->priv->keyfile,
					PK_CONF_GROUP_NAME, key, &error);
	if (error != NULL) {
		g_debug ("%s read error: %s", key, error->message);
		g_error_free (error);
	}
	return value;
}

/**
 * pk_conf_finalize:
 **/
static void
pk_conf_finalize (GObject *object)
{
	PkConf *conf;
	g_return_if_fail (PK_IS_CONF (object));
	conf = PK_CONF (object);

	g_key_file_free (conf->priv->keyfile);

	G_OBJECT_CLASS (pk_conf_parent_class)->finalize (object);
}

/**
 * pk_conf_class_init:
 **/
static void
pk_conf_class_init (PkConfClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_conf_finalize;
	g_type_class_add_private (klass, sizeof (PkConfPrivate));
}

/**
 * pk_conf_get_filename:
 **/
gchar *
pk_conf_get_filename (void)
{
	gchar *path;

#if PK_BUILD_LOCAL
	/* try a local path first */
	path = g_build_filename ("..", "etc", "PackageKit.conf", NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS))
		goto out;
	g_debug ("local config file not found '%s'", path);
	g_free (path);
#endif
	/* check the prefix path */
	path = g_build_filename (SYSCONFDIR, "PackageKit", "PackageKit.conf", NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS))
		goto out;

	/* none found! */
	g_warning ("config file not found '%s'", path);
	g_free (path);
	path = NULL;
out:
	return path;
}

/**
 * pk_conf_init:
 *
 * initializes the conf class. NOTE: We expect conf objects
 * to *NOT* be removed or added during the session.
 * We only control the first conf object if there are more than one.
 **/
static void
pk_conf_init (PkConf *conf)
{
	gboolean ret;
	gchar *path;

	conf->priv = PK_CONF_GET_PRIVATE (conf);
	path = pk_conf_get_filename ();
	if (path == NULL)
		g_error ("config file not found");
	g_debug ("using config file '%s'", path);
	conf->priv->keyfile = g_key_file_new ();
	ret = g_key_file_load_from_file (conf->priv->keyfile, path,
					 G_KEY_FILE_NONE, NULL);
	g_free (path);
	if (!ret)
		g_error ("failed to parse config file!");
}

/**
 * pk_conf_new:
 *
 * Return value: A new conf class instance.
 **/
PkConf *
pk_conf_new (void)
{
	if (pk_conf_object != NULL) {
		g_object_ref (pk_conf_object);
	} else {
		pk_conf_object = g_object_new (PK_TYPE_CONF, NULL);
		g_object_add_weak_pointer (pk_conf_object, &pk_conf_object);
	}
	return PK_CONF (pk_conf_object);
}

