/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2013 Richard Hughes <richard@hughsie.com>
 *
 * Most of this code was taken from Zif, libzif/zif-config.c
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
 * SECTION:hif-config
 * @short_description: System wide config options
 *
 * #HifConfig allows settings to be read from a central config file. Some
 * values can be overridden in a running instance.
 *
 * Before reading any data, the backing config file has to be set with
 * hif_config_set_filename() and any reads prior to that will fail.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <glib.h>

#include <rpm/rpmlib.h>

#include "hif-config.h"
#include "hif-utils.h"

#define HIF_CONFIG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), HIF_TYPE_CONFIG, HifConfigPrivate))

struct _HifConfigPrivate
{
	gboolean		 loaded;
	gchar			**basearch_list;
	gchar			*filename;
	GHashTable		*hash_override;
	GKeyFile		*file_default;
	GKeyFile		*file_override;
	GMutex			 mutex;
};

G_DEFINE_TYPE (HifConfig, hif_config, G_TYPE_OBJECT)
static gpointer hif_config_object = NULL;

/**
 * hif_config_load:
 **/
static gboolean
hif_config_load (HifConfig *config, GError **error)
{
	gboolean ret = TRUE;
	gboolean file_exists;
	GError *error_local = NULL;

	/* lock other threads */
	g_mutex_lock (&config->priv->mutex);

	/* already loaded */
	if (config->priv->loaded)
		goto out;

	/* nothing set */
	if (config->priv->filename == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "no filename set, you need to use "
				     "hif_config_set_filename()!");
		goto out;
	}

	/* check file exists */
	file_exists = g_file_test (config->priv->filename, G_FILE_TEST_IS_REGULAR);
	if (!file_exists) {
		g_debug ("override config file %s does not exist",
			 config->priv->filename);
		config->priv->loaded = TRUE;
		goto out;
	}

	/* load files */
	g_debug ("loading config file %s", config->priv->filename);
	ret = g_key_file_load_from_file (config->priv->file_default,
					 config->priv->filename,
					 G_KEY_FILE_NONE,
					 &error_local);
	if (!ret) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "failed to load config file %s: %s",
			     config->priv->filename,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* done */
	config->priv->loaded = TRUE;
out:
	/* unlock other threads */
	g_mutex_unlock (&config->priv->mutex);
	return ret;
}

/**
 * hif_config_unset:
 **/
