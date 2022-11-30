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
 * SECTION:pk-progress
 * @short_description: Transaction progress information
 *
 * This GObject is available to clients to be able to query details about
 * the transaction. All of the details on this object are stored as properties.
 */

#include "config.h"

#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-progress.h>

static void     pk_progress_finalize	(GObject     *object);

#define PK_PROGRESS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PROGRESS, PkProgressPrivate))

/**
 * PkProgressPrivate:
 *
 * Private #PkProgress data
 **/
struct _PkProgressPrivate
{
	gchar				*package_id;
	gchar				*transaction_id;
	gint				 percentage;
	gboolean			 allow_cancel;
	PkRoleEnum			 role;
	PkStatusEnum			 status;
	gboolean			 caller_active;
	guint				 elapsed_time;
	guint				 remaining_time;
	guint				 speed;
	guint64				 download_size_remaining;
	guint64				 transaction_flags;
	guint				 uid;
	gchar				*sender;
	PkItemProgress			*item_progress;
	PkPackage			*package;
};

enum {
	PROP_0,
	PROP_PACKAGE_ID,
	PROP_TRANSACTION_ID,
	PROP_PERCENTAGE,
	PROP_ALLOW_CANCEL,
	PROP_ROLE,
	PROP_STATUS,
	PROP_CALLER_ACTIVE,
	PROP_ELAPSED_TIME,
	PROP_REMAINING_TIME,
	PROP_SPEED,
	PROP_DOWNLOAD_SIZE_REMAINING,
	PROP_TRANSACTION_FLAGS,
	PROP_UID,
	PROP_SENDER,
	PROP_PACKAGE,
	PROP_ITEM_PROGRESS,
	PROP_LAST
};

G_DEFINE_TYPE (PkProgress, pk_progress, G_TYPE_OBJECT)

/*
 * pk_progress_get_property:
 **/
static void
pk_progress_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkProgress *progress = PK_PROGRESS (object);

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_value_set_string (value, progress->priv->package_id);
		break;
	case PROP_TRANSACTION_ID:
		g_value_set_string (value, progress->priv->transaction_id);
		break;
	case PROP_PERCENTAGE:
		g_value_set_int (value, progress->priv->percentage);
		break;
	case PROP_ITEM_PROGRESS:
		g_value_set_object (value, progress->priv->item_progress);
		break;
	case PROP_ALLOW_CANCEL:
		g_value_set_boolean (value, progress->priv->allow_cancel);
		break;
	case PROP_STATUS:
		g_value_set_uint (value, progress->priv->status);
		break;
	case PROP_ROLE:
		g_value_set_uint (value, progress->priv->role);
		break;
	case PROP_CALLER_ACTIVE:
		g_value_set_boolean (value, progress->priv->caller_active);
		break;
	case PROP_ELAPSED_TIME:
		g_value_set_uint (value, progress->priv->elapsed_time);
		break;
	case PROP_REMAINING_TIME:
		g_value_set_uint (value, progress->priv->remaining_time);
		break;
	case PROP_SPEED:
		g_value_set_uint (value, progress->priv->speed);
		break;
	case PROP_DOWNLOAD_SIZE_REMAINING:
		g_value_set_uint64 (value, progress->priv->download_size_remaining);
		break;
	case PROP_TRANSACTION_FLAGS:
		g_value_set_uint64 (value, progress->priv->transaction_flags);
		break;
	case PROP_UID:
		g_value_set_uint (value, progress->priv->uid);
		break;
	case PROP_SENDER:
		g_value_set_string (value, progress->priv->sender);
		break;
	case PROP_PACKAGE:
		g_value_set_object (value, progress->priv->package);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_progress_set_package_id:
 * @progress: a valid #PkProgress instance
 * @package_id: a PackageID
 *
 * Set the package ID this transaction is acting on.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.5.2
 **/
gboolean
pk_progress_set_package_id (PkProgress *progress, const gchar *package_id)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (g_strcmp0 (progress->priv->package_id, package_id) == 0)
		return FALSE;

	/* valid? */
	if (!pk_package_id_check (package_id)) {
		g_warning ("invalid package_id %s", package_id);
		return FALSE;
	}

	/* new value */
	g_free (progress->priv->package_id);
	progress->priv->package_id = g_strdup (package_id);
	g_object_notify (G_OBJECT(progress), "package-id");

	return TRUE;
}

