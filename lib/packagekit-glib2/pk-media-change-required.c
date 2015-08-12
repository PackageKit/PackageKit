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
 * SECTION:pk-media-change-required
 * @short_description: MediaChangeRequired object
 *
 * This GObject represents a media_change_required from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-media-change-required.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-enum-types.h>

static void     pk_media_change_required_finalize	(GObject     *object);

#define PK_MEDIA_CHANGE_REQUIRED_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_MEDIA_CHANGE_REQUIRED, PkMediaChangeRequiredPrivate))

/**
 * PkMediaChangeRequiredPrivate:
 *
 * Private #PkMediaChangeRequired data
 **/
struct _PkMediaChangeRequiredPrivate
{
	PkMediaTypeEnum			 media_type;
	gchar				*media_id;
	gchar				*media_text;
};

enum {
	PROP_0,
	PROP_MEDIA_TYPE,
	PROP_MEDIA_ID,
	PROP_MEDIA_TEXT,
	PROP_LAST
};

G_DEFINE_TYPE (PkMediaChangeRequired, pk_media_change_required, PK_TYPE_SOURCE)

/**
 * pk_media_change_required_get_property:
 **/
static void
pk_media_change_required_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkMediaChangeRequired *media_change_required = PK_MEDIA_CHANGE_REQUIRED (object);
	PkMediaChangeRequiredPrivate *priv = media_change_required->priv;

	switch (prop_id) {
	case PROP_MEDIA_TYPE:
		g_value_set_enum (value, priv->media_type);
		break;
	case PROP_MEDIA_ID:
		g_value_set_string (value, priv->media_id);
		break;
	case PROP_MEDIA_TEXT:
		g_value_set_string (value, priv->media_text);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_media_change_required_set_property:
 **/
static void
pk_media_change_required_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkMediaChangeRequired *media_change_required = PK_MEDIA_CHANGE_REQUIRED (object);
	PkMediaChangeRequiredPrivate *priv = media_change_required->priv;

	switch (prop_id) {
	case PROP_MEDIA_TYPE:
		priv->media_type = g_value_get_enum (value);
		break;
	case PROP_MEDIA_ID:
		g_free (priv->media_id);
		priv->media_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_MEDIA_TEXT:
		g_free (priv->media_text);
		priv->media_text = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_media_change_required_class_init:
 **/
static void
pk_media_change_required_class_init (PkMediaChangeRequiredClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_media_change_required_finalize;
	object_class->get_property = pk_media_change_required_get_property;
	object_class->set_property = pk_media_change_required_set_property;

	/**
	 * PkMediaChangeRequired:media-type:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("media-type", NULL, NULL,
				   PK_TYPE_MEDIA_TYPE_ENUM, PK_MEDIA_TYPE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_MEDIA_TYPE, pspec);

	/**
	 * PkMediaChangeRequired:media-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("media-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_MEDIA_ID, pspec);

	/**
	 * PkMediaChangeRequired:media-text:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("media-text", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_MEDIA_TEXT, pspec);

	g_type_class_add_private (klass, sizeof (PkMediaChangeRequiredPrivate));
}

/**
 * pk_media_change_required_init:
 **/
static void
pk_media_change_required_init (PkMediaChangeRequired *media_change_required)
{
	media_change_required->priv = PK_MEDIA_CHANGE_REQUIRED_GET_PRIVATE (media_change_required);
}

/**
 * pk_media_change_required_finalize:
 **/
static void
pk_media_change_required_finalize (GObject *object)
{
	PkMediaChangeRequired *media_change_required = PK_MEDIA_CHANGE_REQUIRED (object);
	PkMediaChangeRequiredPrivate *priv = media_change_required->priv;

	g_free (priv->media_id);
	g_free (priv->media_text);

	G_OBJECT_CLASS (pk_media_change_required_parent_class)->finalize (object);
}

/**
 * pk_media_change_required_new:
 *
 * Return value: a new PkMediaChangeRequired object.
 *
 * Since: 0.5.4
 **/
PkMediaChangeRequired *
pk_media_change_required_new (void)
{
	PkMediaChangeRequired *media_change_required;
	media_change_required = g_object_new (PK_TYPE_MEDIA_CHANGE_REQUIRED, NULL);
	return PK_MEDIA_CHANGE_REQUIRED (media_change_required);
}

