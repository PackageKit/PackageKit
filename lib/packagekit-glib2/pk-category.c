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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:pk-category
 * @short_description: Category object
 *
 * This GObject represents a category in the group system.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-category.h>

#include "egg-debug.h"

static void     pk_category_finalize	(GObject     *object);

#define PK_CATEGORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CATEGORY, PkCategoryPrivate))

/**
 * PkCategoryPrivate:
 *
 * Private #PkCategory data
 **/
struct _PkCategoryPrivate
{
	gchar				*parent_id;
	gchar				*cat_id;
	gchar				*name;
	gchar				*summary;
	gchar				*icon;
};

enum {
	PROP_0,
	PROP_PARENT_ID,
	PROP_CAT_ID,
	PROP_NAME,
	PROP_SUMMARY,
	PROP_ICON,
	PROP_LAST
};

G_DEFINE_TYPE (PkCategory, pk_category, PK_TYPE_SOURCE)

/**
 * pk_category_get_property:
 **/
static void
pk_category_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkCategory *category = PK_CATEGORY (object);
	PkCategoryPrivate *priv = category->priv;

	switch (prop_id) {
	case PROP_PARENT_ID:
		g_value_set_string (value, priv->parent_id);
		break;
	case PROP_CAT_ID:
		g_value_set_string (value, priv->cat_id);
		break;
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_SUMMARY:
		g_value_set_string (value, priv->summary);
		break;
	case PROP_ICON:
		g_value_set_string (value, priv->icon);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_category_set_property:
 **/
static void
pk_category_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkCategory *category = PK_CATEGORY (object);
	PkCategoryPrivate *priv = category->priv;

	switch (prop_id) {
	case PROP_PARENT_ID:
		g_free (priv->parent_id);
		priv->parent_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_CAT_ID:
		g_free (priv->cat_id);
		priv->cat_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_NAME:
		g_free (priv->name);
		priv->name = g_strdup (g_value_get_string (value));
		break;
	case PROP_SUMMARY:
		g_free (priv->summary);
		priv->summary = g_strdup (g_value_get_string (value));
		break;
	case PROP_ICON:
		g_free (priv->icon);
		priv->icon = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_category_class_init:
 **/
static void
pk_category_class_init (PkCategoryClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_category_finalize;
	object_class->get_property = pk_category_get_property;
	object_class->set_property = pk_category_set_property;

	/**
	 * PkCategory:parent-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("parent-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PARENT_ID, pspec);

	/**
	 * PkCategory:cat-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("cat-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CAT_ID, pspec);

	/**
	 * PkCategory:name:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("name", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_NAME, pspec);

	/**
	 * PkCategory:summary:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("summary", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SUMMARY, pspec);

	/**
	 * PkCategory:icon:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("icon", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ICON, pspec);

	g_type_class_add_private (klass, sizeof (PkCategoryPrivate));
}

/**
 * pk_category_init:
 **/
static void
pk_category_init (PkCategory *category)
{
	category->priv = PK_CATEGORY_GET_PRIVATE (category);
}

/**
 * pk_category_finalize:
 **/
static void
pk_category_finalize (GObject *object)
{
	PkCategory *category = PK_CATEGORY (object);
	PkCategoryPrivate *priv = category->priv;

	g_free (priv->parent_id);
	g_free (priv->cat_id);
	g_free (priv->name);
	g_free (priv->summary);
	g_free (priv->icon);

	G_OBJECT_CLASS (pk_category_parent_class)->finalize (object);
}

/**
 * pk_category_new:
 *
 * Return value: a new PkCategory object.
 *
 * Since: 0.5.4
 **/
PkCategory *
pk_category_new (void)
{
	PkCategory *category;
	category = g_object_new (PK_TYPE_CATEGORY, NULL);
	return PK_CATEGORY (category);
}