/**
 * pk_progress_get_package_id:
 * @progress: a valid #PkProgress instance
 *
 * Get the package ID this transaction is acting on.
 *
 * Return value: a PackageID
 *
 * Since: 1.0.12
 **/
const gchar *
pk_progress_get_package_id (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), NULL);
	return progress->priv->package_id;
}

/**
 * pk_progress_set_item_progress:
 * @progress: a valid #PkProgress instance
 * @item_progress: a #PkItemProgress
 *
 * Set the item progress associated with this transaction.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.8.1
 **/
gboolean
pk_progress_set_item_progress (PkProgress *progress,
			       PkItemProgress *item_progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (progress->priv->item_progress == item_progress)
		return FALSE;

	/* new value */
	if (progress->priv->item_progress != NULL)
		g_object_unref (progress->priv->item_progress);
	progress->priv->item_progress = g_object_ref (item_progress);
	g_object_notify (G_OBJECT(progress), "item-progress");
	return TRUE;
}

/**
 * pk_progress_get_item_progress:
 * @progress: a valid #PkProgress instance
 *
 * Get the item progress associated with this transaction.
 *
 * Return value: (transfer none): a #PkItemProgress
 *
 * Since: 1.0.12
 **/
PkItemProgress *
pk_progress_get_item_progress (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), NULL);
	return progress->priv->item_progress;
}

/**
 * pk_progress_set_transaction_id:
 * @progress: a valid #PkProgress instance
 * @transaction_id: a transaction ID.
 *
 * Set the ID used by this transaction.
 *
 * Since: 0.5.3
 *
 * Return value: %TRUE if value changed.
 **/
gboolean
pk_progress_set_transaction_id (PkProgress *progress, const gchar *transaction_id)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (g_strcmp0 (progress->priv->transaction_id, transaction_id) == 0)
		return FALSE;

	/* new value */
	g_free (progress->priv->transaction_id);
	progress->priv->transaction_id = g_strdup (transaction_id);
	g_object_notify (G_OBJECT(progress), "transaction-id");

	return TRUE;
}

/**
 * pk_progress_get_transaction_id:
 * @progress: a valid #PkProgress instance
 *
 * Get the ID used by this transaction.
 *
 * Return value: a transaction ID.
 *
 * Since: 1.0.12
 **/
const gchar *
pk_progress_get_transaction_id (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), NULL);
	return progress->priv->transaction_id;
}

/**
 * pk_progress_set_percentage:
 * @progress: a valid #PkProgress instance
 * @percentage: a percentage value (0-100)
 *
 * Set the percentage complete of this transaction.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.5.2
 **/
gboolean
pk_progress_set_percentage (PkProgress *progress, gint percentage)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (progress->priv->percentage == percentage)
		return FALSE;

	/* new value */
	progress->priv->percentage = percentage;
	g_object_notify (G_OBJECT(progress), "percentage");

	return TRUE;
}

/**
 * pk_progress_get_percentage:
 * @progress: a valid #PkProgress instance
 *
 * Get the percentage complete.
 *
 * Return value: a percentage (0-100)
 *
 * Since: 1.0.12
 **/
gint
pk_progress_get_percentage (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), -1);
	return progress->priv->percentage;
}

/**
 * pk_progress_set_status:
 * @progress: a valid #PkProgress instance
 * @status: a #PkStatusEnum
 *
 * Set the status of this transaction.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.5.2
 **/
gboolean
pk_progress_set_status (PkProgress *progress, PkStatusEnum status)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (progress->priv->status == status)
		return FALSE;

	/* new value */
	progress->priv->status = status;
	g_object_notify (G_OBJECT(progress), "status");

	return TRUE;
}

/**
 * pk_progress_get_status:
 * @progress: a valid #PkProgress instance
 *
 * Get the status of this transaction.
 *
 * Return value: a status string
 *
 * Since: 1.0.12
 **/
PkStatusEnum
pk_progress_get_status (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), PK_STATUS_ENUM_UNKNOWN);
	return progress->priv->status;
}

