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
 * SECTION:pk-eula-required
 * @short_description: EulaRequired object
 *
 * This GObject represents a eula_required from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-eula-required.h>

static void     pk_eula_required_finalize	(GObject     *object);

#define PK_EULA_REQUIRED_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_EULA_REQUIRED, PkEulaRequiredPrivate))

/**
 * PkEulaRequiredPrivate:
 *
 * Private #PkEulaRequired data
 **/
struct _PkEulaRequiredPrivate
{
	gchar				*eula_id;
	gchar				*package_id;
	gchar				*vendor_name;
	gchar				*license_agreement;
};

enum {
	PROP_0,
	PROP_EULA_ID,
	PROP_PACKAGE_ID,
	PROP_VENDOR_NAME,
	PROP_LICENSE_AGREEMENT,
	PROP_LAST
};

G_DEFINE_TYPE (PkEulaRequired, pk_eula_required, PK_TYPE_SOURCE)

/**
 * pk_eula_required_get_property:
 **/
static void
pk_eula_required_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkEulaRequired *eula_required = PK_EULA_REQUIRED (object);
	PkEulaRequiredPrivate *priv = eula_required->priv;

	switch (prop_id) {
	case PROP_EULA_ID:
		g_value_set_string (value, priv->eula_id);
		break;
	case PROP_PACKAGE_ID:
		g_value_set_string (value, priv->package_id);
		break;
	case PROP_VENDOR_NAME:
		g_value_set_string (value, priv->vendor_name);
		break;
	case PROP_LICENSE_AGREEMENT:
		g_value_set_string (value, priv->license_agreement);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_eula_required_set_property:
 **/
static void
pk_eula_required_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkEulaRequired *eula_required = PK_EULA_REQUIRED (object);
	PkEulaRequiredPrivate *priv = eula_required->priv;

	switch (prop_id) {
	case PROP_EULA_ID:
		g_free (priv->eula_id);
		priv->eula_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_PACKAGE_ID:
		g_free (priv->package_id);
		priv->package_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_VENDOR_NAME:
		g_free (priv->vendor_name);
		priv->vendor_name = g_strdup (g_value_get_string (value));
		break;
	case PROP_LICENSE_AGREEMENT:
		g_free (priv->license_agreement);
		priv->license_agreement = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_eula_required_class_init:
 **/
static void
pk_eula_required_class_init (PkEulaRequiredClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_eula_required_finalize;
	object_class->get_property = pk_eula_required_get_property;
	object_class->set_property = pk_eula_required_set_property;

	/**
	 * PkEulaRequired:eula-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("eula-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_EULA_ID, pspec);

	/**
	 * PkEulaRequired:package-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("package-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PACKAGE_ID, pspec);

	/**
	 * PkEulaRequired:vendor-name:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("vendor-name", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_VENDOR_NAME, pspec);

	/**
	 * PkEulaRequired:license-agreement:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("license-agreement", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LICENSE_AGREEMENT, pspec);

	g_type_class_add_private (klass, sizeof (PkEulaRequiredPrivate));
}

/**
 * pk_eula_required_init:
 **/
static void
pk_eula_required_init (PkEulaRequired *eula_required)
{
	eula_required->priv = PK_EULA_REQUIRED_GET_PRIVATE (eula_required);
}

/**
 * pk_eula_required_finalize:
 **/
static void
pk_eula_required_finalize (GObject *object)
{
	PkEulaRequired *eula_required = PK_EULA_REQUIRED (object);
	PkEulaRequiredPrivate *priv = eula_required->priv;

	g_free (priv->eula_id);
	g_free (priv->package_id);
	g_free (priv->vendor_name);
	g_free (priv->license_agreement);

	G_OBJECT_CLASS (pk_eula_required_parent_class)->finalize (object);
}

/**
 * pk_eula_required_new:
 *
 * Return value: a new PkEulaRequired object.
 *
 * Since: 0.5.4
 **/
PkEulaRequired *
pk_eula_required_new (void)
{
	PkEulaRequired *eula_required;
	eula_required = g_object_new (PK_TYPE_EULA_REQUIRED, NULL);
	return PK_EULA_REQUIRED (eula_required);
}

