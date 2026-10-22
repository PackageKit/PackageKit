/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * PkSystemUpgrade:
 *
 * SystemUpgrade object
 *
 * This GObject represents one operating system release this system can be
 * upgraded to, as reported by GetSystemUpgrades. Its ID is what is passed to
 * UpgradeSystem to perform the upgrade.
 *
 * The #PkSystemUpgrade:os-id and #PkSystemUpgrade:version properties identify the
 * release in AppStream terms: `os-id` is the component ID of the AppStream
 * `operating-system` component and `version` matches the version of one of
 * its releases. AppStream carries the descriptive metadata of a release
 * (description, release date, end-of-life date, release notes), PackageKit
 * carries the information needed to actually perform the upgrade: That it
 * is available in the first place, the ID to request the upgrade, and the
 * download size.
 *
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include "pk-system-upgrade.h"
#include "pk-enum.h"
#include "pk-enum-types.h"

static void pk_system_upgrade_finalize (GObject *object);

/**
 * PkSystemUpgradePrivate:
 *
 * Private #PkSystemUpgrade data
 **/
struct _PkSystemUpgradePrivate
{
	gchar *id;
	gchar *os_id;
	gchar *version;
	gchar *name;
	gchar *summary;
	gchar *url;
	PkReleaseKindEnum kind;
	guint64 download_size;
};