/**
 * pk_progress_set_role:
 * @progress: a valid #PkProgress instance
 * @role: a #PkRoleEnum
 *
 * Set the role of this transaction.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.5.2
 **/
gboolean
pk_progress_set_role (PkProgress *progress, PkRoleEnum role)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* ignore unknown as we don't want to replace a valid value */
	if (role == PK_ROLE_ENUM_UNKNOWN)
		return FALSE;

	/* the same as before? */
	if (progress->priv->role == role)
		return FALSE;

	/* new value */
	progress->priv->role = role;
	g_debug ("role now %s", pk_role_enum_to_string (role));
	g_object_notify (G_OBJECT(progress), "role");

	return TRUE;
}

/**
 * pk_progress_get_role:
 * @progress: a valid #PkProgress instance
 *
 * Get the role of this transaction.
 *
 * Return value: a #PkRoleEnum
 *
 * Since: 1.0.12
 **/
PkRoleEnum
pk_progress_get_role (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), PK_ROLE_ENUM_UNKNOWN);
	return progress->priv->role;
}

/**
 * pk_progress_set_allow_cancel:
 * @progress: a valid #PkProgress instance
 * @allow_cancel: %TRUE if this transaction can be cancelled.
 *
 * Set if this transaction can be cancelled.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.5.2
 **/
gboolean
pk_progress_set_allow_cancel (PkProgress *progress, gboolean allow_cancel)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (progress->priv->allow_cancel == allow_cancel)
		return FALSE;

	/* new value */
	progress->priv->allow_cancel = allow_cancel;
	g_object_notify (G_OBJECT(progress), "allow-cancel");

	return TRUE;
}

/**
 * pk_progress_get_allow_cancel:
 * @progress: a valid #PkProgress instance
 *
 * Get if this transaction can be cancelled.
 *
 * Return value: %TRUE if progress can be cancelled.
 *
 * Since: 1.0.12
 **/
gboolean
pk_progress_get_allow_cancel (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);
	return progress->priv->allow_cancel;
}

/**
 * pk_progress_set_caller_active:
 * @progress: a valid #PkProgress instance
 * @caller_active: %TRUE if the transaction caller is still connected.
 *
 * Set if the transaction caller is connected.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.5.2
 **/
gboolean
pk_progress_set_caller_active (PkProgress *progress, gboolean caller_active)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (progress->priv->caller_active == caller_active)
		return FALSE;

	/* new value */
	progress->priv->caller_active = caller_active;
	g_object_notify (G_OBJECT(progress), "caller-active");

	return TRUE;
}

/**
 * pk_progress_get_caller_active:
 * @progress: a valid #PkProgress instance
 *
 * Get if the transaction caller is connected.
 *
 * Return value: %TRUE if the transaction caller is still connected.
 *
 * Since: 1.0.12
 **/
gboolean
pk_progress_get_caller_active (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);
	return progress->priv->caller_active;
}

/**
 * pk_progress_set_elapsed_time:
 * @progress: a valid #PkProgress instance
 * @elapsed_time: time in seconds
 *
 * Set the amount of time the transaction has taken.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.5.2
 **/
gboolean
pk_progress_set_elapsed_time (PkProgress *progress, guint elapsed_time)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (progress->priv->elapsed_time == elapsed_time)
		return FALSE;

	/* new value */
	progress->priv->elapsed_time = elapsed_time;
	g_object_notify (G_OBJECT(progress), "elapsed-time");

	return TRUE;
}

/**
 * pk_progress_get_elapsed_time:
 * @progress: a valid #PkProgress instance
 *
 * Get the amount of time the transaction has taken.
 *
 * Return value: time in seconds
 *
 * Since: 1.0.12
 **/
guint
pk_progress_get_elapsed_time (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), 0);
	return progress->priv->elapsed_time;
}

/**
 * pk_progress_set_remaining_time:
 * @progress: a valid #PkProgress instance
 * @remaining_time: time in seconds or 0 if unknown.
 *
 * Set the amount of time the transaction will take to complete.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.5.2
 **/
gboolean
pk_progress_set_remaining_time (PkProgress *progress, guint remaining_time)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (progress->priv->remaining_time == remaining_time)
		return FALSE;

	/* new value */
	progress->priv->remaining_time = remaining_time;
	g_object_notify (G_OBJECT(progress), "remaining-time");

	return TRUE;
}

