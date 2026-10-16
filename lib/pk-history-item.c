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
 * PkHistoryItem:
 *
 * History item object
 *
 * This GObject represents a transaction recorded in the history database.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include "pk-history-item.h"
#include "pk-common.h"
#include "pk-enum.h"
#include "pk-enum-types.h"

static void pk_history_item_finalize (GObject *object);

/**
 * PkHistoryItemPrivate:
 *
 * Private #PkHistoryItem data
 **/
struct _PkHistoryItemPrivate
{
	gchar *tid;
	gchar *time_started;
	GDateTime *time_started_dt;
	gboolean succeeded;
	PkRoleEnum role;
	guint duration;	     /* ms */
	GPtrArray *packages; /* (element-type PkPackage) */
	guint uid;
	gchar *cmdline;
};

enum {
	PROP_0,
	PROP_TID,
	PROP_TIME_STARTED,
	PROP_SUCCEEDED,
	PROP_ROLE,
	PROP_DURATION,
	PROP_PACKAGES,
	PROP_UID,
	PROP_CMDLINE,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (PkHistoryItem, pk_history_item, PK_TYPE_SOURCE)
#define GET_PRIVATE(o) (pk_history_item_get_instance_private (o))

/**
 * pk_history_item_get_id:
 * @item: a valid #PkHistoryItem instance
 *
 * Gets the past transaction ID value;
 *
 * Return value: The transaction data
 *
 * Since: 0.8.11
 **/
const gchar *
pk_history_item_get_id (PkHistoryItem *item)
{
	PkHistoryItemPrivate *priv = GET_PRIVATE(item);

	g_return_val_if_fail (PK_IS_HISTORY_ITEM (item), NULL);

	return priv->tid;
}

/**
 * pk_history_item_get_time_started:
 * @item: a valid #PkHistoryItem instance
 *
 * Gets the time the past transaction was started.
 *
 * Return value: (transfer none) (nullable): the start time, or %NULL if unknown
 *
 * Since: 2.0.0
 **/
GDateTime *
pk_history_item_get_time_started (PkHistoryItem *item)
{
	PkHistoryItemPrivate *priv = GET_PRIVATE(item);

	g_return_val_if_fail (PK_IS_HISTORY_ITEM (item), NULL);

	if (priv->time_started_dt == NULL && priv->time_started != NULL)
		priv->time_started_dt = pk_iso8601_to_datetime (priv->time_started);
	return priv->time_started_dt;
}

/**
 * pk_history_item_get_succeeded:
 * @item: a valid #PkHistoryItem instance
 *
 * Gets the past transaction succeeded value;
 *
 * Return value: The transaction data
 *
 * Since: 0.8.11
 **/
gboolean
pk_history_item_get_succeeded (PkHistoryItem *item)
{
	PkHistoryItemPrivate *priv = GET_PRIVATE(item);

	g_return_val_if_fail (PK_IS_HISTORY_ITEM (item), FALSE);

	return priv->succeeded;
}

/**
 * pk_history_item_get_role:
 * @item: a valid #PkHistoryItem instance
 *
 * Gets the past transaction role;
 *
 * Return value: The transaction data
 *
 * Since: 0.8.11
 **/
PkRoleEnum
pk_history_item_get_role (PkHistoryItem *item)
{
	PkHistoryItemPrivate *priv = GET_PRIVATE(item);

	g_return_val_if_fail (PK_IS_HISTORY_ITEM (item), PK_ROLE_ENUM_UNKNOWN);

	return priv->role;
}

/**
 * pk_history_item_get_duration:
 * @item: a valid #PkHistoryItem instance
 *
 * Gets the past transaction duration;
 *
 * Return value: The transaction duration in ms
 *
 * Since: 0.8.11
 **/
guint
pk_history_item_get_duration (PkHistoryItem *item)
{
	PkHistoryItemPrivate *priv = GET_PRIVATE(item);

	g_return_val_if_fail (PK_IS_HISTORY_ITEM (item), 0);

	return priv->duration;
}

/**
 * pk_history_item_get_packages:
 * @item: a valid #PkHistoryItem instance
 *
 * Gets the packages that the past transaction acted on.
 *
 * Return value: (element-type PkPackage) (transfer none): the packages, or %NULL
 *
 * Since: 2.0.0
 **/
GPtrArray *
pk_history_item_get_packages (PkHistoryItem *item)
{
	PkHistoryItemPrivate *priv = GET_PRIVATE(item);

	g_return_val_if_fail (PK_IS_HISTORY_ITEM (item), NULL);
	return priv->packages;
}

/**
 * pk_history_item_get_uid:
 * @item: a valid #PkHistoryItem instance
 *
 * Gets the past transaction uid;
 *
 * Return value: The transaction data
 *
 * Since: 0.8.11
 **/
guint
pk_history_item_get_uid (PkHistoryItem *item)
{
	g_return_val_if_fail (PK_IS_HISTORY_ITEM (item), 0);
	return item->priv->uid;
}

/**
 * pk_history_item_get_cmdline:
 * @item: a valid #PkHistoryItem instance
 *
 * Gets the past transaction cmdline value;
 *
 * Return value: The transaction data
 *
 * Since: 0.8.11
 **/
const gchar *
pk_history_item_get_cmdline (PkHistoryItem *item)
{
	g_return_val_if_fail (PK_IS_HISTORY_ITEM (item), NULL);
	return item->priv->cmdline;
}

/*
 * pk_history_item_get_property:
 **/
static void
pk_history_item_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkHistoryItem *item = PK_HISTORY_ITEM (object);
	PkHistoryItemPrivate *priv = item->priv;

