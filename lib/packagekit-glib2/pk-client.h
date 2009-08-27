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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

/**
 * SECTION:pk-client
 * @short_description: An abstract client GObject
 */

#ifndef __PK_CLIENT_H
#define __PK_CLIENT_H

#include <glib-object.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-bitfield.h>

G_BEGIN_DECLS

#define PK_TYPE_CLIENT		(pk_client_get_type ())
#define PK_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CLIENT, PkClient))
#define PK_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CLIENT, PkClientClass))
#define PK_IS_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CLIENT))
#define PK_IS_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CLIENT))
#define PK_CLIENT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CLIENT, PkClientClass))
#define PK_CLIENT_ERROR		(pk_client_error_quark ())
#define PK_CLIENT_TYPE_ERROR	(pk_client_error_get_type ())

typedef struct _PkClientPrivate	PkClientPrivate;
typedef struct _PkClient		PkClient;
typedef struct _PkClientClass		PkClientClass;

struct _PkClient
{
	 GObject		 parent;
	 PkClientPrivate	*priv;
};

struct _PkClientClass
{
	GObjectClass	parent_class;

	/* signals */
	void		(* changed)			(PkClient	*client);
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GQuark		 pk_client_error_quark			(void);
GType		 pk_client_get_type		  	(void);
PkClient	*pk_client_new				(void);

typedef void	(*PkClientProgressCallback)		(PkClient		*client,
							 gint			 percentage,
                                                         gpointer		 user_data);
typedef void	(*PkClientStatusCallback)		(PkClient		*client,
							 PkStatusEnum		 status,
                                                         gpointer		 user_data);
typedef void	(*PkClientPackageCallback)		(PkClient		*client,
							 const gchar		*package_id,
                                                         gpointer		 user_data);

/* get transaction results */
PkResults	*pk_client_generic_finish		(PkClient		*client,
							 GAsyncResult		*res,
							 GError			**error);

void		 pk_client_resolve_async		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**packages,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_search_name_async		(PkClient		*client,
							 PkBitfield		 filters,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_search_details_async		(PkClient		*client,
							 PkBitfield		 filters,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_search_group_async		(PkClient		*client,
							 PkBitfield		 filters,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_search_file_async		(PkClient		*client,
							 PkBitfield		 filters,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_details_async		(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_update_detail_async	(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_download_packages		(PkClient		*client,
							 gchar			**package_ids,
							 const gchar		*directory,
							 GCancellable		*cancellable,
							 PkClientPackageCallback callback_package,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_updates			(PkClient		*client,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_update_system		(PkClient		*client,
							 gboolean		 only_trusted,
							 GCancellable		*cancellable,
							 PkClientPackageCallback callback_package,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_depends			(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**package_ids,
							 gboolean		 recursive,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_packages			(PkClient		*client,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_update_detail		(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_requires			(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**package_ids,
							 gboolean		 recursive,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_what_provides		(PkClient		*client,
							 PkBitfield		 filters,
							 PkProvidesEnum		 provides,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_details			(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_distro_upgrades		(PkClient		*client,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_files			(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_categories		(PkClient		*client,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_remove_packages		(PkClient		*client,
							 gchar			**package_ids,
							 gboolean		 allow_deps,
							 gboolean		 autoremove,
							 GCancellable		*cancellable,
							 PkClientPackageCallback callback_package,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_refresh_cache		(PkClient		*client,
							 gboolean		 force,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_install_packages		(PkClient		*client,
							 gboolean		 only_trusted,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkClientPackageCallback callback_package,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_install_signature		(PkClient		*client,
							 PkSigTypeEnum		 type,
							 const gchar		*key_id,
							 const gchar		*package_id,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_update_packages		(PkClient		*client,
							 gboolean		 only_trusted,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkClientPackageCallback callback_package,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_install_files		(PkClient		*client,
							 gboolean		 only_trusted,
							 gchar			**files_rel,
							 GCancellable		*cancellable,
							 PkClientPackageCallback callback_package,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_accept_eula			(PkClient		*client,
							 const gchar		*eula_id,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_repo_list		(PkClient		*client,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_repo_enable			(PkClient		*client,
							 const gchar		*repo_id,
							 gboolean		 enabled,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_repo_set_data		(PkClient		*client,
							 const gchar		*repo_id,
							 const gchar		*parameter,
							 const gchar		*value,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_simulate_install_files	(PkClient		*client,
							 gchar			**files_rel,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_simulate_install_packages	(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_simulate_remove_packages	(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_simulate_update_packages	(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkClientProgressCallback callback_progress,
							 PkClientStatusCallback	 callback_status,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

G_END_DECLS

#endif /* __PK_CLIENT_H */