/**
 * pk_progress_get_remaining_time:
 * @progress: a valid #PkProgress instance
 *
 * Get the amount of time the transaction will take to complete.
 *
 * Return value: time in seconds or 0 if unknown.
 *
 * Since: 1.0.12
 **/
guint
pk_progress_get_remaining_time (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), 0);
	return progress->priv->remaining_time;
}

/**
 * pk_progress_set_speed:
 * @progress: a valid #PkProgress instance
 * @speed: speed in bits per second or 0 if unknown
 *
 * Set the speed of this transaction.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.5.2
 **/
gboolean
pk_progress_set_speed (PkProgress *progress, guint speed)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (progress->priv->speed == speed)
		return FALSE;

	/* new value */
	progress->priv->speed = speed;
	g_object_notify (G_OBJECT(progress), "speed");

	return TRUE;
}

/**
 * pk_progress_get_speed:
 * @progress: a valid #PkProgress instance
 *
 * Get the speed of this transaction.
 *
 * Return value: speed in bits per scond or 0 if unknown
 *
 * Since: 1.0.12
 **/
guint
pk_progress_get_speed (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), 0);
	return progress->priv->speed;
}

/**
 * pk_progress_set_download_size_remaining:
 * @progress: a valid #PkProgress instance
 * @download_size_remaining: number of bytes remaining to download.
 *
 * Set the number of bytes remaining to download.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.8.0
 **/
gboolean
pk_progress_set_download_size_remaining (PkProgress *progress, guint64 download_size_remaining)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (progress->priv->download_size_remaining == download_size_remaining)
		return FALSE;

	/* new value */
	progress->priv->download_size_remaining = download_size_remaining;
	g_object_notify (G_OBJECT(progress), "download-size-remaining");

	return TRUE;
}

/**
 * pk_progress_get_download_size_remaining:
 * @progress: a valid #PkProgress instance
 *
 * Get the number of bytes remaining to download.
 *
 * Return value: number of bytes remaining to download.
 *
 * Since: 1.0.12
 **/
guint64
pk_progress_get_download_size_remaining (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), 0);
	return progress->priv->download_size_remaining;
}

/**
 * pk_progress_set_transaction_flags:
 * @progress: a valid #PkProgress instance
 * @transaction_flags: a #PkBitfield containing #PkTransactionFlagEnum values.
 *
 * Set the flags associated with this transaction.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.8.8
 **/
gboolean
pk_progress_set_transaction_flags (PkProgress *progress, guint64 transaction_flags)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (progress->priv->transaction_flags == transaction_flags)
		return FALSE;

	/* new value */
	progress->priv->transaction_flags = transaction_flags;
	g_object_notify (G_OBJECT(progress), "transaction-flags");

	return TRUE;
}

/**
 * pk_progress_get_transaction_flags:
 * @progress: a valid #PkProgress instance
 *
 * Get the flags associated with this transaction.
 *
 * Return value: a #PkBitfield containing #PkTransactionFlagEnum values.
 *
 * Since: 1.0.12
 **/
guint64
pk_progress_get_transaction_flags (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), 0);
	return progress->priv->transaction_flags;
}

/**
 * pk_progress_set_uid:
 * @progress: a valid #PkProgress instance
 * @uid: a UID
 *
 * Set the UID that started this transaction.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.5.2
 **/
gboolean
pk_progress_set_uid (PkProgress *progress, guint uid)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (progress->priv->uid == uid)
		return FALSE;

	/* new value */
	progress->priv->uid = uid;
	g_object_notify (G_OBJECT(progress), "uid");

	return TRUE;
}

/**
 * pk_progress_get_uid:
 * @progress: a valid #PkProgress instance
 *
 * Get the UID that started this transaction.
 *
 * Return value: an UID
 *
 * Since: 1.0.12
 **/
guint
pk_progress_get_uid (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), 0);
	return progress->priv->uid;
}

/**
 * pk_progress_set_sender:
 * @progress: a valid #PkProgress instance
 * @bus_name: a D-Bus name
 *
 * Set the D-Bus name of the client that started this transaction.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 1.2.6
 **/
