/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-update-info
 * @short_description: Generic object to represent some information about an update.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "egg-debug.h"

#include "zif-update-info.h"

#define ZIF_UPDATE_INFO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_UPDATE_INFO, ZifUpdateInfoPrivate))

struct _ZifUpdateInfoPrivate
{
	ZifUpdateInfoKind	 kind;
	gchar			*url;
	gchar			*title;
};

enum {
	PROP_0,
	PROP_KIND,
	PROP_URL,
	PROP_TITLE,
	PROP_LAST
};

G_DEFINE_TYPE (ZifUpdateInfo, zif_update_info, G_TYPE_OBJECT)

/**
 * zif_update_info_get_kind:
 * @update_info: the #ZifUpdateInfo object
 *
 * Gets the update info kind.
 *
 * Return value: the kind of update info, e.g. %ZIF_UPDATE_INFO_KIND_CVE.
 *
 * Since: 0.0.1
 **/
ZifUpdateInfoKind
zif_update_info_get_kind (ZifUpdateInfo *update_info)
{
	g_return_val_if_fail (ZIF_IS_UPDATE_INFO (update_info), ZIF_UPDATE_INFO_KIND_LAST);
	return update_info->priv->kind;
}

/**
 * zif_update_info_get_url:
 * @update_info: the #ZifUpdateInfo object
 *
 * Gets the URL for this update.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.0.1
 **/
const gchar *
zif_update_info_get_url (ZifUpdateInfo *update_info)
{
	g_return_val_if_fail (ZIF_IS_UPDATE_INFO (update_info), NULL);
	return update_info->priv->url;
}

/**
 * zif_update_info_get_title:
 * @update_info: the #ZifUpdateInfo object
 *
 * Gets the title for this update.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.0.1
 **/
const gchar *
zif_update_info_get_title (ZifUpdateInfo *update_info)
{
	g_return_val_if_fail (ZIF_IS_UPDATE_INFO (update_info), NULL);
	return update_info->priv->title;
}

/**
 * zif_update_info_set_kind:
 * @update_info: the #ZifUpdateInfo object
 * @kind: the kind of update info, e.g. %ZIF_UPDATE_INFO_KIND_BUGZILLA
 *
 * Sets the update_info kind status.
 *
 * Since: 0.0.1
 **/
void
zif_update_info_set_kind (ZifUpdateInfo *update_info, ZifUpdateInfoKind kind)
{
	g_return_if_fail (ZIF_IS_UPDATE_INFO (update_info));
	update_info->priv->kind = kind;
}

/**
 * zif_update_info_set_url:
 * @update_info: the #ZifUpdateInfo object
 * @url: the update info URL
 *
 * Sets the update info URL.
 *
 * Since: 0.0.1
 **/
void
zif_update_info_set_url (ZifUpdateInfo *update_info, const gchar *url)
{
	g_return_if_fail (ZIF_IS_UPDATE_INFO (update_info));
	g_return_if_fail (url != NULL);
	g_return_if_fail (update_info->priv->url == NULL);

	update_info->priv->url = g_strdup (url);
}

/**
 * zif_update_info_set_title:
 * @update_info: the #ZifUpdateInfo object
 * @title: the update info title
 *
 * Sets the update info title.
 *
 * Since: 0.0.1
 **/
void
zif_update_info_set_title (ZifUpdateInfo *update_info, const gchar *title)
{
	g_return_if_fail (ZIF_IS_UPDATE_INFO (update_info));
	g_return_if_fail (title != NULL);
	g_return_if_fail (update_info->priv->title == NULL);

	update_info->priv->title = g_strdup (title);
}

/**
 * zif_update_info_kind_to_string:
 * @type: the #ZifUpdateInfoKind enumerated value
 *
 * Gets the string representation of a #ZifUpdateInfoKind
 *
 * Return value: The #ZifUpdateInfoKind represented as a string
 **/
