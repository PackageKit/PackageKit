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
 * SECTION:pk-distro-upgrade
 * @short_description: DistroUpgrade object
 *
 * This GObject represents a distro_upgrade from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-distro-upgrade.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-enum-types.h>

static void     pk_distro_upgrade_finalize	(GObject     *object);

#define PK_DISTRO_UPGRADE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_DISTRO_UPGRADE, PkDistroUpgradePrivate))

/**
 * PkDistroUpgradePrivate:
 *
 * Private #PkDistroUpgrade data
 **/
struct _PkDistroUpgradePrivate
{
	PkDistroUpgradeEnum		 state;
	gchar				*name;
	gchar				*summary;
};

enum {
	PROP_0,
	PROP_STATE,
	PROP_NAME,	/* FIXME: should be "ID" */
	PROP_SUMMARY,
	PROP_LAST
};

G_DEFINE_TYPE (PkDistroUpgrade, pk_distro_upgrade, PK_TYPE_SOURCE)

/**
 * pk_distro_upgrade_get_id:
 * @distro_upgrade: a valid #PkDistroUpgrade object.
 *
 * Gets the distribution identifier.
 * You use this value to call UpgradeSystem.
 *
 * Return value: the distro-id, e.g. "fedora-14"
 *
 * Since: 0.6.11
 **/
const gchar *
pk_distro_upgrade_get_id (PkDistroUpgrade *distro_upgrade)
{
	g_return_val_if_fail (PK_IS_DISTRO_UPGRADE (distro_upgrade), NULL);
	return distro_upgrade->priv->name;
}

/**
 * pk_distro_upgrade_get_summary:
 * @distro_upgrade: a valid #PkDistroUpgrade object.
 *
 * Gets the description of the distribution upgrade.
 *
 * Return value: the printable name, e.g. "Fedora 14"
 *
 * Since: 0.6.11
 **/
const gchar *
pk_distro_upgrade_get_summary (PkDistroUpgrade *distro_upgrade)
{
	g_return_val_if_fail (PK_IS_DISTRO_UPGRADE (distro_upgrade), NULL);
	return distro_upgrade->priv->summary;
}

/**
 * pk_distro_upgrade_get_state:
 * @distro_upgrade: a valid #PkDistroUpgrade object.
 *
 * Gets the status of the distribution upgrade.
 *
 * Return value: the printable name, e.g. %PK_DISTRO_UPGRADE_ENUM_UNSTABLE
 *
 * Since: 0.6.11
 **/
PkDistroUpgradeEnum
pk_distro_upgrade_get_state (PkDistroUpgrade *distro_upgrade)
{
	g_return_val_if_fail (PK_IS_DISTRO_UPGRADE (distro_upgrade), PK_DISTRO_UPGRADE_ENUM_UNKNOWN);
	return distro_upgrade->priv->state;
}


/*
 * pk_distro_upgrade_get_property:
 **/
static void
pk_distro_upgrade_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkDistroUpgrade *distro_upgrade = PK_DISTRO_UPGRADE (object);
	PkDistroUpgradePrivate *priv = distro_upgrade->priv;

	switch (prop_id) {
	case PROP_STATE:
		g_value_set_enum (value, priv->state);
		break;
	case PROP_NAME:
		g_value_set_string (value, priv->name);
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
 * pk_distro_upgrade_set_property:
 **/
static void
pk_distro_upgrade_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkDistroUpgrade *distro_upgrade = PK_DISTRO_UPGRADE (object);
	PkDistroUpgradePrivate *priv = distro_upgrade->priv;

	switch (prop_id) {
	case PROP_STATE:
		priv->state = g_value_get_enum (value);
		break;
	case PROP_NAME:
		g_free (priv->name);
		priv->name = g_strdup (g_value_get_string (value));
		break;
	case PROP_SUMMARY:
		g_free (priv->summary);
		priv->summary = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_distro_upgrade_class_init:
 **/
static void
pk_distro_upgrade_class_init (PkDistroUpgradeClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_distro_upgrade_finalize;
	object_class->get_property = pk_distro_upgrade_get_property;
	object_class->set_property = pk_distro_upgrade_set_property;

	/**
	 * PkDistroUpgrade:state:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("state", NULL, NULL,
				   PK_TYPE_DISTRO_UPGRADE_ENUM, PK_DISTRO_UPGRADE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_STATE, pspec);

	/**
	 * PkDistroUpgrade:name:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("name", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_NAME, pspec);

	/**
	 * PkDistroUpgrade:summary:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("summary", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SUMMARY, pspec);

	g_type_class_add_private (klass, sizeof (PkDistroUpgradePrivate));
}

/*
 * pk_distro_upgrade_init:
 **/
static void
pk_distro_upgrade_init (PkDistroUpgrade *distro_upgrade)
{
	distro_upgrade->priv = PK_DISTRO_UPGRADE_GET_PRIVATE (distro_upgrade);
}

/*
 * pk_distro_upgrade_finalize:
 **/
static void
pk_distro_upgrade_finalize (GObject *object)
{
	PkDistroUpgrade *distro_upgrade = PK_DISTRO_UPGRADE (object);
	PkDistroUpgradePrivate *priv = distro_upgrade->priv;

	g_free (priv->name);
	g_free (priv->summary);

	G_OBJECT_CLASS (pk_distro_upgrade_parent_class)->finalize (object);
}

/**
 * pk_distro_upgrade_new:
 *
 * Return value: a new PkDistroUpgrade object.
 *
 * Since: 0.5.4
 **/
PkDistroUpgrade *
pk_distro_upgrade_new (void)
{
	PkDistroUpgrade *distro_upgrade;
	distro_upgrade = g_object_new (PK_TYPE_DISTRO_UPGRADE, NULL);
	return PK_DISTRO_UPGRADE (distro_upgrade);
}

