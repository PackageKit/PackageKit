/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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
 * This GObject represents a system_upgrade from a transaction.
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
	PkReleaseKindEnum state;
	gchar *id;
	gchar *summary;
};

enum {
	PROP_0,
	PROP_STATE,
	PROP_ID,
	PROP_SUMMARY,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (PkSystemUpgrade, pk_system_upgrade, PK_TYPE_SOURCE)
#define GET_PRIVATE(o) (pk_system_upgrade_get_instance_private (o))

/**
 * pk_system_upgrade_get_id:
 * @system_upgrade: a valid #PkSystemUpgrade object.
 *
 * Gets the distribution identifier.
 * You use this value to call UpgradeSystem.
 *
 * Return value: the upgrade ID, e.g. "fedora-42"
 *
 * Since: 0.6.11
 **/
const gchar *
pk_system_upgrade_get_id (PkSystemUpgrade *system_upgrade)
{
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	g_return_val_if_fail (PK_IS_SYSTEM_UPGRADE (system_upgrade), NULL);

	return priv->id;
}

/**
 * pk_system_upgrade_get_summary:
 * @system_upgrade: a valid #PkSystemUpgrade object.
 *
 * Gets the description of the distribution upgrade.
 *
 * Return value: the printable name, e.g. "Fedora 14"
 *
 * Since: 0.6.11
 **/
const gchar *
pk_system_upgrade_get_summary (PkSystemUpgrade *system_upgrade)
{
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	g_return_val_if_fail (PK_IS_SYSTEM_UPGRADE (system_upgrade), NULL);

	return priv->summary;
}

/**
 * pk_system_upgrade_get_state:
 * @system_upgrade: a valid #PkSystemUpgrade object.
 *
 * Gets the status of the distribution upgrade.
 *
 * Return value: the printable name, e.g. %PK_RELEASE_KIND_ENUM_UNSTABLE
 *
 * Since: 0.6.11
 **/
PkReleaseKindEnum
pk_system_upgrade_get_state (PkSystemUpgrade *system_upgrade)
{
	PkSystemUpgradePrivate *priv = GET_PRIVATE(system_upgrade);

	g_return_val_if_fail (PK_IS_SYSTEM_UPGRADE (system_upgrade),
			      PK_RELEASE_KIND_ENUM_UNKNOWN);

	return priv->state;
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
	case PROP_STATE:
		g_value_set_enum (value, priv->state);
		break;
	case PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	case PROP_SUMMARY:
		g_value_set_string (value, priv->summary);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
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
	case PROP_STATE:
		priv->state = g_value_get_enum (value);
		break;
	case PROP_ID:
		g_free (priv->id);
		priv->id = g_value_dup_string (value);
		break;
	case PROP_SUMMARY:
		g_free (priv->summary);
		priv->summary = g_value_dup_string (value);
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
	 * PkSystemUpgrade:state:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("state",
				   NULL,
				   NULL,
				   PK_TYPE_RELEASE_KIND_ENUM,
				   PK_RELEASE_KIND_ENUM_UNKNOWN,
				   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_STATE, pspec);

	/**
	 * PkSystemUpgrade:id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("id",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	/**
	 * PkSystemUpgrade:summary:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("summary",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_SUMMARY, pspec);
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
	g_clear_pointer (&priv->summary, g_free);

	G_OBJECT_CLASS (pk_system_upgrade_parent_class)->finalize (object);
}

/**
 * pk_system_upgrade_new:
 *
 * Return value: a new #PkSystemUpgrade object.
 *
 * Since: 0.5.4
 **/
PkSystemUpgrade *
pk_system_upgrade_new (void)
{
	PkSystemUpgrade *system_upgrade;
	system_upgrade = g_object_new (PK_TYPE_SYSTEM_UPGRADE, NULL);
	return PK_SYSTEM_UPGRADE (system_upgrade);
}