const gchar *
zif_update_info_kind_to_string (ZifUpdateInfoKind kind)
{
	if (kind == ZIF_UPDATE_INFO_KIND_CVE)
		return "cve";
	if (kind == ZIF_UPDATE_INFO_KIND_BUGZILLA)
		return "bugzilla";
	return "unknown";
}

/**
 * zif_update_info_kind_from_string:
 * @type: the #ZifUpdateInfoKind enumerated value
 *
 * Gets the string representation of a #ZifUpdateInfoKind
 *
 * Return value: The #ZifUpdateInfoKind represented as a string
 **/
ZifUpdateInfoKind
zif_update_info_kind_from_string (const gchar *type)
{
	if (g_strcmp0 (type, "cve") == 0)
		return ZIF_UPDATE_INFO_KIND_CVE;
	if (g_strcmp0 (type, "bz") == 0)
		return ZIF_UPDATE_INFO_KIND_BUGZILLA;
	return ZIF_UPDATE_INFO_KIND_LAST;
}

/**
 * zif_update_info_get_property:
 **/
static void
zif_update_info_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	ZifUpdateInfo *update_info = ZIF_UPDATE_INFO (object);
	ZifUpdateInfoPrivate *priv = update_info->priv;

	switch (prop_id) {
	case PROP_KIND:
		g_value_set_uint (value, priv->kind);
		break;
	case PROP_URL:
		g_value_set_string (value, priv->url);
		break;
	case PROP_TITLE:
		g_value_set_string (value, priv->title);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_update_info_set_property:
 **/
static void
zif_update_info_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
}

/**
 * zif_update_info_finalize:
 **/
static void
zif_update_info_finalize (GObject *object)
{
	ZifUpdateInfo *update_info;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_UPDATE_INFO (object));
	update_info = ZIF_UPDATE_INFO (object);

	g_free (update_info->priv->url);
	g_free (update_info->priv->title);

	G_OBJECT_CLASS (zif_update_info_parent_class)->finalize (object);
}

/**
 * zif_update_info_class_init:
 **/
static void
zif_update_info_class_init (ZifUpdateInfoClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_update_info_finalize;
	object_class->get_property = zif_update_info_get_property;
	object_class->set_property = zif_update_info_set_property;

	/**
	 * ZifUpdateInfo:kind:
	 *
	 * Since: 0.0.1
	 */
	pspec = g_param_spec_uint ("kind", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_KIND, pspec);

	/**
	 * ZifUpdateInfo:url:
	 *
	 * Since: 0.0.1
	 */
	pspec = g_param_spec_string ("url", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_URL, pspec);

	/**
	 * ZifUpdateInfo:title:
	 *
	 * Since: 0.0.1
	 */
	pspec = g_param_spec_string ("title", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_TITLE, pspec);
	g_type_class_add_private (klass, sizeof (ZifUpdateInfoPrivate));
}

/**
 * zif_update_info_init:
 **/
static void
zif_update_info_init (ZifUpdateInfo *update_info)
{
	update_info->priv = ZIF_UPDATE_INFO_GET_PRIVATE (update_info);
	update_info->priv->kind = ZIF_UPDATE_INFO_KIND_LAST;
	update_info->priv->url = NULL;
	update_info->priv->title = NULL;
}

/**
 * zif_update_info_new:
 *
 * Return value: A new #ZifUpdateInfo class instance.
 *
 * Since: 0.0.1
 **/
ZifUpdateInfo *
zif_update_info_new (void)
{
	ZifUpdateInfo *update_info;
	update_info = g_object_new (ZIF_TYPE_UPDATE_INFO, NULL);
	return ZIF_UPDATE_INFO (update_info);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_update_info_test (EggTest *test)
{
	ZifUpdateInfo *update_info;

	if (!egg_test_start (test, "ZifUpdateInfo"))
		return;

	/************************************************************/
	egg_test_title (test, "get update_info");
	update_info = zif_update_info_new ();
	egg_test_assert (test, update_info != NULL);

	g_object_unref (update_info);

	egg_test_end (test);
}
#endif

