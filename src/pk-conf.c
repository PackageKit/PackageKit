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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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

#include "pk-debug.h"
#include "pk-conf.h"

#define PK_CONF_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CONF, PkConfPrivate))

struct PkConfPrivate
{
	GKeyFile		*keyfile;
};

G_DEFINE_TYPE (PkConf, pk_conf, G_TYPE_OBJECT)
static gpointer pk_conf_object = NULL;

/**
 * pk_conf_get_string:
 **/
gchar *
pk_conf_get_string (PkConf *conf, const gchar *key)
{
	gchar *value = NULL;
	GError *error = NULL;
	value = g_key_file_get_string (conf->priv->keyfile, "Daemon", key, &error);
	if (error != NULL) {
		/* set to missing value */
		value = PK_CONF_VALUE_STRING_MISSING;
		pk_debug ("%s read error: %s", key, error->message);
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
	gint value;
	GError *error = NULL;
	value = g_key_file_get_integer (conf->priv->keyfile, "Daemon", key, &error);
	if (error != NULL) {
		/* set to missing value */
		value = PK_CONF_VALUE_INT_MISSING;
		pk_debug ("%s read error: %s", key, error->message);
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
	g_return_if_fail (object != NULL);
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
 * pk_conf_init:
 *
 * initialises the conf class. NOTE: We expect conf objects
 * to *NOT* be removed or added during the session.
 * We only control the first conf object if there are more than one.
 **/
static void
pk_conf_init (PkConf *conf)
{
	gboolean ret;
	gchar *path;

	conf->priv = PK_CONF_GET_PRIVATE (conf);

#if PK_BUILD_LOCAL
	/* try a local path first */
	path = g_build_filename ("..", "etc", "PackageKit.conf", NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS) == FALSE) {
		pk_debug ("local config file not found '%s'", path);
		g_free (path);
		path = g_build_filename (SYSCONFDIR, "PackageKit", "PackageKit.conf", NULL);
	}
#else
	path = g_build_filename (SYSCONFDIR, "PackageKit", "PackageKit.conf", NULL);
#endif
	if (g_file_test (path, G_FILE_TEST_EXISTS) == FALSE) {
		pk_error ("config file not found '%s'", path);
	}
	pk_debug ("using config file '%s'", path);
	conf->priv->keyfile = g_key_file_new ();
	ret = g_key_file_load_from_file (conf->priv->keyfile, path,
					 G_KEY_FILE_KEEP_COMMENTS, NULL);
	g_free (path);
	if (ret == FALSE) {
		pk_error ("failed to parse config file!");
	}
}

/**
 * pk_conf_new:
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

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_conf (LibSelfTest *test)
{
	PkConf *conf;
	gchar *text;
	gint value;

	if (libst_start (test, "PkConf", CLASS_AUTO) == FALSE) {
		return;
	}


	/************************************************************/
	libst_title (test, "get an instance");
	conf = pk_conf_new ();
	if (conf != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get the default backend");
	text = pk_conf_get_string (conf, "DefaultBackend");
	if (text != PK_CONF_VALUE_STRING_MISSING) {
		libst_success (test, "got default backend '%s'", text);
	} else {
		libst_failed (test, "got NULL!");
	}

	/************************************************************/
	libst_title (test, "get a string that doesn't exist");
	text = pk_conf_get_string (conf, "FooBarBaz");
	if (text == PK_CONF_VALUE_STRING_MISSING) {
		libst_success (test, "got NULL", text);
	} else {
		libst_failed (test, "got return value '%s'", text);
	}

	/************************************************************/
	libst_title (test, "get the shutdown timeout");
	value = pk_conf_get_int (conf, "ShutdownTimeout");
	if (value != PK_CONF_VALUE_INT_MISSING) {
		libst_success (test, "got ShutdownTimeout '%i'", value);
	} else {
		libst_failed (test, "got %i", value);
	}

	/************************************************************/
	libst_title (test, "get an int that doesn't exist");
	value = pk_conf_get_int (conf, "FooBarBaz");
	if (value == PK_CONF_VALUE_INT_MISSING) {
		libst_success (test, "got %i", value);
	} else {
		libst_failed (test, "got return value '%i'", value);
	}

	g_object_unref (conf);

	libst_end (test);
}
#endif