enum {
	PROP_0,
	PROP_ID,
	PROP_OS_ID,
	PROP_VERSION,
	PROP_NAME,
	PROP_SUMMARY,
	PROP_URL,
	PROP_KIND,
	PROP_DOWNLOAD_SIZE,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (PkSystemUpgrade, pk_system_upgrade, PK_TYPE_SOURCE)
#define GET_PRIVATE(o) (pk_system_upgrade_get_instance_private (o))

/**
 * pk_system_upgrade_get_id:
 * @system_upgrade: a valid #PkSystemUpgrade object.
 *
 * Gets the identifier of the upgrade.
 * You use this value to call UpgradeSystem.
 *
 * Return value: the upgrade ID, e.g. "fedora-42"
 *
 * Since: 2.0.0
 **/
const gchar *
pk_system_upgrade_get_id (PkSystemUpgrade *system_upgrade)
{
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	g_return_val_if_fail (PK_IS_SYSTEM_UPGRADE (system_upgrade), NULL);

	return priv->id;
}

/**
 * pk_system_upgrade_get_os_id:
 * @system_upgrade: a valid #PkSystemUpgrade object.
 *
 * Gets the component ID of the operating system this upgrade
 * leads to, e.g. "org.fedoraproject.fedora".
 *
 * Return value: (nullable): the component ID of the target OS
 *
 * Since: 2.0.0
 **/
const gchar *
pk_system_upgrade_get_os_id (PkSystemUpgrade *system_upgrade)
{
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	g_return_val_if_fail (PK_IS_SYSTEM_UPGRADE (system_upgrade), NULL);

	return priv->os_id;
}

/**
 * pk_system_upgrade_get_version:
 * @system_upgrade: a valid #PkSystemUpgrade object.
 *
 * Gets the version of the operating system release this upgrade leads to.
 * It matches the version of the corresponding release of the AppStream
 * `operating-system` component, e.g. "42".
 *
 * Return value: (nullable): the target release version
 *
 * Since: 2.0.0
 **/
const gchar *
pk_system_upgrade_get_version (PkSystemUpgrade *system_upgrade)
{
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	g_return_val_if_fail (PK_IS_SYSTEM_UPGRADE (system_upgrade), NULL);

	return priv->version;
}

/**
 * pk_system_upgrade_get_name:
 * @system_upgrade: a valid #PkSystemUpgrade object.
 *
 * Gets the human readable name of the release, e.g. "Fedora 42".
 *
 * Return value: (nullable): the release name
 *
 * Since: 2.0.0
 **/
const gchar *
pk_system_upgrade_get_name (PkSystemUpgrade *system_upgrade)
{
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	g_return_val_if_fail (PK_IS_SYSTEM_UPGRADE (system_upgrade), NULL);

	return priv->name;
}

/**
 * pk_system_upgrade_get_summary:
 * @system_upgrade: a valid #PkSystemUpgrade object.
 *
 * Gets the one-line summary of the release.
 *
 * Return value: (nullable): the summary
 *
 * Since: 2.0.0
 **/
const gchar *
pk_system_upgrade_get_summary (PkSystemUpgrade *system_upgrade)
{
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	g_return_val_if_fail (PK_IS_SYSTEM_UPGRADE (system_upgrade), NULL);

	return priv->summary;
}

/**
 * pk_system_upgrade_get_url:
 * @system_upgrade: a valid #PkSystemUpgrade object.
 *
 * Gets the URL where a user can read the release notes or other details
 * about the release.
 *
 * Return value: (nullable): the URL
 *
 * Since: 2.0.0
 **/
const gchar *
pk_system_upgrade_get_url (PkSystemUpgrade *system_upgrade)
{
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	g_return_val_if_fail (PK_IS_SYSTEM_UPGRADE (system_upgrade), NULL);

	return priv->url;
}

/**
 * pk_system_upgrade_get_kind:
 * @system_upgrade: a valid #PkSystemUpgrade object.
 *
 * Gets the kind of release this upgrade leads to.
 *
 * Return value: the release kind, e.g. %PK_RELEASE_KIND_ENUM_STABLE
 *
 * Since: 2.0.0
 **/
PkReleaseKindEnum
pk_system_upgrade_get_kind (PkSystemUpgrade *system_upgrade)
{
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	g_return_val_if_fail (PK_IS_SYSTEM_UPGRADE (system_upgrade),
			      PK_RELEASE_KIND_ENUM_UNKNOWN);

	return priv->kind;
}

/**
 * pk_system_upgrade_get_download_size:
 * @system_upgrade: a valid #PkSystemUpgrade object.
 *
 * Gets the number of bytes the package manager has to download to perform
 * the upgrade, if the backend could determine this cheaply.
 *
 * Return value: the download size in bytes, or 0 if unknown
 *
 * Since: 2.0.0
 **/
guint64
pk_system_upgrade_get_download_size (PkSystemUpgrade *system_upgrade)
{
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	g_return_val_if_fail (PK_IS_SYSTEM_UPGRADE (system_upgrade), 0);

	return priv->download_size;
}

/*
 * pk_system_upgrade_get_property:
 **/
static void
pk_system_upgrade_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkSystemUpgrade *system_upgrade = PK_SYSTEM_UPGRADE (object);
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	switch (prop_id) {
	case PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	case PROP_OS_ID:
		g_value_set_string (value, priv->os_id);
		break;
	case PROP_VERSION:
		g_value_set_string (value, priv->version);
		break;
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_SUMMARY:
		g_value_set_string (value, priv->summary);
		break;
	case PROP_URL:
		g_value_set_string (value, priv->url);
		break;
	case PROP_KIND:
		g_value_set_enum (value, priv->kind);
		break;
	case PROP_DOWNLOAD_SIZE:
		g_value_set_uint64 (value, priv->download_size);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_system_upgrade_set_string:
 *
 * Store a string property; empty strings are stored as %NULL so that
 * "not reported" has a single representation.
 **/
static void
pk_system_upgrade_set_string (gchar **dest, const GValue *value)
{
	const gchar *str = g_value_get_string (value);

	g_free (*dest);
	*dest = (str != NULL && str[0] != '\0') ? g_strdup (str) : NULL;
}

/*
 * pk_system_upgrade_set_property:
 **/
static void
pk_system_upgrade_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	PkSystemUpgrade *system_upgrade = PK_SYSTEM_UPGRADE (object);
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	switch (prop_id) {
	case PROP_ID:
		pk_system_upgrade_set_string (&priv->id, value);
		break;
	case PROP_OS_ID:
		pk_system_upgrade_set_string (&priv->os_id, value);
		break;
	case PROP_VERSION:
		pk_system_upgrade_set_string (&priv->version, value);
		break;
	case PROP_NAME:
		pk_system_upgrade_set_string (&priv->name, value);
		break;
	case PROP_SUMMARY:
		pk_system_upgrade_set_string (&priv->summary, value);
		break;
	case PROP_URL:
		pk_system_upgrade_set_string (&priv->url, value);
		break;
	case PROP_KIND:
		priv->kind = g_value_get_enum (value);
		break;
	case PROP_DOWNLOAD_SIZE:
		priv->download_size = g_value_get_uint64 (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_system_upgrade_class_init:
 **/
static void
pk_system_upgrade_class_init (PkSystemUpgradeClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_system_upgrade_finalize;
	object_class->get_property = pk_system_upgrade_get_property;
	object_class->set_property = pk_system_upgrade_set_property;

	/**
	 * PkSystemUpgrade:id:
	 *
	 * The identifier to pass to UpgradeSystem, e.g. "fedora-42".
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_string ("id",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	/**
	 * PkSystemUpgrade:os-id:
	 *
	 * The component ID of the target operating system,
	 * e.g. "org.fedoraproject.fedora".
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_string ("os-id",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_OS_ID, pspec);

	/**
	 * PkSystemUpgrade:version:
	 *
	 * The version of the target release, matching the AppStream release
	 * version, e.g. "42".
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_string ("version",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_VERSION, pspec);

	/**
	 * PkSystemUpgrade:name:
	 *
	 * A human readable name of the release, e.g. "Fedora 42".
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_string ("name",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_NAME, pspec);

	/**
	 * PkSystemUpgrade:summary:
	 *
	 * A one-line summary of the release.
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_string ("summary",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_SUMMARY, pspec);

	/**
	 * PkSystemUpgrade:url:
	 *
	 * Where a user can read the release notes or details about the release.
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_string ("url",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_URL, pspec);

	/**
	 * PkSystemUpgrade:kind:
	 *
	 * The kind of OS release.
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_enum ("kind",
				   NULL,
				   NULL,
				   PK_TYPE_RELEASE_KIND_ENUM,
				   PK_RELEASE_KIND_ENUM_UNKNOWN,
				   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_KIND, pspec);

	/**
	 * PkSystemUpgrade:download-size:
	 *
	 * The number of bytes to download to perform the upgrade, 0 if unknown.
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_uint64 ("download-size",
				     NULL,
				     NULL,
				     0,
				     G_MAXUINT64,
				     0,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_DOWNLOAD_SIZE, pspec);
}

/*
 * pk_system_upgrade_init:
 **/
static void
pk_system_upgrade_init (PkSystemUpgrade *system_upgrade)
{
	system_upgrade->priv = GET_PRIVATE(system_upgrade);
}

/*
 * pk_system_upgrade_finalize:
 **/
static void
pk_system_upgrade_finalize (GObject *object)
{
	PkSystemUpgrade *system_upgrade = PK_SYSTEM_UPGRADE (object);
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	g_clear_pointer (&priv->id, g_free);
	g_clear_pointer (&priv->os_id, g_free);
	g_clear_pointer (&priv->version, g_free);
	g_clear_pointer (&priv->name, g_free);
	g_clear_pointer (&priv->summary, g_free);
	g_clear_pointer (&priv->url, g_free);

	G_OBJECT_CLASS (pk_system_upgrade_parent_class)->finalize (object);
}

/**
 * pk_system_upgrade_new:
 *
 * Return value: a new #PkSystemUpgrade object.
 *
 * Since: 2.0.0
 **/
PkSystemUpgrade *
pk_system_upgrade_new (void)
{
	PkSystemUpgrade *system_upgrade;
	system_upgrade = g_object_new (PK_TYPE_SYSTEM_UPGRADE, NULL);
	return PK_SYSTEM_UPGRADE (system_upgrade);
}

/**
 * pk_system_upgrade_new_full:
 * @id: the identifier to pass to UpgradeSystem, e.g. "fedora-42"
 * @os_id: (nullable): the component ID of the target OS
 * @version: (nullable): the target release version, e.g. "42"
 * @name: (nullable): a human readable name, e.g. "Fedora 42"
 * @summary: (nullable): a one-line summary of the release
 * @kind: the #PkReleaseKindEnum of the release
 * @download_size: the bytes to download to perform the upgrade, or 0
 * @url: (nullable): where to read the release notes
 *
 * Creates a new #PkSystemUpgrade with all its properties set.
 * Empty and %NULL strings are treated the same.
 *
 * Returns: (transfer full): a new #PkSystemUpgrade object.
 *
 * Since: 2.0.0
 **/
PkSystemUpgrade *
pk_system_upgrade_new_full (const gchar *id,
			const gchar *os_id,
			const gchar *version,
			const gchar *name,
			const gchar *summary,
			PkReleaseKindEnum kind,
			guint64 download_size,
			const gchar *url)
{
	g_return_val_if_fail (id != NULL, NULL);

	return g_object_new (PK_TYPE_SYSTEM_UPGRADE,
			     "id",
			     id,
			     "os-id",
			     os_id,
			     "version",
			     version,
			     "name",
			     name,
			     "summary",
			     summary,
			     "kind",
			     kind,
			     "download-size",
			     download_size,
			     "url",
			     url,
			     NULL);
}