gboolean
hif_config_unset (HifConfig *config, const gchar *key, GError **error)
{
	gboolean ret = TRUE;

	g_return_val_if_fail (HIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not loaded yet */
	ret = hif_config_load (config, error);
	if (!ret)
		goto out;

	/* remove */
	g_hash_table_remove (config->priv->hash_override, key);
out:
	return ret;
}

/**
 * hif_config_get_string:
 **/
gchar *
hif_config_get_string (HifConfig *config,
		       const gchar *key,
		       GError **error)
{
	const gchar *value_tmp;
	gboolean ret;
	gchar *value = NULL;

	g_return_val_if_fail (HIF_IS_CONFIG (config), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not loaded yet */
	ret = hif_config_load (config, error);
	if (!ret)
		goto out;

	/* exists as local override */
	value_tmp = g_hash_table_lookup (config->priv->hash_override, key);
	if (value_tmp != NULL) {
		value = g_strdup (value_tmp);
		goto out;
	}

	/* exists in the keyfile */
	value = g_key_file_get_string (config->priv->file_default,
				       "Backend", key, NULL);
	if (value != NULL)
		goto out;

	/* nothing matched */
	g_set_error (error, HIF_ERROR, PK_ERROR_ENUM_INTERNAL_ERROR,
		     "failed to get value for %s", key);
out:
	return value;
}

/**
 * hif_config_get_boolean:
 **/
gboolean
hif_config_get_boolean (HifConfig *config,
			const gchar *key,
			GError **error)
{
	gchar *value;
	gboolean ret = FALSE;

	g_return_val_if_fail (HIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get string value */
	value = hif_config_get_string (config, key, error);
	if (value == NULL)
		goto out;

	/* convert to bool */
	ret = (g_ascii_strcasecmp (value, "true") == 0 ||
	       g_ascii_strcasecmp (value, "yes") == 0 ||
	       g_ascii_strcasecmp (value, "1") == 0);
out:
	g_free (value);
	return ret;
}

/**
 * hif_config_get_strv:
 **/
gchar **
hif_config_get_strv (HifConfig *config,
		     const gchar *key,
		     GError **error)
{
	gchar *value;
	gchar **split = NULL;

	g_return_val_if_fail (HIF_IS_CONFIG (config), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get string value */
	value = hif_config_get_string (config, key, error);
	if (value == NULL)
		goto out;

	/* convert to array */
	split = g_strsplit (value, ",", -1);
out:
	g_free (value);
	return split;
}

/**
 * hif_config_get_uint:
 **/
guint
hif_config_get_uint (HifConfig *config,
		     const gchar *key,
		     GError **error)
{
	gchar *value;
	guint retval = G_MAXUINT;
	gchar *endptr = NULL;

	g_return_val_if_fail (HIF_IS_CONFIG (config), G_MAXUINT);
	g_return_val_if_fail (key != NULL, G_MAXUINT);
	g_return_val_if_fail (error == NULL || *error == NULL, G_MAXUINT);

	/* get string value */
	value = hif_config_get_string (config, key, error);
	if (value == NULL)
		goto out;

	/* convert to int */
	retval = g_ascii_strtoull (value, &endptr, 10);
	if (value == endptr) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "failed to convert '%s' to unsigned integer", value);
		goto out;
	}

out:
	g_free (value);
	return retval;
}

/**
 * hif_config_get_basearch_array:
 **/
gchar **
hif_config_get_basearch_array (HifConfig *config)
{
	g_return_val_if_fail (HIF_IS_CONFIG (config), NULL);
	return config->priv->basearch_list;
}

/**
 * hif_config_set_filename:
 **/
gboolean
hif_config_set_filename (HifConfig *config,
			 const gchar *filename,
			 GError **error)
{
	const gchar *text;
	gboolean ret = TRUE;
	gchar *basearch = NULL;
	GError *error_local = NULL;
	GPtrArray *array;
	guint i;

	g_return_val_if_fail (HIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already loaded */
	if (config->priv->loaded) {
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "config already loaded");
		goto out;
	}

	/* do we use te default? */
	if (filename == NULL) {
		config->priv->filename = g_build_filename (SYSCONFDIR,
							   "PackageKit",
							   "Hif.conf",
							   NULL);
	} else {
		config->priv->filename = g_strdup (filename);
	}
	g_debug ("using config %s", config->priv->filename);

	/* calculate the valid basearchs */
	basearch = hif_config_get_string (config, "basearch", &error_local);
	if (basearch == NULL) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "failed to get basearch: %s",
			     error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* add valid archs to array */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
	g_ptr_array_add (array, g_strdup (basearch));
	g_ptr_array_add (array, g_strdup ("noarch"));
	if (g_strcmp0 (basearch, "i386") == 0) {
		g_ptr_array_add (array, g_strdup ("i486"));
		g_ptr_array_add (array, g_strdup ("i586"));
		g_ptr_array_add (array, g_strdup ("i686"));
	}

	/* copy into GStrv array */
	config->priv->basearch_list = g_new0 (gchar*, array->len+1);
	for (i = 0; i < array->len; i++) {
		text = g_ptr_array_index (array, i);
		config->priv->basearch_list[i] = g_strdup (text);
	}
	g_ptr_array_unref (array);
out:
	g_free (basearch);
	return ret;
}

/**
 * hif_config_reset_default:
 **/
gboolean
hif_config_reset_default (HifConfig *config, GError **error)
{
	g_return_val_if_fail (HIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_hash_table_remove_all (config->priv->hash_override);
	return TRUE;
}

/**
 * hif_config_set_string:
 **/
gboolean
hif_config_set_string (HifConfig *config,
		       const gchar *key,
		       const gchar *value,
		       GError **error)
{
	const gchar *value_tmp;
	gboolean ret = TRUE;

	g_return_val_if_fail (HIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already exists? */
	value_tmp = g_hash_table_lookup (config->priv->hash_override, key);
	if (value_tmp != NULL) {
		/* already set to the same value */
		if (g_strcmp0 (value_tmp, value) == 0)
			goto out;
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "already set key %s to %s, cannot overwrite with %s",
			     key, value_tmp, value);
		ret = FALSE;
		goto out;
	}

	/* insert into table */
	g_hash_table_insert (config->priv->hash_override,
			     g_strdup (key),
			     g_strdup (value));
out:
	return ret;
}

/**
 * hif_config_set_boolean:
 **/
gboolean
hif_config_set_boolean (HifConfig *config,
			const gchar *key,
			gboolean value,
			GError **error)
{
	return hif_config_set_string (config,
				      key,
				      value ? "true" : "false",
				      error);
}

/**
 * hif_config_set_uint:
 **/
gboolean
hif_config_set_uint (HifConfig *config,
		     const gchar *key,
		     guint value,
		     GError **error)
{
	gboolean ret;
	gchar *temp;
	temp = g_strdup_printf ("%i", value);
	ret = hif_config_set_string (config, key, temp, error);
	g_free (temp);
	return ret;
}

/**
 * hif_config_finalize:
 **/
static void
hif_config_finalize (GObject *object)
{
	HifConfig *config;
	g_return_if_fail (HIF_IS_CONFIG (object));
	config = HIF_CONFIG (object);

	g_free (config->priv->filename);
	g_hash_table_unref (config->priv->hash_override);
	g_key_file_free (config->priv->file_default);
	g_strfreev (config->priv->basearch_list);

	G_OBJECT_CLASS (hif_config_parent_class)->finalize (object);
}

/**
 * hif_config_class_init:
 **/
static void
hif_config_class_init (HifConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = hif_config_finalize;
	g_type_class_add_private (klass, sizeof (HifConfigPrivate));
}

/**
 * hif_config_init:
 **/
static void
hif_config_init (HifConfig *config)
{
	const gchar *value;

	config->priv = HIF_CONFIG_GET_PRIVATE (config);
	config->priv->file_default = g_key_file_new ();
	config->priv->hash_override = g_hash_table_new_full (g_str_hash,
							     g_str_equal,
							     g_free,
							     g_free);

	/* get info from RPM */
	rpmGetOsInfo (&value, NULL);
	hif_config_set_string (config, "osinfo", value, NULL);
	rpmGetArchInfo (&value, NULL);
	hif_config_set_string (config, "archinfo", value, NULL);
	rpmGetArchInfo (&value, NULL);
	if (g_strcmp0 (value, "i486") == 0 ||
	    g_strcmp0 (value, "i586") == 0 ||
	    g_strcmp0 (value, "i686") == 0)
		value = "i386";
	if (g_strcmp0 (value, "armv7l") == 0 ||
	    g_strcmp0 (value, "armv6l") == 0 ||
	    g_strcmp0 (value, "armv5tejl") == 0 ||
	    g_strcmp0 (value, "armv5tel") == 0)
		value = "arm";
	if (g_strcmp0 (value, "armv7hnl") == 0 ||
	    g_strcmp0 (value, "armv7hl") == 0)
		value = "armhfp";
	hif_config_set_string (config, "basearch", value, NULL);
}

/**
 * hif_config_new:
 **/
HifConfig *
hif_config_new (void)
{
	if (hif_config_object != NULL) {
		g_object_ref (hif_config_object);
	} else {
		hif_config_object = g_object_new (HIF_TYPE_CONFIG, NULL);
		g_object_add_weak_pointer (hif_config_object,
					   &hif_config_object);
	}
	return HIF_CONFIG (hif_config_object);
}