gboolean
pk_progress_set_sender (PkProgress *progress, const gchar *bus_name)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (g_strcmp0 (progress->priv->sender, bus_name) == 0)
		return FALSE;

	/* new value */
	g_free (progress->priv->sender);
	progress->priv->sender = g_strdup (bus_name);
	g_object_notify (G_OBJECT(progress), "sender");

	return TRUE;
}

/**
 * pk_progress_get_sender:
 * @progress: a valid #PkProgress instance
 *
 * Get the D-Bus name of the client that started this transaction.
 *
 * Return value: a D-Bus name
 *
 * Since: 1.2.6
 **/
gchar*
pk_progress_get_sender (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), NULL);
	return progress->priv->sender;
}

/**
 * pk_progress_set_package:
 * @progress: a valid #PkProgress instance
 * @package: a #PkPackage
 *
 * Set the package this transaction is acting on.
 *
 * Return value: %TRUE if value changed.
 *
 * Since: 0.5.2
 **/
gboolean
pk_progress_set_package (PkProgress *progress, PkPackage *package)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	/* the same as before? */
	if (progress->priv->package == package)
		return FALSE;

	/* new value */
	if (progress->priv->package != NULL)
		g_object_unref (progress->priv->package);
	progress->priv->package = g_object_ref (package);
	g_object_notify (G_OBJECT(progress), "package");

	return TRUE;
}

/**
 * pk_progress_get_package:
 * @progress: a valid #PkProgress instance
 *
 * Get the package this transaction is acting on.
 *
 * Return value: (transfer none): a #PkPackage
 *
 * Since: 1.0.12
 **/
PkPackage *
pk_progress_get_package (PkProgress *progress)
{
	g_return_val_if_fail (PK_IS_PROGRESS (progress), NULL);
	return progress->priv->package;
}

/*
 * pk_progress_set_property:
 **/
