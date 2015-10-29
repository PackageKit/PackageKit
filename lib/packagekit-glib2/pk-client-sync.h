/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2014 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_CLIENT_SYNC_H
#define __PK_CLIENT_SYNC_H

#include <glib.h>
#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-bitfield.h>
#include <packagekit-glib2/pk-progress.h>

G_BEGIN_DECLS

PkResults	*pk_client_resolve			(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**packages,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_search_names			(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_search_details		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_search_groups		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_search_files			(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_details			(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_details_local		(PkClient		*client,
							 gchar			**files,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_files_local		(PkClient		*client,
							 gchar			**files,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_update_detail		(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_download_packages		(PkClient		*client,
							 gchar			**package_ids,
							 const gchar		*directory,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_updates			(PkClient		*client,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_old_transactions		(PkClient		*client,
							 guint			 number,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_depends_on			(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**package_ids,
							 gboolean		 recursive,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_packages			(PkClient		*client,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_required_by			(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**package_ids,
							 gboolean		 recursive,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_what_provides		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_distro_upgrades		(PkClient		*client,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_files			(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_categories		(PkClient		*client,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_remove_packages		(PkClient		*client,
							 PkBitfield		 transaction_flags,
							 gchar			**package_ids,
							 gboolean		 allow_deps,
							 gboolean		 autoremove,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_refresh_cache		(PkClient		*client,
							 gboolean		 force,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_install_packages		(PkClient		*client,
							 PkBitfield		 transaction_flags,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_install_signature		(PkClient		*client,
							 PkSigTypeEnum		 type,
							 const gchar		*key_id,
							 const gchar		*package_id,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_update_packages		(PkClient		*client,
							 PkBitfield		 transaction_flags,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_install_files		(PkClient		*client,
							 PkBitfield		 transaction_flags,
							 gchar			**files,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_accept_eula			(PkClient		*client,
							 const gchar		*eula_id,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_get_repo_list		(PkClient		*client,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_repo_enable			(PkClient		*client,
							 const gchar		*repo_id,
							 gboolean		 enabled,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_repo_set_data		(PkClient		*client,
							 const gchar		*repo_id,
							 const gchar		*parameter,
							 const gchar		*value,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_repo_remove			(PkClient		*client,
							 PkBitfield		 transaction_flags,
							 const gchar		*repo_id,
							 gboolean		 autoremove,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_upgrade_system		(PkClient		*client,
							 PkBitfield		 transaction_flags,
							 const gchar		*distro_id,
							 PkUpgradeKindEnum	 upgrade_kind,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_repair_system		(PkClient		*client,
							 PkBitfield		 transaction_flags,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_client_adopt 			(PkClient		*client,
							 const gchar		*transaction_id,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkProgress	*pk_client_get_progress			(PkClient		*client,
							 const gchar		*transaction_id,
							 GCancellable		*cancellable,
							 GError			**error);

G_END_DECLS

#endif /* __PK_CLIENT_SYNC_H */

