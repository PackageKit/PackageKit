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
	PROP_PACKAGE,
	PROP_ITEM_PROGRESS,
	PROP_LAST
};

G_DEFINE_TYPE (PkProgress, pk_progress, G_TYPE_OBJECT)

/**
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
 * pk_progress_set_item_progress:
 * @progress: a valid #PkProgress instance
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
 * pk_progress_set_transaction_id:
 * @progress: a valid #PkProgress instance
 *
 * Since: 0.5.3
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
 * pk_progress_set_percentage:
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
 * pk_progress_set_status:
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
 * pk_progress_set_role:
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
 * pk_progress_set_allow_cancel:
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
 * pk_progress_set_caller_active:
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
 * pk_progress_set_elapsed_time:
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
 * pk_progress_set_remaining_time:
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
 * pk_progress_set_speed:
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
 * pk_progress_set_download_size_remaining:
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
 * pk_progress_set_transaction_flags:
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
 * pk_progress_set_uid:
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
 * pk_progress_set_package:
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

/**
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
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_int ("percentage", NULL, NULL,
				  -1, G_MAXINT, -1,
				  G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PERCENTAGE, pspec);

	/**
	 * PkProgress:allow-cancel:
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
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("status", NULL, NULL,
				   0, PK_STATUS_ENUM_LAST, PK_STATUS_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_STATUS, pspec);

	/**
	 * PkProgress:role:
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
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_boolean ("caller-active", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CALLER_ACTIVE, pspec);

	/**
	 * PkProgress:elapsed-time:
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
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("remaining-time", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_REMAINING_TIME, pspec);

	/**
	 * PkProgress:speed:
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
	 * Since: 0.8.0
	 */
	pspec = g_param_spec_uint ("download-size-remaining", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DOWNLOAD_SIZE_REMAINING, pspec);

	/**
	 * PkProgress:transaction-flags:
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
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("uid", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UID, pspec);

	/**
	 * PkProgress:package:
	 *
	 * Since: 0.5.3
	 */
	pspec = g_param_spec_object ("package", NULL, NULL,
				     PK_TYPE_PACKAGE,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PACKAGE, pspec);

	/**
	 * PkProgress:item-progress-id:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_object ("item-progress", NULL, NULL,
				     PK_TYPE_ITEM_PROGRESS,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ITEM_PROGRESS, pspec);

	g_type_class_add_private (klass, sizeof (PkProgressPrivate));
}

/**
 * pk_progress_init:
 **/
static void
pk_progress_init (PkProgress *progress)
{
	progress->priv = PK_PROGRESS_GET_PRIVATE (progress);
}

/**
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
 * PkProgress is a nice GObject wrapper for PackageKit and makes writing
 * frontends easy.
 *
 * Return value: A new %PkProgress instance
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
