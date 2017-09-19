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
 * SECTION:pk-require-restart
 * @short_description: RequireRestart object
 *
 * This GObject represents a requirement of restart from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-require-restart.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-enum-types.h>

static void     pk_require_restart_finalize	(GObject     *object);

#define PK_REQUIRE_RESTART_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_REQUIRE_RESTART, PkRequireRestartPrivate))

/**
 * PkRequireRestartPrivate:
 *
 * Private #PkRequireRestart data
 **/
struct _PkRequireRestartPrivate
{
	PkRestartEnum			 restart;
	gchar				*package_id;
};

enum {
	PROP_0,
	PROP_RESTART,
	PROP_PACKAGE_ID,
	PROP_LAST
};

G_DEFINE_TYPE (PkRequireRestart, pk_require_restart, PK_TYPE_SOURCE)

/*
 * pk_require_restart_get_property:
 **/
static void
pk_require_restart_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkRequireRestart *require_restart = PK_REQUIRE_RESTART (object);
	PkRequireRestartPrivate *priv = require_restart->priv;

	switch (prop_id) {
	case PROP_RESTART:
		g_value_set_enum (value, priv->restart);
		break;
	case PROP_PACKAGE_ID:
		g_value_set_string (value, priv->package_id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_require_restart_set_property:
 **/
static void
pk_require_restart_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkRequireRestart *require_restart = PK_REQUIRE_RESTART (object);
	PkRequireRestartPrivate *priv = require_restart->priv;

	switch (prop_id) {
	case PROP_RESTART:
		priv->restart = g_value_get_enum (value);
		break;
	case PROP_PACKAGE_ID:
		g_free (priv->package_id);
		priv->package_id = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_require_restart_class_init:
 **/
static void
pk_require_restart_class_init (PkRequireRestartClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_require_restart_finalize;
	object_class->get_property = pk_require_restart_get_property;
	object_class->set_property = pk_require_restart_set_property;

	/**
	 * PkRequireRestart:restart:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("restart", NULL, NULL,
				   PK_TYPE_RESTART_ENUM, PK_RESTART_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_RESTART, pspec);

	/**
	 * PkRequireRestart:package-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("package-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PACKAGE_ID, pspec);

	g_type_class_add_private (klass, sizeof (PkRequireRestartPrivate));
}

/*
 * pk_require_restart_init:
 **/
static void
pk_require_restart_init (PkRequireRestart *require_restart)
{
	require_restart->priv = PK_REQUIRE_RESTART_GET_PRIVATE (require_restart);
}

/*
 * pk_require_restart_finalize:
 **/
static void
pk_require_restart_finalize (GObject *object)
{
	PkRequireRestart *require_restart = PK_REQUIRE_RESTART (object);
	PkRequireRestartPrivate *priv = require_restart->priv;

	g_free (priv->package_id);

	G_OBJECT_CLASS (pk_require_restart_parent_class)->finalize (object);
}

/**
 * pk_require_restart_new:
 *
 * Return value: a new PkRequireRestart object.
 *
 * Since: 0.5.4
 **/
PkRequireRestart *
pk_require_restart_new (void)
{
	PkRequireRestart *require_restart;
	require_restart = g_object_new (PK_TYPE_REQUIRE_RESTART, NULL);
	return PK_REQUIRE_RESTART (require_restart);
}

