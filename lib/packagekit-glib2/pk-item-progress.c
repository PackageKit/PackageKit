/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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
 * Lesser General Public License for more item_progress.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-item-progress
 * @short_description: ItemProgress object
 *
 * This GObject represents a item_progress from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-item-progress.h>
#include <packagekit-glib2/pk-enum.h>

static void     pk_item_progress_finalize	(GObject     *object);

#define PK_ITEM_PROGRESS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_ITEM_PROGRESS, PkItemProgressPrivate))

/**
 * PkItemProgressPrivate:
 *
 * Private #PkItemProgress data
 **/
struct _PkItemProgressPrivate
{
	gchar				*package_id;
	PkStatusEnum			 status;
	guint				 percentage;
};

enum {
	PROP_0,
	PROP_PACKAGE_ID,
	PROP_STATUS,
	PROP_PERCENTAGE,
	PROP_LAST
};

G_DEFINE_TYPE (PkItemProgress, pk_item_progress, PK_TYPE_SOURCE)

/**
 * pk_item_progress_get_status:
 * @item_progress: a valid #PkItemProgress instance
 *
 * Get the status of this item.
 *
 * Return value: a #PkStatusEnum
 **/
guint
pk_item_progress_get_status (PkItemProgress *item_progress)
{
	return item_progress->priv->status;
}

/**
 * pk_item_progress_get_percentage:
 * @item_progress: a valid #PkItemProgress instance
 *
 * Get the percentage complete of this item.
 *
 * Return value: a progress percentage (0-100)
 **/
guint
pk_item_progress_get_percentage (PkItemProgress *item_progress)
{
	return item_progress->priv->percentage;
}

/**
 * pk_item_progress_get_package_id:
 * @item_progress: a valid #PkItemProgress instance
 *
 * Get the package ID this item is working on.
 *
 * Return value: a package ID
 **/
const gchar *
pk_item_progress_get_package_id (PkItemProgress *item_progress)
{
	return item_progress->priv->package_id;
}

/*
 * pk_item_progress_get_property:
 **/
static void
pk_item_progress_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkItemProgress *item_progress = PK_ITEM_PROGRESS (object);
	PkItemProgressPrivate *priv = item_progress->priv;

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_value_set_string (value, priv->package_id);
		break;
	case PROP_PERCENTAGE:
		g_value_set_uint (value, priv->percentage);
		break;
	case PROP_STATUS:
		g_value_set_uint (value, priv->status);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_item_progress_set_property:
 **/
static void
pk_item_progress_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkItemProgress *item_progress = PK_ITEM_PROGRESS (object);
	PkItemProgressPrivate *priv = item_progress->priv;

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_free (priv->package_id);
		priv->package_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_STATUS:
		priv->status = g_value_get_uint (value);
		break;
	case PROP_PERCENTAGE:
		priv->percentage = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_item_progress_class_init:
 **/
static void
pk_item_progress_class_init (PkItemProgressClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_item_progress_finalize;
	object_class->get_property = pk_item_progress_get_property;
	object_class->set_property = pk_item_progress_set_property;

	/**
	 * PkItemProgress:package-id:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_string ("package-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PACKAGE_ID, pspec);

	/**
	 * PkItemProgress:status:
	 *
	 * Since: 0.8.2
	 */
	pspec = g_param_spec_uint ("status", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_STATUS, pspec);

	/**
	 * PkItemProgress:percentage:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_uint ("percentage", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PERCENTAGE, pspec);

	g_type_class_add_private (klass, sizeof (PkItemProgressPrivate));
}

/*
 * pk_item_progress_init:
 **/
static void
pk_item_progress_init (PkItemProgress *item_progress)
{
	item_progress->priv = PK_ITEM_PROGRESS_GET_PRIVATE (item_progress);
}

/*
 * pk_item_progress_finalize:
 **/
static void
pk_item_progress_finalize (GObject *object)
{
	PkItemProgress *item_progress = PK_ITEM_PROGRESS (object);
	PkItemProgressPrivate *priv = item_progress->priv;

	g_free (priv->package_id);

	G_OBJECT_CLASS (pk_item_progress_parent_class)->finalize (object);
}

/**
 * pk_item_progress_new:
 *
 * An object containing item inside a #PkProgress.
 *
 * Return value: a new PkItemProgress object.
 *
 * Since: 0.8.1
 **/
PkItemProgress *
pk_item_progress_new (void)
{
	PkItemProgress *item_progress;
	item_progress = g_object_new (PK_TYPE_ITEM_PROGRESS, NULL);
	return PK_ITEM_PROGRESS (item_progress);
}

