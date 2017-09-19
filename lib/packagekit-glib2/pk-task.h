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

#ifndef __PK_TASK_H
#define __PK_TASK_H

#include <glib-object.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-progress.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-client.h>

G_BEGIN_DECLS

#define PK_TYPE_TASK		(pk_task_get_type ())
#define PK_TASK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TASK, PkTask))
#define PK_TASK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TASK, PkTaskClass))
#define PK_IS_TASK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TASK))
#define PK_IS_TASK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TASK))
#define PK_TASK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TASK, PkTaskClass))
#define PK_TASK_TYPE_ERROR	(pk_task_error_get_type ())

typedef struct _PkTaskPrivate	PkTaskPrivate;
typedef struct _PkTask		PkTask;
typedef struct _PkTaskClass	PkTaskClass;

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkTask, g_object_unref)
#endif

struct _PkTask
{
	 PkClient		 parent;
	 PkTaskPrivate		*priv;
};

struct _PkTaskClass
{
	PkClientClass		parent_class;
	void	 (*untrusted_question)			(PkTask			*task,
							 guint			 request,
							 PkResults		*results);
	void	 (*key_question)			(PkTask			*task,
							 guint			 request,
							 PkResults		*results);
	void	 (*eula_question)			(PkTask			*task,
							 guint			 request,
							 PkResults		*results);
	void	 (*media_change_question)		(PkTask			*task,
							 guint			 request,
							 PkResults		*results);
	void	 (*simulate_question)			(PkTask			*task,
							 guint			 request,
							 PkResults		*results);
	void	 (*repair_question)			(PkTask			*task,
							 guint			 request,
							 PkResults		*results);
	/* padding for future expansion */
	void (*_pk_reserved1)	(void);
	void (*_pk_reserved2)	(void);
	void (*_pk_reserved3)	(void);
	void (*_pk_reserved4)	(void);
};

GType		 pk_task_get_type			(void);
PkTask		*pk_task_new				(void);

PkResults	*pk_task_generic_finish			(PkTask			*task,
							 GAsyncResult		*res,
							 GError			**error);

void		 pk_task_install_packages_async		(PkTask			*task,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_update_packages_async		(PkTask			*task,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_remove_packages_async		(PkTask			*task,
							 gchar			**package_ids,
							 gboolean		 allow_deps,
							 gboolean		 autoremove,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_install_files_async		(PkTask			*task,
							 gchar			**files,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_resolve_async			(PkTask			*task,
							 PkBitfield		 filters,
							 gchar			**packages,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_search_names_async		(PkTask			*task,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_search_details_async		(PkTask			*task,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_search_groups_async		(PkTask			*task,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_search_files_async		(PkTask			*task,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_get_details_async		(PkTask			*task,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_get_update_detail_async	(PkTask			*task,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_download_packages_async	(PkTask			*task,
							 gchar			**package_ids,
							 const gchar		*directory,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_get_updates_async		(PkTask			*task,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_depends_on_async		(PkTask			*task,
							 PkBitfield		 filters,
							 gchar			**package_ids,
							 gboolean		 recursive,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_get_packages_async		(PkTask			*task,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_required_by_async		(PkTask			*task,
							 PkBitfield		 filters,
							 gchar			**package_ids,
							 gboolean		 recursive,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_what_provides_async		(PkTask			*task,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_get_files_async		(PkTask			*task,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_get_categories_async		(PkTask			*task,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_refresh_cache_async		(PkTask			*task,
							 gboolean		 force,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_get_repo_list_async		(PkTask			*task,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_repo_enable_async		(PkTask			*task,
							 const gchar		*repo_id,
							 gboolean		 enabled,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_upgrade_system_async		(PkTask			*task,
							 const gchar		*distro_id,
							 PkUpgradeKindEnum	 upgrade_kind,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_repair_system_async		(PkTask			*task,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

gboolean	 pk_task_user_accepted			(PkTask			*task,
							 guint			 request);
gboolean	 pk_task_user_declined			(PkTask			*task,
							 guint			 request);

/* getters and setters */
void		 pk_task_set_simulate			(PkTask			*task,
							 gboolean		 simulate);
gboolean	 pk_task_get_simulate			(PkTask			*task);
void		 pk_task_set_only_download		(PkTask			*task,
							 gboolean		 only_download);
gboolean	 pk_task_get_only_download		(PkTask			*task);
void		 pk_task_set_allow_downgrade	(PkTask			*task,
							 gboolean		 allow_downgrade);
gboolean	 pk_task_get_allow_downgrade	(PkTask			*task);
void		 pk_task_set_allow_reinstall	(PkTask			*task,
							 gboolean		 allow_reinstall);
gboolean	 pk_task_get_allow_reinstall	(PkTask			*task);
void		 pk_task_set_only_trusted		(PkTask			*task,
							 gboolean		 only_trusted);
gboolean	 pk_task_get_only_trusted		(PkTask			*task);

G_END_DECLS

#endif /* __PK_TASK_H */

