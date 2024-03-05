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
 * SECTION:pk-category
 * @short_description: Category object
 *
 * This GObject represents a category in the group system.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-category.h>

static void     pk_category_finalize	(GObject     *object);

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

static GParamSpec *obj_properties[PROP_LAST] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (PkCategory, pk_category, PK_TYPE_SOURCE)

/**
 * pk_category_get_parent_id:
 * @category: The #PkCategory
 *
 * Gets the parent category id.
 *
 * Returns: (nullable): the string value, or %NULL for unset.
 *
 * Since: 0.6.2
 **/
const gchar *
pk_category_get_parent_id (PkCategory *category)
{
	PkCategoryPrivate *priv = pk_category_get_instance_private (category);

	g_return_val_if_fail (PK_IS_CATEGORY (category), NULL);

	return priv->parent_id;
}

/**
 * pk_category_set_parent_id:
 * @category: The #PkCategory
 * @parent_id: the new value
 *
 * Sets the parent category id.
 *
 * Since: 0.6.2
 **/
void
pk_category_set_parent_id (PkCategory *category, const gchar *parent_id)
{
	PkCategoryPrivate *priv = pk_category_get_instance_private (category);

	g_return_if_fail (PK_IS_CATEGORY (category));

	if (g_strcmp0 (priv->parent_id, parent_id) == 0)
		return;

	g_free (priv->parent_id);
	priv->parent_id = g_strdup (parent_id);
	g_object_notify_by_pspec (G_OBJECT(category), obj_properties[PROP_PARENT_ID]);
}

/**
 * pk_category_get_id:
 * @category: The #PkCategory
 *
 * Gets the id specific to this category.
 *
 * Returns: (nullable): the string value, or %NULL for unset.
 *
 * Since: 0.6.2
 **/
const gchar *
pk_category_get_id (PkCategory *category)
{
	PkCategoryPrivate *priv = pk_category_get_instance_private (category);

	g_return_val_if_fail (PK_IS_CATEGORY (category), NULL);

	return priv->cat_id;
}

/**
 * pk_category_set_id:
 * @category: The #PkCategory
 * @cat_id: the new value
 *
 * Sets the id specific to this category.
 *
 * Since: 0.6.2
 **/
void
pk_category_set_id (PkCategory *category, const gchar *cat_id)
{
	PkCategoryPrivate *priv = pk_category_get_instance_private (category);

	g_return_if_fail (PK_IS_CATEGORY (category));

	if (g_strcmp0 (priv->cat_id, cat_id) == 0)
		return;

	g_free (priv->cat_id);
	priv->cat_id = g_strdup (cat_id);
	g_object_notify_by_pspec (G_OBJECT(category), obj_properties[PROP_CAT_ID]);
}

/**
 * pk_category_get_name:
 * @category: The #PkCategory
 *
 * Gets the name.
 *
 * Returns: (nullable): the string value, or %NULL for unset.
 *
 * Since: 0.6.2
 **/
const gchar *
pk_category_get_name (PkCategory *category)
{
	PkCategoryPrivate *priv = pk_category_get_instance_private (category);

	g_return_val_if_fail (PK_IS_CATEGORY (category), NULL);

	return priv->name;
}

/**
 * pk_category_set_name:
 * @category: The #PkCategory
 * @name: the new value
 *
 * Sets the name.
 *
 * Since: 0.6.2
 **/
void
pk_category_set_name (PkCategory *category, const gchar *name)
{
	PkCategoryPrivate *priv = pk_category_get_instance_private (category);

	g_return_if_fail (PK_IS_CATEGORY (category));

	if (g_strcmp0 (priv->name, name) == 0)
		return;

	g_free (priv->name);
	priv->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT(category), obj_properties[PROP_NAME]);
}

/**
 * pk_category_get_summary:
 * @category: The #PkCategory
 *
 * Gets the summary.
 *
 * Returns: (nullable): the string value, or %NULL for unset.
 *
 * Since: 0.6.2
 **/
const gchar *
pk_category_get_summary (PkCategory *category)
{
	PkCategoryPrivate *priv = pk_category_get_instance_private (category);

	g_return_val_if_fail (PK_IS_CATEGORY (category), NULL);

	return priv->summary;
}

/**
 * pk_category_set_summary:
 * @category: The #PkCategory
 * @summary: the new value
 *
 * Sets the summary.
 *
 * Since: 0.6.2
 **/
