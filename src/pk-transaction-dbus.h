/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_TRANSACTION_DBUS_H
#define __PK_TRANSACTION_DBUS_H

#include "pk-transaction.h"

G_BEGIN_DECLS

void		 pk_transaction_accept_eula			(PkTransaction	*transaction,
								 const gchar	*eula_id,
								 DBusGMethodInvocation *context);
void		 pk_transaction_cancel				(PkTransaction	*transaction,
								 DBusGMethodInvocation *context);
void		 pk_transaction_download_packages		(PkTransaction  *transaction,
								 gboolean	 copy_files,
								 gchar		**package_ids,
								 DBusGMethodInvocation *context);
void		 pk_transaction_get_categories			(PkTransaction	*transaction,
								 DBusGMethodInvocation *context);
void		 pk_transaction_get_depends			(PkTransaction	*transaction,
								 const gchar	*filter,
								 gchar		**package_ids,
								 gboolean	 recursive,
								 DBusGMethodInvocation *context);
void		 pk_transaction_get_details			(PkTransaction	*transaction,
								 gchar		**package_ids,
								 DBusGMethodInvocation *context);
void		 pk_transaction_get_distro_upgrades		(PkTransaction	*transaction,
								 DBusGMethodInvocation *context);
void		 pk_transaction_get_files			(PkTransaction	*transaction,
								 gchar		**package_ids,
								 DBusGMethodInvocation *context);
gboolean	 pk_transaction_get_old_transactions		(PkTransaction	*transaction,
								 guint		 number,
								 GError		**error);
void		 pk_transaction_get_packages			(PkTransaction	*transaction,
								 const gchar	*filter,
								 DBusGMethodInvocation *context);
void		 pk_transaction_get_repo_list			(PkTransaction	*transaction,
								 const gchar	*filter,
								 DBusGMethodInvocation *context);
void		 pk_transaction_get_requires			(PkTransaction	*transaction,
								 const gchar	*filter,
								 gchar		**package_ids,
								 gboolean	 recursive,
								 DBusGMethodInvocation *context);
void		 pk_transaction_get_update_detail		(PkTransaction	*transaction,
								 gchar		**package_ids,
								 DBusGMethodInvocation *context);
void		 pk_transaction_get_updates			(PkTransaction	*transaction,
								 const gchar	*filter,
								 DBusGMethodInvocation *context);
void		 pk_transaction_install_files			(PkTransaction	*transaction,
								 gboolean	 only_trusted,
								 gchar		**full_paths,
								 DBusGMethodInvocation *context);
void		 pk_transaction_install_packages		(PkTransaction	*transaction,
								 gboolean	 only_trusted,
								 gchar		**package_ids,
								 DBusGMethodInvocation *context);
void		 pk_transaction_install_signature		(PkTransaction	*transaction,
								 const gchar	*sig_type,
								 const gchar	*key_id,
								 const gchar	*package_id,
								DBusGMethodInvocation *context);
void		 pk_transaction_refresh_cache			(PkTransaction	*transaction,
								 gboolean	 force,
								 DBusGMethodInvocation *context);
void		 pk_transaction_remove_packages			(PkTransaction	*transaction,
								 gchar		**package_ids,
								 gboolean	 allow_deps,
								 gboolean	 autoremove,
								 DBusGMethodInvocation *context);
void		 pk_transaction_repo_enable			(PkTransaction	*transaction,
								 const gchar	*repo_id,
								 gboolean	 enabled,
								 DBusGMethodInvocation *context);
void		 pk_transaction_repo_set_data			(PkTransaction	*transaction,
								 const gchar	*repo_id,
								 const gchar	*parameter,
								 const gchar	*value,
								 DBusGMethodInvocation *context);
void		 pk_transaction_resolve				(PkTransaction	*transaction,
								 const gchar	*filter,
								 gchar		**packages,
								 DBusGMethodInvocation *context);
void		 pk_transaction_rollback			(PkTransaction	*transaction,
								 const gchar	*transaction_id,
								 DBusGMethodInvocation *context);
void		 pk_transaction_search_details			(PkTransaction	*transaction,
								 const gchar	*filter,
								 gchar		**values,
								 DBusGMethodInvocation *context);
void		 pk_transaction_search_files			(PkTransaction	*transaction,
								 const gchar	*filter,
								 gchar		**values,
								 DBusGMethodInvocation *context);
void		 pk_transaction_search_groups			(PkTransaction	*transaction,
								 const gchar	*filter,
								 gchar		**values,
								 DBusGMethodInvocation *context);
void		 pk_transaction_search_names			(PkTransaction	*transaction,
								 const gchar	*filter,
								 gchar		**values,
								 DBusGMethodInvocation *context);
void		 pk_transaction_set_hints			(PkTransaction	*transaction,
								 gchar		**hints,
								 DBusGMethodInvocation *context);
void		 pk_transaction_simulate_install_files		(PkTransaction  *transaction,
								 gchar		**full_paths,
								 DBusGMethodInvocation *context);
void		 pk_transaction_simulate_install_packages	(PkTransaction  *transaction,
								 gchar		**package_ids,
								 DBusGMethodInvocation *context);
void		 pk_transaction_simulate_remove_packages	(PkTransaction  *transaction,
								 gchar		**package_ids,
								 gboolean	 autoremove,
								 DBusGMethodInvocation *context);
void		 pk_transaction_simulate_update_packages	(PkTransaction  *transaction,
								 gchar		**package_ids,
								 DBusGMethodInvocation *context);
void		 pk_transaction_update_packages			(PkTransaction	*transaction,
								 gboolean	 only_trusted,
								 gchar		**package_ids,
								 DBusGMethodInvocation *context);
void		 pk_transaction_update_system			(PkTransaction	*transaction,
								 gboolean	 only_trusted,
								 DBusGMethodInvocation *context);
void		 pk_transaction_what_provides			(PkTransaction	*transaction,
								 const gchar	*filter,
								 const gchar	*type,
								 gchar		**values,
								 DBusGMethodInvocation *context);
void		 pk_transaction_upgrade_system			(PkTransaction	*transaction,
								 const gchar	*distro_id,
								 const gchar	*upgrade_kind_str,
								 DBusGMethodInvocation *context);

G_END_DECLS

#endif /* __PK_TRANSACTION_DBUS_H */