	switch (prop_id) {
	case PROP_TID:
		g_value_set_string (value, priv->tid);
		break;
	case PROP_TIME_STARTED:
		g_value_set_string (value, priv->time_started);
		break;
	case PROP_SUCCEEDED:
		g_value_set_boolean (value, priv->succeeded);
		break;
	case PROP_ROLE:
		g_value_set_enum (value, priv->role);
		break;
	case PROP_DURATION:
		g_value_set_uint (value, priv->duration);
		break;
	case PROP_PACKAGES:
		g_value_set_boxed (value, priv->packages);
		break;
	case PROP_UID:
		g_value_set_uint (value, priv->uid);
		break;
	case PROP_CMDLINE:
		g_value_set_string (value, priv->cmdline);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_history_item_set_property:
 **/
static void
pk_history_item_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	PkHistoryItem *item = PK_HISTORY_ITEM (object);
	PkHistoryItemPrivate *priv = GET_PRIVATE(item);

	switch (prop_id) {
	case PROP_TID:
		g_free (priv->tid);
		priv->tid = g_value_dup_string (value);
		break;
	case PROP_TIME_STARTED:
		g_free (priv->time_started);
		priv->time_started = g_value_dup_string (value);
		g_clear_pointer (&priv->time_started_dt, g_date_time_unref);
		break;
	case PROP_SUCCEEDED:
		priv->succeeded = g_value_get_boolean (value);
		break;
	case PROP_ROLE:
		priv->role = g_value_get_enum (value);
		break;
	case PROP_DURATION:
		priv->duration = g_value_get_uint (value);
		break;
	case PROP_PACKAGES:
		g_clear_pointer (&priv->packages, g_ptr_array_unref);
		priv->packages = g_value_dup_boxed (value);
		break;
	case PROP_UID:
		priv->uid = g_value_get_uint (value);
		break;
	case PROP_CMDLINE:
		g_free (priv->cmdline);
		priv->cmdline = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_history_item_class_init:
 **/
static void
pk_history_item_class_init (PkHistoryItemClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_history_item_finalize;
	object_class->get_property = pk_history_item_get_property;
	object_class->set_property = pk_history_item_set_property;

	/**
	 * PkHistoryItem:tid:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("tid",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_TID, pspec);

	/**
	 * PkHistoryItem:time-started:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("time-started",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_TIME_STARTED, pspec);

	/**
	 * PkHistoryItem:succeeded:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_boolean ("succeeded",
				      NULL,
				      NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_SUCCEEDED, pspec);

	/**
	 * PkHistoryItem:role:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("role",
				   NULL,
				   NULL,
				   PK_TYPE_ROLE_ENUM,
				   PK_ROLE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_ROLE, pspec);

	/**
	 * PkHistoryItem:duration:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_uint ("duration",
				   NULL,
				   NULL,
				   0,
				   G_MAXUINT,
				   0,
				   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_DURATION, pspec);

	/**
	 * PkHistoryItem:packages:
	 *
	 * The packages the transaction acted on, as a #GPtrArray of #PkPackage.
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_boxed ("packages",
				    NULL,
				    NULL,
				    G_TYPE_PTR_ARRAY,
				    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_PACKAGES, pspec);

	/**
	 * PkHistoryItem:uid:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_uint ("uid",
				   NULL,
				   NULL,
				   0,
				   G_MAXUINT,
				   G_MAXUINT,
				   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_UID, pspec);

	/**
	 * PkHistoryItem:cmdline:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("cmdline",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_CMDLINE, pspec);
}

/*
 * pk_history_item_init:
 **/
static void
pk_history_item_init (PkHistoryItem *item)
{
	item->priv = GET_PRIVATE(item);
}

/*
 * pk_history_item_finalize:
 **/
static void
pk_history_item_finalize (GObject *object)
{
	PkHistoryItem *item = PK_HISTORY_ITEM (object);
	PkHistoryItemPrivate *priv = GET_PRIVATE(item);

	g_clear_pointer (&priv->tid, g_free);
	g_clear_pointer (&priv->time_started, g_free);
	g_clear_pointer (&priv->time_started_dt, g_date_time_unref);
	g_clear_pointer (&priv->packages, g_ptr_array_unref);
	g_clear_pointer (&priv->cmdline, g_free);

	G_OBJECT_CLASS (pk_history_item_parent_class)->finalize (object);
}

/**
 * pk_history_item_new:
 *
 * Return value: a new #PkHistoryItem object.
 *
 * Since: 0.5.4
 **/
PkHistoryItem *
pk_history_item_new (void)
{
	PkHistoryItem *item;
	item = g_object_new (PK_TYPE_HISTORY_ITEM, NULL);
	return PK_HISTORY_ITEM (item);
}
