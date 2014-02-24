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
 * SECTION:pk-details
 * @short_description: Details object
 *
 * This GObject represents a details from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-details.h>

static void     pk_details_finalize	(GObject     *object);

#define PK_DETAILS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_DETAILS, PkDetailsPrivate))

/**
 * PkDetailsPrivate:
 *
 * Private #PkDetails data
 **/
struct _PkDetailsPrivate
{
	gchar				*package_id;
	gchar				*license;
	PkGroupEnum			 group;
	gchar				*description;
	gchar				*url;
	gchar                           *summary;
	guint64				 size;
};

enum {
	PROP_0,
	PROP_PACKAGE_ID,
	PROP_LICENSE,
	PROP_GROUP,
	PROP_DESCRIPTION,
	PROP_URL,
	PROP_SIZE,
	PROP_SUMMARY,
	PROP_LAST
};

G_DEFINE_TYPE (PkDetails, pk_details, PK_TYPE_SOURCE)

/**
 * pk_details_get_package_id:
 * @details: a #PkDetails instance
 *
 * Gets the PackageId for the details object.
 *
 * Return value: string value
 *
 * Since: 0.8.12
 **/
const gchar *
pk_details_get_package_id (PkDetails *details)
{
	g_return_val_if_fail (details != NULL, NULL);
	return details->priv->package_id;
}

/**
 * pk_details_get_license:
 * @details: a #PkDetails instance
 *
 * Gets the license for the details object.
 *
 * Return value: string value
 *
 * Since: 0.8.12
 **/
const gchar *
pk_details_get_license (PkDetails *details)
{
	g_return_val_if_fail (details != NULL, NULL);
	return details->priv->license;
}

/**
 * pk_details_get_group:
 * @details: a #PkDetails instance
 *
 * Gets the group for the details object.
 *
 * Return value: string value
 *
 * Since: 0.8.12
 **/
PkGroupEnum
pk_details_get_group (PkDetails *details)
{
	g_return_val_if_fail (details != NULL, PK_GROUP_ENUM_UNKNOWN);
	return details->priv->group;
}

/**
 * pk_details_get_description:
 * @details: a #PkDetails instance
 *
 * Gets the description for the details object.
 *
 * Return value: string value
 *
 * Since: 0.8.12
 **/
const gchar *
pk_details_get_description (PkDetails *details)
{
	g_return_val_if_fail (details != NULL, NULL);
	return details->priv->description;
}

/**
 * pk_details_get_url:
 * @details: a #PkDetails instance
 *
 * Gets the url for the details object.
 *
 * Return value: string value
 *
 * Since: 0.8.12
 **/
const gchar *
pk_details_get_url (PkDetails *details)
{
	g_return_val_if_fail (details != NULL, NULL);
	return details->priv->url;
}

/**
 * pk_details_get_summary:
 * @details: a #PkDetails instance
 *
 * Gets the summary for the details object.
 *
 * Return value: string value
 *
 * Since: 0.9.1
 **/
const gchar *
pk_details_get_summary (PkDetails *details)
{
	g_return_val_if_fail (details != NULL, NULL);
	return details->priv->summary;
}

/**
 * pk_details_get_size:
 * @details: a #PkDetails instance
 *
 * Gets the size for the details object.
 *
 * Return value: string value
 *
 * Since: 0.8.12
 **/
guint64
pk_details_get_size (PkDetails *details)
{
	g_return_val_if_fail (details != NULL, G_MAXUINT64);
	return details->priv->size;
}

/**
 * pk_details_get_property:
 **/
static void
pk_details_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkDetails *details = PK_DETAILS (object);
	PkDetailsPrivate *priv = details->priv;

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_value_set_string (value, priv->package_id);
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
	case PROP_SUMMARY:
		g_value_set_string (value, priv->summary);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_details_set_property:
 **/
static void
pk_details_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkDetails *details = PK_DETAILS (object);
	PkDetailsPrivate *priv = details->priv;

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_free (priv->package_id);
		priv->package_id = g_strdup (g_value_get_string (value));
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
	case PROP_SUMMARY:
		g_free (priv->summary);
		priv->summary = g_strdup (g_value_get_string (value));
		break;
	case PROP_SIZE:
		priv->size = g_value_get_uint64 (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_details_class_init:
 **/
static void
pk_details_class_init (PkDetailsClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_details_finalize;
	object_class->get_property = pk_details_get_property;
	object_class->set_property = pk_details_set_property;

	/**
	 * PkDetails:package-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("package-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PACKAGE_ID, pspec);

	/**
	 * PkDetails:license:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("license", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LICENSE, pspec);

	/**
	 * PkDetails:group:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_uint ("group", NULL, NULL,
				   0, G_MAXUINT, PK_GROUP_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_GROUP, pspec);

	/**
	 * PkDetails:description:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("description", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DESCRIPTION, pspec);

	/**
	 * PkDetails:url:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("url", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_URL, pspec);

	/**
	 * PkDetails:size:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_uint64 ("size", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SIZE, pspec);

	/**
	 * PkDetails:summary:
	 *
	 * Since: 0.9.1
	 */
	pspec = g_param_spec_string ("summary", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SUMMARY, pspec);

	g_type_class_add_private (klass, sizeof (PkDetailsPrivate));
}

/**
 * pk_details_init:
 **/
static void
pk_details_init (PkDetails *details)
{
	details->priv = PK_DETAILS_GET_PRIVATE (details);
}

/**
 * pk_details_finalize:
 **/
static void
pk_details_finalize (GObject *object)
{
	PkDetails *details = PK_DETAILS (object);
	PkDetailsPrivate *priv = details->priv;

	g_free (priv->package_id);
	g_free (priv->license);
	g_free (priv->description);
	g_free (priv->url);
	g_free (priv->summary);

	G_OBJECT_CLASS (pk_details_parent_class)->finalize (object);
}

/**
 * pk_details_new:
 *
 * Return value: a new PkDetails object.
 *
 * Since: 0.5.4
 **/
PkDetails *
pk_details_new (void)
{
	PkDetails *details;
	details = g_object_new (PK_TYPE_DETAILS, NULL);
	return PK_DETAILS (details);
}

