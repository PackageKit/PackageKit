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

/**
 * SECTION:pk-package
 * @short_description: TODO
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-package.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-package-id.h>

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
	gchar			*summary;
	PkInfoEnum		 info;
	gchar			*license;
	PkGroupEnum		 group;
	gchar			*description;
	gchar			*url;
	guint64			 size;
	gchar			*update_updates;
	gchar			*update_obsoletes;
	gchar			*update_vendor_url;
	gchar			*update_bugzilla_url;
	gchar			*update_cve_url;
	PkRestartEnum		 update_restart;
	gchar			*update_text;
	gchar			*update_changelog;
	PkUpdateStateEnum	 update_state;
	GDate			*update_issued;
	GDate			*update_updated;
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_ID,
	PROP_SUMMARY,
	PROP_INFO,
	PROP_LICENSE,
	PROP_GROUP,
	PROP_DESCRIPTION,
	PROP_URL,
	PROP_SIZE,
	PROP_UPDATE_UPDATES,
	PROP_UPDATE_OBSOLETES,
	PROP_UPDATE_VENDOR_URL,
	PROP_UPDATE_BUGZILLA_URL,
	PROP_UPDATE_CVE_URL,
	PROP_UPDATE_RESTART,
	PROP_UPDATE_UPDATE_TEXT,
	PROP_UPDATE_CHANGELOG,
	PROP_UPDATE_STATE,
	PROP_UPDATE_ISSUED,
	PROP_UPDATE_UPDATED,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkPackage, pk_package, G_TYPE_OBJECT)

/**
 * pk_package_set_id:
 * @package: a valid #PkPackage instance
 * @package_id: the valid package_id
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
	gchar **parts;
	g_return_if_fail (PK_IS_PACKAGE (package));

	parts = pk_package_id_split (package->priv->id);
	g_print ("%s-%s.%s\t%s\t%s\n",
		 parts[PK_PACKAGE_ID_NAME],
		 parts[PK_PACKAGE_ID_VERSION],
		 parts[PK_PACKAGE_ID_ARCH],
		 parts[PK_PACKAGE_ID_DATA],
		 package->priv->summary);
	g_strfreev (parts);
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
	case PROP_SUMMARY:
		g_value_set_string (value, priv->summary);
		break;
	case PROP_INFO:
		g_value_set_uint (value, priv->info);
		break;
	case PROP_LICENSE:
		g_value_set_string (value, priv->license);
		break;
	case PROP_GROUP:
		g_value_set_uint (value, priv->group);
		break;
	case PROP_DESCRIPTION:
		g_value_set_string (value, priv->description);
		break;
	case PROP_URL:
		g_value_set_string (value, priv->url);
		break;
	case PROP_SIZE:
		g_value_set_uint64 (value, priv->size);
		break;
	case PROP_UPDATE_UPDATES:
		g_value_set_string (value, priv->update_updates);
		break;
	case PROP_UPDATE_OBSOLETES:
		g_value_set_string (value, priv->update_obsoletes);
		break;
	case PROP_UPDATE_VENDOR_URL:
		g_value_set_string (value, priv->update_vendor_url);
		break;
	case PROP_UPDATE_BUGZILLA_URL:
		g_value_set_string (value, priv->update_bugzilla_url);
		break;
	case PROP_UPDATE_CVE_URL:
		g_value_set_string (value, priv->update_cve_url);
		break;
	case PROP_UPDATE_RESTART:
		g_value_set_uint (value, priv->update_restart);
		break;
	case PROP_UPDATE_UPDATE_TEXT:
		g_value_set_string (value, priv->update_text);
		break;
	case PROP_UPDATE_CHANGELOG:
		g_value_set_string (value, priv->update_changelog);
		break;
	case PROP_UPDATE_STATE:
		g_value_set_uint (value, priv->update_state);
		break;
	case PROP_UPDATE_ISSUED:
		g_value_set_pointer (value, priv->update_issued);
		break;
	case PROP_UPDATE_UPDATED:
		g_value_set_pointer (value, priv->update_updated);
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
	GDate *date;

	switch (prop_id) {
	case PROP_SUMMARY:
		g_free (priv->summary);
		priv->summary = g_strdup (g_value_get_string (value));
		break;
	case PROP_INFO:
		priv->info = g_value_get_uint (value);
		break;
	case PROP_LICENSE:
		g_free (priv->license);
		priv->license = g_strdup (g_value_get_string (value));
		break;
	case PROP_GROUP:
		priv->group = g_value_get_uint (value);
		break;
	case PROP_DESCRIPTION:
		g_free (priv->description);
		priv->description = g_strdup (g_value_get_string (value));
		break;
	case PROP_URL:
		g_free (priv->url);
		priv->url = g_strdup (g_value_get_string (value));
		break;
	case PROP_SIZE:
		priv->size = g_value_get_uint64 (value);
		break;
	case PROP_UPDATE_UPDATES:
		g_free (priv->update_updates);
		priv->update_updates = g_strdup (g_value_get_string (value));
		break;
	case PROP_UPDATE_OBSOLETES:
		g_free (priv->update_obsoletes);
		priv->update_obsoletes = g_strdup (g_value_get_string (value));
		break;
	case PROP_UPDATE_VENDOR_URL:
		g_free (priv->update_vendor_url);
		priv->update_vendor_url = g_strdup (g_value_get_string (value));
		break;
	case PROP_UPDATE_BUGZILLA_URL:
		g_free (priv->update_bugzilla_url);
		priv->update_bugzilla_url = g_strdup (g_value_get_string (value));
		break;
	case PROP_UPDATE_CVE_URL:
		g_free (priv->update_cve_url);
		priv->update_cve_url = g_strdup (g_value_get_string (value));
		break;
	case PROP_UPDATE_RESTART:
		priv->update_restart = g_value_get_uint (value);
		break;
	case PROP_UPDATE_UPDATE_TEXT:
		g_free (priv->update_text);
		priv->update_text = g_strdup (g_value_get_string (value));
		break;
	case PROP_UPDATE_CHANGELOG:
		g_free (priv->update_changelog);
		priv->update_changelog = g_strdup (g_value_get_string (value));
		break;
	case PROP_UPDATE_STATE:
		priv->update_state = g_value_get_uint (value);
		break;
	case PROP_UPDATE_ISSUED:
		if (priv->update_issued != NULL) {
			g_date_free (priv->update_issued);
			priv->update_issued = NULL;
		}
		date = g_value_get_pointer (value);
		if (date != NULL)
			priv->update_issued = g_date_new_dmy (date->day, date->month, date->year);
		break;
	case PROP_UPDATE_UPDATED:
		if (priv->update_updated != NULL) {
			g_date_free (priv->update_updated);
			priv->update_updated = NULL;
		}
		date = g_value_get_pointer (value);
		if (date != NULL)
			priv->update_updated = g_date_new_dmy (date->day, date->month, date->year);
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
				   0, G_MAXUINT, PK_INFO_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_INFO, pspec);

	/**
	 * PkPackage:license:
	 */
	pspec = g_param_spec_string ("license", NULL,
				     "The package license",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LICENSE, pspec);

	/**
	 * PkPackage:group:
	 */
	pspec = g_param_spec_uint ("group", NULL,
				   "The package group",
				   0, PK_GROUP_ENUM_UNKNOWN, PK_GROUP_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_GROUP, pspec);

	/**
	 * PkPackage:description:
	 */
	pspec = g_param_spec_string ("description", NULL,
				     "The package description",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DESCRIPTION, pspec);

	/**
	 * PkPackage:url:
	 */
	pspec = g_param_spec_string ("url", NULL,
				     "The package homepage URL",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_URL, pspec);

	/**
	 * PkPackage:size:
	 */
	pspec = g_param_spec_uint64 ("size", NULL,
				     "The package size",
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SIZE, pspec);

	/**
	 * PkPackage:update-updates:
	 */
	pspec = g_param_spec_string ("update-updates", NULL,
				     "The update packages",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_UPDATES, pspec);

	/**
	 * PkPackage:update-obsoletes:
	 */
	pspec = g_param_spec_string ("update-obsoletes", NULL,
				     "The update packages that are obsoleted",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_OBSOLETES, pspec);

	/**
	 * PkPackage:update-vendor-url:
	 */
	pspec = g_param_spec_string ("update-vendor-url", NULL,
				     "The update vendor URL",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_VENDOR_URL, pspec);

	/**
	 * PkPackage:update-bugzilla-url:
	 */
	pspec = g_param_spec_string ("update-bugzilla-url", NULL,
				     "The update bugzilla URL",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_BUGZILLA_URL, pspec);

	/**
	 * PkPackage:update-cve-url:
	 */
	pspec = g_param_spec_string ("update-cve-url", NULL,
				     "The update CVE URL",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_CVE_URL, pspec);

	/**
	 * PkPackage:update-restart:
	 */
	pspec = g_param_spec_uint ("update-restart", NULL,
				   "The update restart type",
				   0, PK_RESTART_ENUM_UNKNOWN, PK_RESTART_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_RESTART, pspec);

	/**
	 * PkPackage:update-text:
	 */
	pspec = g_param_spec_string ("update-text", NULL,
				     "The update description",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_UPDATE_TEXT, pspec);

	/**
	 * PkPackage:update-changelog:
	 */
	pspec = g_param_spec_string ("update-changelog", NULL,
				     "The update ChangeLog",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_CHANGELOG, pspec);

	/**
	 * PkPackage:update-state:
	 */
	pspec = g_param_spec_uint ("update-state", NULL,
				   "The update state",
				   0, PK_UPDATE_STATE_ENUM_UNKNOWN, PK_UPDATE_STATE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_STATE, pspec);

	/**
	 * PkPackage:update-issued:
	 */
	pspec = g_param_spec_pointer ("update-issued", NULL,
				      "When the update was issued",
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_ISSUED, pspec);

	/**
	 * PkPackage:update-updated:
	 */
	pspec = g_param_spec_pointer ("update-updated", NULL,
				      "When the update was last updated",
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_UPDATED, pspec);

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
	g_free (priv->summary);
	g_free (priv->license);
	g_free (priv->description);
	g_free (priv->url);
	g_free (priv->update_updates);
	g_free (priv->update_obsoletes);
	g_free (priv->update_vendor_url);
	g_free (priv->update_bugzilla_url);
	g_free (priv->update_cve_url);
	g_free (priv->update_text);
	g_free (priv->update_changelog);
	if (priv->update_issued != NULL)
		g_date_free (priv->update_issued);
	if (priv->update_updated != NULL)
		g_date_free (priv->update_updated);

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
pk_package_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
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
	egg_test_title (test, "get id of unset package");
	g_object_get (package, "id", &text, NULL);
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
	egg_test_title (test, "get id of set package");
	id = pk_package_get_id (package);
	egg_test_assert (test, (g_strcmp0 (id, "gnome-power-manager;0.1.2;i386;fedora") == 0));

	/************************************************************/
	egg_test_title (test, "get name of set package");
	g_object_get (package, "id", &text, NULL);
	egg_test_assert (test, (g_strcmp0 (text, "gnome-power-manager;0.1.2;i386;fedora") == 0));
	g_free (text);

	g_object_unref (package);
	egg_test_end (test);
}
#endif

