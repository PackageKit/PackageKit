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

/**
 * SECTION:pk-package
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

#include <packagekit-glib2/pk-package.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>

#include "egg-debug.h"

static void     pk_package_finalize	(GObject     *object);

#define PK_PACKAGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PACKAGE, PkPackagePrivate))

/**
 * PkPackagePrivate:
 *
 * Private #PkPackage data
 **/
struct _PkPackagePrivate
{
	gchar			*id;
	gchar			*id_name;
	gchar			*id_version;
	gchar			*id_arch;
	gchar			*id_data;
	gchar			*summary;
	PkInfoEnum		 info;
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_ID,
	PROP_ID_NAME,
	PROP_ID_VERSION,
	PROP_ID_ARCH,
	PROP_ID_DATA,
	PROP_SUMMARY,
	PROP_INFO,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkPackage, pk_package, G_TYPE_OBJECT)

/**
 * pk_package_set_id:
 * @package: a valid #PkPackage instance
 * @id: the valid package_id
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Sets the package object to have the given ID
 *
 * Return value: %TRUE if the package_id was set
 **/
gboolean
pk_package_set_id (PkPackage *package, const gchar *package_id, GError **error)
{
	gboolean ret;
	gchar **sections = NULL;
	PkPackagePrivate *priv = package->priv;

	g_return_val_if_fail (PK_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check valid UTF8 */
	ret = g_utf8_validate (package_id, -1, NULL);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "invalid UTF8!");
		goto out;
	}

	/* split by delimeter */
	sections = g_strsplit (package_id, ";", -1);
	ret = (g_strv_length (sections) == 4);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "invalid number of sections");
		goto out;
	}

	/* name has to be valid */
	ret = (sections[0][0] != '\0');
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "name invalid");
		goto out;
	}

	/* save */
	priv->id = g_strdup (package_id);
	priv->id_name = g_strdup (sections[0]);
	priv->id_version = g_strdup (sections[1]);
	priv->id_arch = g_strdup (sections[2]);
	priv->id_data = g_strdup (sections[3]);
out:
	g_strfreev (sections);
	return ret;
}

/**
 * pk_package_get_id:
 * @package: a valid #PkPackage instance
 *
 * Gets the package object ID
 *
 * Return value: the ID, or %NULL if unset
 **/
const gchar *
pk_package_get_id (PkPackage *package)
{
	g_return_val_if_fail (PK_IS_PACKAGE (package), FALSE);
	return package->priv->id;
}

/**
 * pk_package_print:
 * @package: a valid #PkPackage instance
 *
 * Prints details about the package to standard out.
 **/
void
pk_package_print (PkPackage *package)
{
	PkPackagePrivate *priv;
	g_return_if_fail (PK_IS_PACKAGE (package));

	priv = package->priv;
	g_print ("%s-%s.%s\t%s\t%s\n", priv->id_name, priv->id_version, priv->id_arch, priv->id_data, priv->summary);
}

/**
 * pk_package_get_property:
 **/
