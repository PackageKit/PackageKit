/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-package
 * @short_description: Package object
 *
 * This GObject represents a package from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-package.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-enum-types.h>
#include <packagekit-glib2/pk-package-id.h>

static void     pk_package_finalize	(GObject     *object);

#define PK_PACKAGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PACKAGE, PkPackagePrivate))

/**
 * PkPackagePrivate:
 *
 * Private #PkPackage data
 **/
struct _PkPackagePrivate
{
	PkInfoEnum		 info;
	gchar			*package_id;
	gchar			*package_id_data;
	const gchar		*package_id_split[4];
	gchar			*summary;
	gchar			*license;
	PkGroupEnum		 group;
	gchar			*description;
	gchar			*url;
	guint64			 size;
	gchar			*update_updates;
	gchar			*update_obsoletes;
	gchar			**update_vendor_urls;
	gchar			**update_bugzilla_urls;
	gchar			**update_cve_urls;
	PkRestartEnum		 update_restart;
	gchar			*update_text;
	gchar			*update_changelog;
	PkUpdateStateEnum	 update_state;
	gchar			*update_issued;
	gchar			*update_updated;
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_INFO,
	PROP_PACKAGE_ID,
	PROP_SUMMARY,
	PROP_LICENSE,
	PROP_GROUP,
	PROP_DESCRIPTION,
	PROP_URL,
	PROP_SIZE,
	PROP_UPDATE_UPDATES,
	PROP_UPDATE_OBSOLETES,
	PROP_UPDATE_VENDOR_URLS,
	PROP_UPDATE_BUGZILLA_URLS,
	PROP_UPDATE_CVE_URLS,
	PROP_UPDATE_RESTART,
	PROP_UPDATE_UPDATE_TEXT,
	PROP_UPDATE_CHANGELOG,
	PROP_UPDATE_STATE,
	PROP_UPDATE_ISSUED,
	PROP_UPDATE_UPDATED,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkPackage, pk_package, PK_TYPE_SOURCE)

/**
 * pk_package_equal:
 * @package1: a valid #PkPackage instance
 * @package2: a valid #PkPackage instance
 *
 * Do the #PkPackage's have the same ID.
 *
 * Return value: %TRUE if the packages have the same package_id, info and summary.
 *
 * Since: 0.5.4
 **/
gboolean
pk_package_equal (PkPackage *package1, PkPackage *package2)
{
	g_return_val_if_fail (PK_IS_PACKAGE (package1), FALSE);
	g_return_val_if_fail (PK_IS_PACKAGE (package2), FALSE);
	return (g_strcmp0 (package1->priv->summary, package2->priv->summary) == 0 &&
	        g_strcmp0 (package1->priv->package_id, package2->priv->package_id) == 0 &&
	        package1->priv->info == package2->priv->info);
}

/**
 * pk_package_equal_id:
 * @package1: a valid #PkPackage instance
 * @package2: a valid #PkPackage instance
 *
 * Do the #PkPackage's have the same ID.
 *
 * Return value: %TRUE if the packages have the same package_id.
 *
 * Since: 0.5.4
 **/
gboolean
pk_package_equal_id (PkPackage *package1, PkPackage *package2)
{
	g_return_val_if_fail (PK_IS_PACKAGE (package1), FALSE);
	g_return_val_if_fail (PK_IS_PACKAGE (package2), FALSE);
	return (g_strcmp0 (package1->priv->package_id, package2->priv->package_id) == 0);
}

/**
 * pk_package_set_id:
 * @package: a valid #PkPackage instance
 * @package_id: the valid package_id
 * @error: a #GError to put the error code and message in, or %NULL
 *
 * Sets the package object to have the given ID
 *
 * Return value: %TRUE if the package_id was set
 *
 * Since: 0.5.4
 **/
