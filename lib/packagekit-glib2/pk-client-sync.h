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

#ifndef __PK_CLIENT_SYNC_H
#define __PK_CLIENT_SYNC_H

#include <glib.h>
#include <packagekit-glib2/packagekit.h>

PkResults	*pk_client_resolve_sync			(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**packages,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_search_name_sync		(PkClient		*client,
							 PkBitfield		 filters,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_search_details_sync		(PkClient		*client,
							 PkBitfield		 filters,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_search_group_sync		(PkClient		*client,
							 PkBitfield		 filters,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_search_file_sync		(PkClient		*client,
							 PkBitfield		 filters,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_details_sync		(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_update_detail_sync	(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_download_packages_sync	(PkClient		*client,
							 gchar			**package_ids,
							 const gchar		*directory,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_updates_sync		(PkClient		*client,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_old_transactions_sync	(PkClient		*client,
							 guint			 number,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_update_system_sync		(PkClient		*client,
							 gboolean		 only_trusted,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_depends_sync		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**package_ids,
							 gboolean		 recursive,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_packages_sync		(PkClient		*client,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_requires_sync		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**package_ids,
							 gboolean		 recursive,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_what_provides_sync		(PkClient		*client,
							 PkBitfield		 filters,
							 PkProvidesEnum		 provides,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_distro_upgrades_sync	(PkClient		*client,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_files_sync		(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_categories_sync		(PkClient		*client,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_remove_packages_sync	(PkClient		*client,
							 gchar			**package_ids,
							 gboolean		 allow_deps,
							 gboolean		 autoremove,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_refresh_cache_sync		(PkClient		*client,
							 gboolean		 force,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_install_packages_sync	(PkClient		*client,
							 gboolean		 only_trusted,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_install_signature_sync	(PkClient		*client,
							 PkSigTypeEnum		 type,
							 const gchar		*key_id,
							 const gchar		*package_id,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_update_packages_sync		(PkClient		*client,
							 gboolean		 only_trusted,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_install_files_sync		(PkClient		*client,
							 gboolean		 only_trusted,
							 gchar			**files,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_accept_eula_sync		(PkClient		*client,
							 const gchar		*eula_id,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_rollback_sync		(PkClient		*client,
							 const gchar		*transaction_id,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_repo_list_sync		(PkClient		*client,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_repo_enable_sync		(PkClient		*client,
							 const gchar		*repo_id,
							 gboolean		 enabled,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_repo_set_data_sync		(PkClient		*client,
							 const gchar		*repo_id,
							 const gchar		*parameter,
							 const gchar		*value,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_simulate_install_files_sync	(PkClient		*client,
							 gchar			**files,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_simulate_install_packages_sync (PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_simulate_remove_packages_sync (PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_simulate_update_packages_sync (PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_adopt_sync 			(PkClient		*client,
							 const gchar		*transaction_id,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

#endif /* __PK_CLIENT_SYNC_H */