void
pk_category_set_summary (PkCategory *category, const gchar *summary)
{
	PkCategoryPrivate *priv = pk_category_get_instance_private (category);

	g_return_if_fail (PK_IS_CATEGORY (category));

	if (g_strcmp0 (priv->summary, summary) == 0)
		return;

	g_free (priv->summary);
	priv->summary = g_strdup (summary);
	g_object_notify_by_pspec (G_OBJECT(category), obj_properties[PROP_SUMMARY]);
}

/**
 * pk_category_get_icon:
 * @category: The #PkCategory
 *
 * Gets the icon filename.
 *
 * Returns: (nullable): the string value, or %NULL for unset.
 *
 * Since: 0.6.2
 **/
const gchar *
pk_category_get_icon (PkCategory *category)
{
	PkCategoryPrivate *priv = pk_category_get_instance_private (category);

	g_return_val_if_fail (PK_IS_CATEGORY (category), NULL);

	return priv->icon;
}

/**
 * pk_category_set_icon:
 * @category: The #PkCategory
 * @icon: the new value
 *
 * Sets the icon filename.
 *
 * Since: 0.6.2
 **/
void
pk_category_set_icon (PkCategory *category, const gchar *icon)
{
	PkCategoryPrivate *priv = pk_category_get_instance_private (category);

	g_return_if_fail (PK_IS_CATEGORY (category));

	if (g_strcmp0 (priv->icon, icon) == 0)
		return;

	g_free (priv->icon);
	priv->icon = g_strdup (icon);
	g_object_notify_by_pspec (G_OBJECT(category), obj_properties[PROP_ICON]);
}

/*
 * pk_category_get_property:
 **/
static void
pk_category_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkCategory *category = PK_CATEGORY (object);
	PkCategoryPrivate *priv = pk_category_get_instance_private (category);

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

/*
 * pk_category_set_property:
 **/
static void
pk_category_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkCategory *category = PK_CATEGORY (object);

	switch (prop_id) {
	case PROP_PARENT_ID:
		pk_category_set_parent_id (category, g_value_get_string (value));
		break;
	case PROP_CAT_ID:
		pk_category_set_id (category, g_value_get_string (value));
		break;
	case PROP_NAME:
		pk_category_set_name (category, g_value_get_string (value));
		break;
	case PROP_SUMMARY:
		pk_category_set_summary (category, g_value_get_string (value));
		break;
	case PROP_ICON:
		pk_category_set_icon (category, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_category_class_init:
 **/
static void
pk_category_class_init (PkCategoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_category_finalize;
	object_class->get_property = pk_category_get_property;
	object_class->set_property = pk_category_set_property;

	/**
	 * PkCategory:parent-id:
	 *
	 * Since: 0.5.4
	 */
	obj_properties[PROP_PARENT_ID] =
		g_param_spec_string ("parent-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * PkCategory:cat-id:
	 *
	 * Since: 0.5.4
	 */
	obj_properties[PROP_CAT_ID] =
		g_param_spec_string ("cat-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * PkCategory:name:
	 *
	 * Since: 0.5.4
	 */
	obj_properties[PROP_NAME] =
		g_param_spec_string ("name", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * PkCategory:summary:
	 *
	 * Since: 0.5.4
	 */
	obj_properties[PROP_SUMMARY] =
		g_param_spec_string ("summary", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * PkCategory:icon:
	 *
	 * Since: 0.5.4
	 */
	obj_properties[PROP_ICON] =
		g_param_spec_string ("icon", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, PROP_LAST, obj_properties);
}

/*
 * pk_category_init:
 **/
static void
pk_category_init (PkCategory *category)
{
	category->priv = pk_category_get_instance_private (category);
}

/*
 * pk_category_finalize:
 **/
static void
pk_category_finalize (GObject *object)
{
	PkCategory *category = PK_CATEGORY (object);
	PkCategoryPrivate *priv = pk_category_get_instance_private (category);

	g_clear_pointer (&priv->parent_id, g_free);
	g_clear_pointer (&priv->cat_id, g_free);
	g_clear_pointer (&priv->name, g_free);
	g_clear_pointer (&priv->summary, g_free);
	g_clear_pointer (&priv->icon, g_free);

	G_OBJECT_CLASS (pk_category_parent_class)->finalize (object);
}

/**
 * pk_category_new:
 *
 * Return value: a new #PkCategory object.
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