gboolean
pk_package_set_id (PkPackage *package, const gchar *package_id, GError **error)
{
	PkPackagePrivate *priv = package->priv;
	gboolean ret;
	guint cnt = 0;
	guint i;

	g_return_val_if_fail (PK_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* free old data */
	g_free (priv->package_id);
	g_free (priv->package_id_data);

	/* copy the package-id into package_id_data, change the ';' into '\0'
	 * and reference the pointers in the const gchar * array */
	priv->package_id = g_strdup (package_id);
	priv->package_id_data = g_strdup (package_id);
	priv->package_id_split[0] = priv->package_id_data;
	for (i = 0; priv->package_id_data[i] != '\0'; i++) {
		if (package_id[i] == ';') {
			if (++cnt > 3)
				continue;
			priv->package_id_split[cnt] = &priv->package_id_data[i+1];
			priv->package_id_data[i] = '\0';
		}
	}
	if (cnt != 3) {
		ret = FALSE;
		g_set_error (error, 1, 0, "invalid number of sections %i", cnt);
		goto out;
	}

	/* name has to be valid */
	ret = (priv->package_id_split[0][0] != '\0');
	if (!ret) {
		g_set_error_literal (error, 1, 0, "name invalid");
		goto out;
	}
out:
	return ret;
}

/**
 * pk_package_parse:
 * @package: a valid #PkPackage instance
 * @data: the data describing the package
 * @error: a #GError to put the error code and message in, or %NULL
 *
 * Parses the data to populate the #PkPackage.
 *
 * Return value: %TRUE if the data was parsed correcty
 *
 * Since: 0.8.11
 **/
gboolean
pk_package_parse (PkPackage *package, const gchar *data, GError **error)
{
	g_auto(GStrv) sections = NULL;

	g_return_val_if_fail (PK_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* split */
	sections = g_strsplit (data, "\t", -1);
	if (g_strv_length (sections) != 3) {
		g_set_error_literal (error, 1, 0, "data invalid");
		return FALSE;
	}

	/* parse object */
	package->priv->info = pk_info_enum_from_string (sections[0]);
	if (!pk_package_set_id (package, sections[1], error))
		return FALSE;
	g_free (package->priv->summary);
	package->priv->summary = g_strdup (sections[2]);
	return TRUE;
}

/**
 * pk_package_get_info:
 * @package: a valid #PkPackage instance
 *
 * Gets the package object ID
 *
 * Return value: the #PkInfoEnum
 *
 * Since: 0.5.4
 **/
PkInfoEnum
pk_package_get_info (PkPackage *package)
{
	g_return_val_if_fail (PK_IS_PACKAGE (package), FALSE);
	return package->priv->info;
}

/**
 * pk_package_set_info:
 * @package: a valid #PkPackage instance
 * @info: the #PkInfoEnum
 *
 * Sets the package info enum.
 *
 * Since: 0.8.14
 **/
void
pk_package_set_info (PkPackage *package, PkInfoEnum info)
{
	g_return_if_fail (PK_IS_PACKAGE (package));
	package->priv->info = info;
}

/**
 * pk_package_set_summary:
 * @package: a valid #PkPackage instance
 * @summary: the package summary
 *
 * Sets the package summary.
 *
 * Since: 0.8.14
 **/
void
pk_package_set_summary (PkPackage *package, const gchar *summary)
{
	g_return_if_fail (PK_IS_PACKAGE (package));
	g_free (package->priv->summary);
	package->priv->summary = g_strdup (summary);
}

/**
 * pk_package_get_id:
 * @package: a valid #PkPackage instance
 *
 * Gets the package object ID
 *
 * Return value: the ID, or %NULL if unset
 *
 * Since: 0.5.4
 **/
const gchar *
pk_package_get_id (PkPackage *package)
{
	g_return_val_if_fail (PK_IS_PACKAGE (package), NULL);
	return package->priv->package_id;
}

/**
 * pk_package_get_summary:
 * @package: a valid #PkPackage instance
 *
 * Gets the package object ID
 *
 * Return value: the summary, or %NULL if unset
 *
 * Since: 0.5.4
 **/
const gchar *
pk_package_get_summary (PkPackage *package)
{
	g_return_val_if_fail (PK_IS_PACKAGE (package), NULL);
	return package->priv->summary;
}

/**
 * pk_package_get_name:
 * @package: a valid #PkPackage instance
 *
 * Gets the package name.
 *
 * Return value: the name, or %NULL if unset
 *
 * Since: 0.6.4
 **/
const gchar *
pk_package_get_name (PkPackage *package)
{
	g_return_val_if_fail (PK_IS_PACKAGE (package), NULL);
	return package->priv->package_id_split[PK_PACKAGE_ID_NAME];
}

/**
 * pk_package_get_version:
 * @package: a valid #PkPackage instance
 *
 * Gets the package version.
 *
 * Return value: the version, or %NULL if unset
 *
 * Since: 0.6.4
 **/
const gchar *
pk_package_get_version (PkPackage *package)
{
	g_return_val_if_fail (PK_IS_PACKAGE (package), NULL);
	return package->priv->package_id_split[PK_PACKAGE_ID_VERSION];
}

/**
 * pk_package_get_arch:
 * @package: a valid #PkPackage instance
 *
 * Gets the package arch.
 *
 * Return value: the arch, or %NULL if unset
 *
 * Since: 0.6.4
 **/
const gchar *
pk_package_get_arch (PkPackage *package)
{
	g_return_val_if_fail (PK_IS_PACKAGE (package), NULL);
	return package->priv->package_id_split[PK_PACKAGE_ID_ARCH];
}

/**
 * pk_package_get_data:
 * @package: a valid #PkPackage instance
 *
 * Gets the package data, which is usually the repository ID that contains the
 * package. Special ID's include "installed" for installed packages, and "local"
 * for local packages that exist on disk but not in a repository.
 *
 * Return value: the data, or %NULL if unset
 *
 * Since: 0.6.4
 **/
const gchar *
pk_package_get_data (PkPackage *package)
{
	g_return_val_if_fail (PK_IS_PACKAGE (package), NULL);
	return package->priv->package_id_split[PK_PACKAGE_ID_DATA];
}

/**
 * pk_package_print:
 * @package: a valid #PkPackage instance
 *
 * Prints details about the package to standard out.
 *
 * Since: 0.5.4
 **/
void
pk_package_print (PkPackage *package)
{
	PkPackagePrivate *priv = package->priv;
	g_return_if_fail (PK_IS_PACKAGE (package));
	g_print ("%s-%s.%s\t%s\t%s\n",
		 priv->package_id_split[PK_PACKAGE_ID_NAME],
		 priv->package_id_split[PK_PACKAGE_ID_VERSION],
		 priv->package_id_split[PK_PACKAGE_ID_ARCH],
		 priv->package_id_split[PK_PACKAGE_ID_DATA],
		 package->priv->summary);
}

/*
 * pk_package_get_property:
 **/
static void
pk_package_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkPackage *package = PK_PACKAGE (object);
	PkPackagePrivate *priv = package->priv;

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_value_set_string (value, priv->package_id);
		break;
	case PROP_SUMMARY:
		g_value_set_string (value, priv->summary);
		break;
	case PROP_INFO:
		g_value_set_enum (value, priv->info);
		break;
	case PROP_LICENSE:
		g_value_set_string (value, priv->license);
		break;
	case PROP_GROUP:
		g_value_set_enum (value, priv->group);
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
	case PROP_UPDATE_VENDOR_URLS:
		g_value_set_boxed (value, priv->update_vendor_urls);
		break;
	case PROP_UPDATE_BUGZILLA_URLS:
		g_value_set_boxed (value, priv->update_bugzilla_urls);
		break;
	case PROP_UPDATE_CVE_URLS:
		g_value_set_boxed (value, priv->update_cve_urls);
		break;
	case PROP_UPDATE_RESTART:
		g_value_set_enum (value, priv->update_restart);
		break;
	case PROP_UPDATE_UPDATE_TEXT:
		g_value_set_string (value, priv->update_text);
		break;
	case PROP_UPDATE_CHANGELOG:
		g_value_set_string (value, priv->update_changelog);
		break;
	case PROP_UPDATE_STATE:
		g_value_set_enum (value, priv->update_state);
		break;
	case PROP_UPDATE_ISSUED:
		g_value_set_string (value, priv->update_issued);
		break;
	case PROP_UPDATE_UPDATED:
		g_value_set_string (value, priv->update_updated);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_package_set_property:
 **/
static void
pk_package_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkPackage *package = PK_PACKAGE (object);
	PkPackagePrivate *priv = package->priv;

	switch (prop_id) {
	case PROP_INFO:
		pk_package_set_info (package, g_value_get_enum (value));
		break;
	case PROP_SUMMARY:
		pk_package_set_summary (package, g_value_get_string (value));
		break;
	case PROP_LICENSE:
		g_free (priv->license);
		priv->license = g_strdup (g_value_get_string (value));
		break;
	case PROP_GROUP:
		priv->group = g_value_get_enum (value);
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
	case PROP_UPDATE_VENDOR_URLS:
		g_strfreev (priv->update_vendor_urls);
		priv->update_vendor_urls = g_strdupv (g_value_get_boxed (value));
		break;
	case PROP_UPDATE_BUGZILLA_URLS:
		g_strfreev (priv->update_bugzilla_urls);
		priv->update_bugzilla_urls = g_strdupv (g_value_get_boxed (value));
		break;
	case PROP_UPDATE_CVE_URLS:
		g_strfreev (priv->update_cve_urls);
		priv->update_cve_urls = g_strdupv (g_value_get_boxed (value));
		break;
	case PROP_UPDATE_RESTART:
		priv->update_restart = g_value_get_enum (value);
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
		priv->update_state = g_value_get_enum (value);
		break;
	case PROP_UPDATE_ISSUED:
		g_free (priv->update_issued);
		priv->update_issued = g_strdup (g_value_get_string (value));
		break;
	case PROP_UPDATE_UPDATED:
		g_free (priv->update_updated);
		priv->update_updated = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
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
	 * PkPackage:info:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("info", NULL,
				   "The PkInfoEnum package type, e.g. PK_INFO_ENUM_NORMAL",
				   PK_TYPE_INFO_ENUM, PK_INFO_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_INFO, pspec);

	/**
	 * PkPackage:package-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("package-id", NULL,
				     "The full package_id, e.g. 'gnome-power-manager;0.1.2;i386;fedora'",
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_PACKAGE_ID, pspec);

	/**
	 * PkPackage:summary:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("summary", NULL,
				     "The package summary",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SUMMARY, pspec);

	/**
	 * PkPackage:license:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("license", NULL,
				     "The package license",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LICENSE, pspec);

	/**
	 * PkPackage:group:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("group", NULL,
				   "The package group",
				   PK_TYPE_GROUP_ENUM, PK_GROUP_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_GROUP, pspec);

	/**
	 * PkPackage:description:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("description", NULL,
				     "The package description",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DESCRIPTION, pspec);

	/**
	 * PkPackage:url:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("url", NULL,
				     "The package homepage URL",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_URL, pspec);

	/**
	 * PkPackage:size:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_uint64 ("size", NULL,
				     "The package size",
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SIZE, pspec);

	/**
	 * PkPackage:update-updates:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("update-updates", NULL,
				     "The update packages",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_UPDATES, pspec);

	/**
	 * PkPackage:update-obsoletes:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("update-obsoletes", NULL,
				     "The update packages that are obsoleted",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_OBSOLETES, pspec);

	/**
	 * PkPackage:update-vendor-urls:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_boxed ("update-vendor-urls", NULL,
				    "The update vendor URLs",
				    G_TYPE_STRV,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_VENDOR_URLS, pspec);

	/**
	 * PkPackage:update-bugzilla-urls:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_boxed ("update-bugzilla-urls", NULL,
				    "The update bugzilla URLs",
				    G_TYPE_STRV,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_BUGZILLA_URLS, pspec);

	/**
	 * PkPackage:update-cve-urls:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_boxed ("update-cve-urls", NULL,
				    "The update CVE URLs",
				    G_TYPE_STRV,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_CVE_URLS, pspec);

	/**
	 * PkPackage:update-restart:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("update-restart", NULL,
				   "The update restart type",
				   PK_TYPE_RESTART_ENUM, PK_RESTART_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_RESTART, pspec);

	/**
	 * PkPackage:update-text:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("update-text", NULL,
				     "The update description",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_UPDATE_TEXT, pspec);

	/**
	 * PkPackage:update-changelog:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("update-changelog", NULL,
				     "The update ChangeLog",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_CHANGELOG, pspec);

	/**
	 * PkPackage:update-state:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("update-state", NULL,
				   "The update state",
				   PK_TYPE_UPDATE_STATE_ENUM, PK_UPDATE_STATE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_STATE, pspec);

	/**
	 * PkPackage:update-issued:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("update-issued", NULL,
				     "When the update was issued",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_ISSUED, pspec);

	/**
	 * PkPackage:update-updated:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("update-updated", NULL,
				     "When the update was last updated",
				     NULL,
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

/*
 * pk_package_init:
 * @package: This class instance
 **/
static void
pk_package_init (PkPackage *package)
{
	package->priv = PK_PACKAGE_GET_PRIVATE (package);
	package->priv->package_id_split[PK_PACKAGE_ID_NAME] = NULL;
	package->priv->package_id_split[PK_PACKAGE_ID_VERSION] = NULL;
	package->priv->package_id_split[PK_PACKAGE_ID_ARCH] = NULL;
	package->priv->package_id_split[PK_PACKAGE_ID_DATA] = NULL;
}

/*
 * pk_package_finalize:
 * @object: The object to finalize
 **/
static void
pk_package_finalize (GObject *object)
{
	PkPackage *package = PK_PACKAGE (object);
	PkPackagePrivate *priv = package->priv;

	g_free (priv->package_id);
	g_free (priv->summary);
	g_free (priv->license);
	g_free (priv->description);
	g_free (priv->url);
	g_free (priv->update_updates);
	g_free (priv->update_obsoletes);
	g_strfreev (priv->update_vendor_urls);
	g_strfreev (priv->update_bugzilla_urls);
	g_strfreev (priv->update_cve_urls);
	g_free (priv->update_text);
	g_free (priv->update_changelog);
	g_free (priv->update_issued);
	g_free (priv->update_updated);
	g_free (priv->package_id_data);

	G_OBJECT_CLASS (pk_package_parent_class)->finalize (object);
}

/**
 * pk_package_new:
 *
 * Return value: a new PkPackage object.
 *
 * Since: 0.5.4
 **/
PkPackage *
pk_package_new (void)
{
	PkPackage *package;
	package = g_object_new (PK_TYPE_PACKAGE, NULL);
	return PK_PACKAGE (package);
}
