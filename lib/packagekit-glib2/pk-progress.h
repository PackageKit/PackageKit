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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_PROGRESS_H
#define __PK_PROGRESS_H

#include <glib-object.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-package.h>
#include <packagekit-glib2/pk-item-progress.h>

G_BEGIN_DECLS

#define PK_TYPE_PROGRESS		(pk_progress_get_type ())
#define PK_PROGRESS(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_PROGRESS, PkProgress))
#define PK_PROGRESS_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_PROGRESS, PkProgressClass))
#define PK_IS_PROGRESS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_PROGRESS))
#define PK_IS_PROGRESS_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_PROGRESS))
#define PK_PROGRESS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_PROGRESS, PkProgressClass))
#define PK_PROGRESS_TYPE_ERROR		(pk_progress_error_get_type ())

typedef struct _PkProgressPrivate	PkProgressPrivate;
typedef struct _PkProgress		PkProgress;
typedef struct _PkProgressClass		PkProgressClass;

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkProgress, g_object_unref)
#endif

struct _PkProgress
{
	 GObject		 parent;
	 PkProgressPrivate	*priv;
};

struct _PkProgressClass
{
	GObjectClass	parent_class;
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GType		 pk_progress_get_type		  	(void);
PkProgress	*pk_progress_new			(void);

/**
 * PkProgressType:
 * @PK_PROGRESS_TYPE_PACKAGE_ID: package id updated
 * @PK_PROGRESS_TYPE_TRANSACTION_ID: transaction ID updated
 * @PK_PROGRESS_TYPE_PERCENTAGE: percentage updated
 * @PK_PROGRESS_TYPE_ALLOW_CANCEL: allow cancel updated
 * @PK_PROGRESS_TYPE_STATUS: status updated
 * @PK_PROGRESS_TYPE_ROLE: role updated
 * @PK_PROGRESS_TYPE_CALLER_ACTIVE: called active updated
 * @PK_PROGRESS_TYPE_ELAPSED_TIME: elapsed time updated
 * @PK_PROGRESS_TYPE_REMAINING_TIME: remaining time updated
 * @PK_PROGRESS_TYPE_SPEED: speed updated
 * @PK_PROGRESS_TYPE_DOWNLOAD_SIZE_REMAINING: download size remaining updated
 * @PK_PROGRESS_TYPE_UID: uid updated
 * @PK_PROGRESS_TYPE_PACKAGE: package updated
 * @PK_PROGRESS_TYPE_ITEM_PROGRESS: item progress updated
 * @PK_PROGRESS_TYPE_TRANSACTION_FLAGS: transaction flags updated
 * @PK_PROGRESS_TYPE_SENDER: D-Bus name of sender updated (Since: 1.2.6)
 * @PK_PROGRESS_TYPE_INVALID:
 *
 * Flag to show which progress field has been updated.
 **/
typedef enum {
	PK_PROGRESS_TYPE_PACKAGE_ID,
	PK_PROGRESS_TYPE_TRANSACTION_ID,
	PK_PROGRESS_TYPE_PERCENTAGE,
	PK_PROGRESS_TYPE_ALLOW_CANCEL,
	PK_PROGRESS_TYPE_STATUS,
	PK_PROGRESS_TYPE_ROLE,
	PK_PROGRESS_TYPE_CALLER_ACTIVE,
	PK_PROGRESS_TYPE_ELAPSED_TIME,
	PK_PROGRESS_TYPE_REMAINING_TIME,
	PK_PROGRESS_TYPE_SPEED,
	PK_PROGRESS_TYPE_DOWNLOAD_SIZE_REMAINING,
	PK_PROGRESS_TYPE_UID,
	PK_PROGRESS_TYPE_PACKAGE,
	PK_PROGRESS_TYPE_ITEM_PROGRESS,
	PK_PROGRESS_TYPE_TRANSACTION_FLAGS,
	PK_PROGRESS_TYPE_INVALID,
	PK_PROGRESS_TYPE_SENDER
} PkProgressType;

/**
 * PkProgressCallback:
 * @progress: a #PkProgress
 * @type: the type of progress update this is
 * @user_data: User data supplied when the callback was registered.
 *
 * Function that is called when progress updates are given.
 */
typedef void	(*PkProgressCallback)			(PkProgress		*progress,
							 PkProgressType		 type,
                                                         gpointer		 user_data);

gboolean	 pk_progress_set_package_id		(PkProgress		*progress,
							 const gchar		*package_id);
const gchar	*pk_progress_get_package_id		(PkProgress		*progress);
gboolean	 pk_progress_set_item_progress		(PkProgress		*progress,
							 PkItemProgress		*item_progress);
PkItemProgress	*pk_progress_get_item_progress		(PkProgress		*progress);
gboolean	 pk_progress_set_transaction_id		(PkProgress		*progress,
							 const gchar		*transaction_id);
const gchar	*pk_progress_get_transaction_id		(PkProgress		*progress);
gboolean	 pk_progress_set_percentage		(PkProgress		*progress,
							 gint			 percentage);
gint		 pk_progress_get_percentage		(PkProgress		*progress);
gboolean	 pk_progress_set_status			(PkProgress		*progress,
							 PkStatusEnum		 status);
PkStatusEnum	 pk_progress_get_status			(PkProgress		*progress);
gboolean	 pk_progress_set_role			(PkProgress		*progress,
							 PkRoleEnum		 role);
PkRoleEnum	 pk_progress_get_role			(PkProgress		*progress);
gboolean	 pk_progress_set_allow_cancel		(PkProgress		*progress,
							 gboolean		 allow_cancel);
gboolean	 pk_progress_get_allow_cancel		(PkProgress		*progress);
gboolean	 pk_progress_set_caller_active		(PkProgress		*progress,
							 gboolean		 caller_active);
gboolean	 pk_progress_get_caller_active		(PkProgress		*progress);
gboolean	 pk_progress_set_elapsed_time		(PkProgress		*progress,
							 guint			 elapsed_time);
guint		 pk_progress_get_elapsed_time		(PkProgress		*progress);
gboolean	 pk_progress_set_remaining_time		(PkProgress		*progress,
							 guint			 remaining_time);
guint		 pk_progress_get_remaining_time		(PkProgress		*progress);
gboolean	 pk_progress_set_speed			(PkProgress		*progress,
							 guint			 speed);
guint		 pk_progress_get_speed			(PkProgress		*progress);
gboolean	 pk_progress_set_download_size_remaining(PkProgress		*progress,
							 guint64		 download_size_remaining);
guint64	 pk_progress_get_download_size_remaining(PkProgress		*progress);
gboolean	 pk_progress_set_transaction_flags	(PkProgress		*progress,
							 guint64		 transaction_flags);
guint64	 pk_progress_get_transaction_flags	(PkProgress		*progress);
gboolean	 pk_progress_set_uid			(PkProgress		*progress,
							 guint			 uid);
guint		 pk_progress_get_uid			(PkProgress		*progress);
gboolean	 pk_progress_set_sender			(PkProgress		*progress,
							 const gchar		*bus_name);
gchar		*pk_progress_get_sender			(PkProgress		*progress);
gboolean	 pk_progress_set_package		(PkProgress		*progress,
							 PkPackage		*package);
PkPackage	*pk_progress_get_package		(PkProgress		*progress);

G_END_DECLS

#endif /* __PK_PROGRESS_H */