static void
pk_package_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkPackage *package = PK_PACKAGE (object);
	PkPackagePrivate *priv = package->priv;

	switch (prop_id) {
	case PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	case PROP_ID_NAME:
		g_value_set_string (value, priv->id_name);
		break;
	case PROP_ID_VERSION:
		g_value_set_string (value, priv->id_version);
		break;
	case PROP_ID_ARCH:
		g_value_set_string (value, priv->id_arch);
		break;
	case PROP_ID_DATA:
		g_value_set_string (value, priv->id_data);
		break;
	case PROP_SUMMARY:
		g_value_set_string (value, priv->summary);
		break;
	case PROP_INFO:
		g_value_set_uint (value, priv->info);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_package_set_property:
 **/
static void
pk_package_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkPackage *package = PK_PACKAGE (object);
	PkPackagePrivate *priv = package->priv;

	switch (prop_id) {
	case PROP_SUMMARY:
		g_free (priv->summary);
		priv->summary = g_strdup (g_value_get_string (value));
		break;
	case PROP_INFO:
		priv->info = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_package_class_init:
 * @klass: The PkPackageClass
 **/
static void
pk_package_class_init (PkPackageClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = pk_package_get_property;
	object_class->set_property = pk_package_set_property;
	object_class->finalize = pk_package_finalize;

	/**
	 * PkPackage:id:
	 */
	pspec = g_param_spec_string ("id", NULL,
				     "The full package_id, e.g. 'gnome-power-manager;0.1.2;i386;fedora'",
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	/**
	 * PkPackage:id-name:
	 */
	pspec = g_param_spec_string ("id-name", NULL,
				     "The package name, e.g. 'gnome-power-manager'",
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ID_NAME, pspec);

	/**
	 * PkPackage:id-version:
	 */
	pspec = g_param_spec_string ("id-version", NULL,
				     "The package version, e.g. '0.1.2'",
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ID_VERSION, pspec);

	/**
	 * PkPackage:id-arch:
	 */
	pspec = g_param_spec_string ("id-arch", NULL,
				     "The package architecture, e.g. 'i386'",
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ID_ARCH, pspec);

	/**
	 * PkPackage:id-data:
	 */
	pspec = g_param_spec_string ("id-data", NULL,
				     "The package data, e.g. 'fedora'",
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ID_DATA, pspec);

	/**
	 * PkPackage:summary:
	 */
	pspec = g_param_spec_string ("summary", NULL,
				     "The package summary",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SUMMARY, pspec);

	/**
	 * PkPackage:info:
	 */
	pspec = g_param_spec_uint ("info", NULL,
				   "The PkInfoEnum package type, e.g. PK_INFO_ENUM_NORMAL",
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_INFO, pspec);

	/**
	 * PkPackage::changed:
	 * @package: the #PkPackage instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the package data may have changed.
	 **/
	signals [SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkPackageClass, changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkPackagePrivate));
}

/**
 * pk_package_init:
 * @package: This class instance
 **/
static void
pk_package_init (PkPackage *package)
{
	package->priv = PK_PACKAGE_GET_PRIVATE (package);
}

/**
 * pk_package_finalize:
 * @object: The object to finalize
 **/
static void
pk_package_finalize (GObject *object)
{
	PkPackage *package = PK_PACKAGE (object);
	PkPackagePrivate *priv = package->priv;

	g_free (priv->id);
	g_free (priv->id_name);
	g_free (priv->id_version);
	g_free (priv->id_arch);
	g_free (priv->id_data);
	g_free (priv->summary);

	G_OBJECT_CLASS (pk_package_parent_class)->finalize (object);
}

/**
 * pk_package_new:
 *
 * Return value: a new PkPackage object.
 **/
PkPackage *
pk_package_new (void)
{
	PkPackage *package;
	package = g_object_new (PK_TYPE_PACKAGE, NULL);
	return PK_PACKAGE (package);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_package_test (EggTest *test)
{
	gboolean ret;
	PkPackage *package;
	const gchar *id;
	gchar *text;

	if (!egg_test_start (test, "PkPackage"))
		return;

	/************************************************************/
	egg_test_title (test, "get package");
	package = pk_package_new ();
	egg_test_assert (test, package != NULL);

	/************************************************************/
	egg_test_title (test, "get id of unset package");
	id = pk_package_get_id (package);
	egg_test_assert (test, (id == NULL));

	/************************************************************/
	egg_test_title (test, "get name of unset package");
	g_object_get (package, "id-name", &text, NULL);
	egg_test_assert (test, (text == NULL));
	g_free (text);

	/************************************************************/
	egg_test_title (test, "set invalid id");
	ret = pk_package_set_id (package, "gnome-power-manager", NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "set invalid id (sections)");
	ret = pk_package_set_id (package, "gnome-power-manager;0.1.2;i386", NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "set invalid name");
	ret = pk_package_set_id (package, ";0.1.2;i386;fedora", NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "set valid name");
	ret = pk_package_set_id (package, "gnome-power-manager;0.1.2;i386;fedora", NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get id of unset package");
	id = pk_package_get_id (package);
	egg_test_assert (test, (g_strcmp0 (id, "gnome-power-manager;0.1.2;i386;fedora") == 0));

	/************************************************************/
	egg_test_title (test, "get name of unset package");
	g_object_get (package, "id-name", &text, NULL);
	egg_test_assert (test, (g_strcmp0 (text, "gnome-power-manager") == 0));
	g_free (text);

	g_object_unref (package);
out:
	egg_test_end (test);
}
#endif

