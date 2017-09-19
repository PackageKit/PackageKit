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
 * SECTION:pk-transaction-past
 * @short_description: TransactionPast object
 *
 * This GObject represents a transaction_past from a transaction_past.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-transaction-past.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-enum-types.h>

static void     pk_transaction_past_finalize	(GObject     *object);

#define PK_TRANSACTION_PAST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION_PAST, PkTransactionPastPrivate))

/**
 * PkTransactionPastPrivate:
 *
 * Private #PkTransactionPast data
 **/
struct _PkTransactionPastPrivate
{
	gchar				*tid;
	gchar				*timespec;
	gboolean			 succeeded;
	PkRoleEnum			 role;
	guint				 duration; /* ms */
	gchar				*data;
	guint				 uid;
	gchar				*cmdline;
};

enum {
	PROP_0,
	PROP_TID,
	PROP_TIMESPEC,
	PROP_SUCCEEDED,
	PROP_ROLE,
	PROP_DURATION,
	PROP_DATA,
	PROP_UID,
	PROP_CMDLINE,
	PROP_LAST
};

G_DEFINE_TYPE (PkTransactionPast, pk_transaction_past, PK_TYPE_SOURCE)

/**
 * pk_transaction_past_get_id:
 * @past: a valid #PkTransactionPast instance
 *
 * Gets the past transaction ID value;
 *
 * Return value: The transaction data
 *
 * Since: 0.8.11
 **/
const gchar *
pk_transaction_past_get_id (PkTransactionPast *past)
{
	g_return_val_if_fail (PK_IS_TRANSACTION_PAST (past), NULL);
	return past->priv->tid;
}

/**
 * pk_transaction_past_get_timespec:
 * @past: a valid #PkTransactionPast instance
 *
 * Gets the past transaction timespec value;
 *
 * Return value: The transaction data
 *
 * Since: 0.8.11
 **/
const gchar *
pk_transaction_past_get_timespec (PkTransactionPast *past)
{
	g_return_val_if_fail (PK_IS_TRANSACTION_PAST (past), NULL);
	return past->priv->timespec;
}

/**
 * pk_transaction_past_get_datetime:
 * @past: a valid #PkTransactionPast instance
 *
 * Gets the past transaction date & time value;
 *
 * Return value: The transaction data, or %NULL if it's not available
 *
 * Since: 0.8.11
 **/
GDateTime *
pk_transaction_past_get_datetime (PkTransactionPast *past)
{
	g_return_val_if_fail (PK_IS_TRANSACTION_PAST (past), NULL);
	if (past->priv->timespec == NULL)
		return NULL;
	return pk_iso8601_to_datetime (past->priv->timespec);
}

/**
 * pk_transaction_past_get_timestamp:
 * @past: a valid #PkTransactionPast instance
 *
 * Gets the past transaction timestamp
 *
 * Return value: The transaction data, or 0 if it's not available
 *
 * Since: 0.8.11
 **/
gint64
pk_transaction_past_get_timestamp (PkTransactionPast *past)
{
	GDateTime *datetime;
	gint64 timestamp;

	g_return_val_if_fail (PK_IS_TRANSACTION_PAST (past), 0);

	datetime = pk_transaction_past_get_datetime (past);
	if (datetime == NULL)
		return 0;
	timestamp = g_date_time_to_unix (datetime);
	g_date_time_unref (datetime);
	return timestamp;
}

/**
 * pk_transaction_past_get_succeeded:
 * @past: a valid #PkTransactionPast instance
 *
 * Gets the past transaction succeeded value;
 *
 * Return value: The transaction data
 *
 * Since: 0.8.11
 **/
gboolean
pk_transaction_past_get_succeeded (PkTransactionPast *past)
{
	g_return_val_if_fail (PK_IS_TRANSACTION_PAST (past), FALSE);
	return past->priv->succeeded;
}

/**
 * pk_transaction_past_get_role:
 * @past: a valid #PkTransactionPast instance
 *
 * Gets the past transaction role;
 *
 * Return value: The transaction data
 *
 * Since: 0.8.11
 **/
PkRoleEnum
pk_transaction_past_get_role (PkTransactionPast *past)
{
	g_return_val_if_fail (PK_IS_TRANSACTION_PAST (past), PK_ROLE_ENUM_UNKNOWN);
	return past->priv->role;
}

/**
 * pk_transaction_past_get_duration:
 * @past: a valid #PkTransactionPast instance
 *
 * Gets the past transaction duration;
 *
 * Return value: The transaction duration in ms
 *
 * Since: 0.8.11
 **/
guint
pk_transaction_past_get_duration (PkTransactionPast *past)
{
	g_return_val_if_fail (PK_IS_TRANSACTION_PAST (past), 0);
	return past->priv->duration;
}

/**
 * pk_transaction_past_get_data:
 * @past: a valid #PkTransactionPast instance
 *
 * Gets the past transaction data;
 *
 * Return value: The transaction data
 *
 * Since: 0.8.11
 **/