static void
pk_progress_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkProgress *progress = PK_PROGRESS (object);

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		pk_progress_set_package_id (progress, g_value_get_string (value));
		break;
	case PROP_TRANSACTION_ID:
		pk_progress_set_transaction_id (progress, g_value_get_string (value));
		break;
	case PROP_PERCENTAGE:
		pk_progress_set_percentage (progress, g_value_get_int (value));
		break;
	case PROP_ALLOW_CANCEL:
		pk_progress_set_allow_cancel (progress, g_value_get_boolean (value));
		break;
	case PROP_STATUS:
		pk_progress_set_status (progress, g_value_get_uint (value));
		break;
	case PROP_ROLE:
		pk_progress_set_role (progress, g_value_get_uint (value));
		break;
	case PROP_CALLER_ACTIVE:
		pk_progress_set_caller_active (progress, g_value_get_boolean (value));
		break;
	case PROP_ELAPSED_TIME:
		pk_progress_set_elapsed_time (progress, g_value_get_uint (value));
		break;
	case PROP_REMAINING_TIME:
		pk_progress_set_remaining_time (progress, g_value_get_uint (value));
		break;
	case PROP_SPEED:
		pk_progress_set_speed (progress, g_value_get_uint (value));
		break;
	case PROP_UID:
		pk_progress_set_uid (progress, g_value_get_uint (value));
		break;
	case PROP_PACKAGE:
		pk_progress_set_package (progress, g_value_get_object (value));
		break;
	case PROP_ITEM_PROGRESS:
		pk_progress_set_item_progress (progress, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_progress_class_init:
 **/
static void
pk_progress_class_init (PkProgressClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = pk_progress_get_property;
	object_class->set_property = pk_progress_set_property;
	object_class->finalize = pk_progress_finalize;

	/**
	 * PkProgress:package-id:
         *
	 * Package ID this transaction is acting on.
         *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_string ("package-id", NULL,
				     "The full package_id, e.g. 'gnome-power-manager;0.1.2;i386;fedora'",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PACKAGE_ID, pspec);

	/**
	 * PkProgress:transaction-id:
	 *
         * ID used by this transaction.
         *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_string ("transaction-id", NULL,
				     "The transaction_id, e.g. '/892_deabbbdb_data'",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_TRANSACTION_ID, pspec);

	/**
	 * PkProgress:percentage:
         *
         * Percentage complete of this transaction.
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_int ("percentage", NULL, NULL,
				  -1, G_MAXINT, -1,
				  G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PERCENTAGE, pspec);

	/**
	 * PkProgress:allow-cancel:
         *
         * %TRUE if this transaction can be cancelled.
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_boolean ("allow-cancel", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ALLOW_CANCEL, pspec);

	/**
	 * PkProgress:status:
         *
         * Status of this transaction.
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("status", NULL, NULL,
				   0, PK_STATUS_ENUM_LAST, PK_STATUS_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_STATUS, pspec);

	/**
	 * PkProgress:role:
         *
         * Role of this transaction.
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("role", NULL, NULL,
				   0, PK_ROLE_ENUM_LAST, PK_ROLE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ROLE, pspec);

	/**
	 * PkProgress:caller-active:
	 *
         * %TRUE if the transaction caller is still connected.
         *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_boolean ("caller-active", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CALLER_ACTIVE, pspec);

	/**
	 * PkProgress:elapsed-time:
	 *
         * Amount of time the transaction has taken in seconds.
         *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("elapsed-time", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ELAPSED_TIME, pspec);

	/**
	 * PkProgress:remaining-time:
	 *
         * Amount of time the transaction will take to complete in seconds or 0 if unknown.
         *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("remaining-time", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_REMAINING_TIME, pspec);

	/**
	 * PkProgress:speed:
	 *
         * Transaction speed in bits per second or 0 if unknown.
         *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("speed", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SPEED, pspec);

	/**
	 * PkProgress:download-size-remaining:
	 *
         * Number of bytes remaining to download.
         *
	 * Since: 0.8.0
	 */
	pspec = g_param_spec_uint64 ("download-size-remaining", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DOWNLOAD_SIZE_REMAINING, pspec);

	/**
	 * PkProgress:transaction-flags:
	 *
         * A #PkBitfield containing #PkTransactionFlagEnum associated with this transaction.
         *
	 * Since: 0.8.8
	 */
	pspec = g_param_spec_uint64 ("transaction-flags", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_TRANSACTION_FLAGS, pspec);

	/**
	 * PkProgress:uid:
	 *
         * The UID that started this transaction.
         *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("uid", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UID, pspec);

	/**
	 * PkProgress:sender:
	 *
         * The D-Bus name of the client that started this transaction.
         *
	 * Since: 1.2.6
	 */
	pspec = g_param_spec_string ("sender", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SENDER, pspec);

	/**
	 * PkProgress:package:
	 *
         * The package this transaction is acting on.
         *
	 * Since: 0.5.3
	 */
	pspec = g_param_spec_object ("package", NULL, NULL,
				     PK_TYPE_PACKAGE,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PACKAGE, pspec);

	/**
	 * PkProgress:item-progress:
         *
         * Item progress associated with this transaction.
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_object ("item-progress", NULL, NULL,
				     PK_TYPE_ITEM_PROGRESS,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ITEM_PROGRESS, pspec);

	g_type_class_add_private (klass, sizeof (PkProgressPrivate));
}

/*
 * pk_progress_init:
 **/
static void
pk_progress_init (PkProgress *progress)
{
	progress->priv = PK_PROGRESS_GET_PRIVATE (progress);
}

/*
 * pk_progress_finalize:
 **/
static void
pk_progress_finalize (GObject *object)
{
	PkProgress *progress = PK_PROGRESS (object);

	if (progress->priv->package != NULL)
		g_object_unref (progress->priv->package);
	if (progress->priv->item_progress != NULL)
		g_object_unref (progress->priv->item_progress);

	g_free (progress->priv->package_id);
	g_free (progress->priv->transaction_id);

	G_OBJECT_CLASS (pk_progress_parent_class)->finalize (object);
}

/**
 * pk_progress_new:
 *
 * #PkProgress is a nice GObject wrapper for PackageKit and makes writing
 * frontends easy.
 *
 * Return value: A new #PkProgress instance
 *
 * Since: 0.5.2
 **/
PkProgress *
pk_progress_new (void)
{
	PkProgress *progress;
	progress = g_object_new (PK_TYPE_PROGRESS, NULL);
	return PK_PROGRESS (progress);
}