const gchar *
pk_transaction_past_get_data (PkTransactionPast *past)
{
	g_return_val_if_fail (PK_IS_TRANSACTION_PAST (past), NULL);
	return past->priv->data;
}

/**
 * pk_transaction_past_get_uid:
 * @past: a valid #PkTransactionPast instance
 *
 * Gets the past transaction uid;
 *
 * Return value: The transaction data
 *
 * Since: 0.8.11
 **/
guint
pk_transaction_past_get_uid (PkTransactionPast *past)
{
	g_return_val_if_fail (PK_IS_TRANSACTION_PAST (past), 0);
	return past->priv->uid;
}

/**
 * pk_transaction_past_get_cmdline:
 * @past: a valid #PkTransactionPast instance
 *
 * Gets the past transaction cmdline value;
 *
 * Return value: The transaction data
 *
 * Since: 0.8.11
 **/
const gchar *
pk_transaction_past_get_cmdline (PkTransactionPast *past)
{
	g_return_val_if_fail (PK_IS_TRANSACTION_PAST (past), NULL);
	return past->priv->cmdline;
}

/*
 * pk_transaction_past_get_property:
 **/
static void
pk_transaction_past_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkTransactionPast *transaction_past = PK_TRANSACTION_PAST (object);
	PkTransactionPastPrivate *priv = transaction_past->priv;

	switch (prop_id) {
	case PROP_TID:
		g_value_set_string (value, priv->tid);
		break;
	case PROP_TIMESPEC:
		g_value_set_string (value, priv->timespec);
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
	case PROP_DATA:
		g_value_set_string (value, priv->data);
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
 * pk_transaction_past_set_property:
 **/
static void
pk_transaction_past_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkTransactionPast *transaction_past = PK_TRANSACTION_PAST (object);
	PkTransactionPastPrivate *priv = transaction_past->priv;

	switch (prop_id) {
	case PROP_TID:
		g_free (priv->tid);
		priv->tid = g_strdup (g_value_get_string (value));
		break;
	case PROP_TIMESPEC:
		g_free (priv->timespec);
		priv->timespec = g_strdup (g_value_get_string (value));
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
	case PROP_DATA:
		g_free (priv->data);
		priv->data = g_strdup (g_value_get_string (value));
		break;
	case PROP_UID:
		priv->uid = g_value_get_uint (value);
		break;
	case PROP_CMDLINE:
		g_free (priv->cmdline);
		priv->cmdline = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_transaction_past_class_init:
 **/
static void
pk_transaction_past_class_init (PkTransactionPastClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_transaction_past_finalize;
	object_class->get_property = pk_transaction_past_get_property;
	object_class->set_property = pk_transaction_past_set_property;

	/**
	 * PkTransactionPast:tid:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("tid", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_TID, pspec);

	/**
	 * PkTransactionPast:timespec:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("timespec", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_TIMESPEC, pspec);

	/**
	 * PkTransactionPast:succeeded:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_boolean ("succeeded", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SUCCEEDED, pspec);

	/**
	 * PkTransactionPast:role:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("role", NULL, NULL,
				   PK_TYPE_ROLE_ENUM, PK_ROLE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ROLE, pspec);

	/**
	 * PkTransactionPast:duration:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_uint ("duration", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DURATION, pspec);

	/**
	 * PkTransactionPast:data:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("data", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DATA, pspec);

	/**
	 * PkTransactionPast:uid:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_uint ("uid", NULL, NULL,
				   0, G_MAXUINT, G_MAXUINT,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UID, pspec);

	/**
	 * PkTransactionPast:cmdline:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("cmdline", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CMDLINE, pspec);

	g_type_class_add_private (klass, sizeof (PkTransactionPastPrivate));
}

/*
 * pk_transaction_past_init:
 **/
static void
pk_transaction_past_init (PkTransactionPast *transaction_past)
{
	transaction_past->priv = PK_TRANSACTION_PAST_GET_PRIVATE (transaction_past);
}

/*
 * pk_transaction_past_finalize:
 **/
static void
pk_transaction_past_finalize (GObject *object)
{
	PkTransactionPast *transaction_past = PK_TRANSACTION_PAST (object);
	PkTransactionPastPrivate *priv = transaction_past->priv;

	g_free (priv->tid);
	g_free (priv->timespec);
	g_free (priv->data);
	g_free (priv->cmdline);

	G_OBJECT_CLASS (pk_transaction_past_parent_class)->finalize (object);
}

/**
 * pk_transaction_past_new:
 *
 * Return value: a new #PkTransactionPast object.
 *
 * Since: 0.5.4
 **/
PkTransactionPast *
pk_transaction_past_new (void)
{
	PkTransactionPast *transaction_past;
	transaction_past = g_object_new (PK_TYPE_TRANSACTION_PAST, NULL);
	return PK_TRANSACTION_PAST (transaction_past);
}

