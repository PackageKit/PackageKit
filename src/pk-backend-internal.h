/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_BACKEND_INTERNAL_H
#define __PK_BACKEND_INTERNAL_H

#include <glib-object.h>
#include <packagekit-glib2/pk-bitfield.h>

#include "pk-store.h"
#include "pk-backend.h"

G_BEGIN_DECLS

#define PK_TYPE_BACKEND		(pk_backend_get_type ())
#define PK_BACKEND(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_BACKEND, PkBackend))
#define PK_BACKEND_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_BACKEND, PkBackendClass))
#define PK_IS_BACKEND(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_BACKEND))
#define PK_IS_BACKEND_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_BACKEND))
#define PK_BACKEND_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_BACKEND, PkBackendClass))

typedef struct _PkBackendPrivate PkBackendPrivate;
typedef struct _PkBackendClass PkBackendClass;

struct _PkBackend
{
	GObject			 parent;
	PkBackendPrivate	*priv;
};

struct _PkBackendClass
{
	GObjectClass	parent_class;
};

GType		 pk_backend_get_type			(void);
PkBackend	*pk_backend_new				(void);
gboolean	 pk_backend_lock			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_backend_unlock			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_backend_reset			(PkBackend	*backend);
gboolean	 pk_backend_set_name			(PkBackend	*backend,
							 const gchar	*name)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_backend_set_proxy			(PkBackend	*backend,
							 const gchar	*proxy_http,
							 const gchar	*proxy_ftp);
gboolean	 pk_backend_set_root			(PkBackend	*backend,
							 const gchar	*root);
gchar		*pk_backend_get_name			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		*pk_backend_get_description		(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		*pk_backend_get_author			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
PkBitfield	 pk_backend_get_groups			(PkBackend	*backend);
PkBitfield	 pk_backend_get_filters			(PkBackend	*backend);
PkBitfield	 pk_backend_get_roles			(PkBackend	*backend);
gchar		*pk_backend_get_mime_types		(PkBackend	*backend);
gboolean	 pk_backend_has_set_error_code		(PkBackend	*backend);
gboolean	 pk_backend_is_implemented		(PkBackend	*backend,
							 PkRoleEnum	 role);
gchar		*pk_backend_get_accepted_eula_string	(PkBackend	*backend);
void		pk_backend_cancel			(PkBackend	*backend);
void		pk_backend_download_packages		(PkBackend	*backend,
							 gchar		**package_ids,
							 const gchar	*directory);
void		pk_backend_get_categories		(PkBackend	*backend);
void		pk_backend_get_depends			(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**package_ids,
							 gboolean	 recursive);
void		pk_backend_get_details			(PkBackend	*backend,
							 gchar		**package_ids);
void		pk_backend_get_distro_upgrades		(PkBackend	*backend);
void		pk_backend_get_files			(PkBackend	*backend,
							 gchar		**package_ids);
void		pk_backend_get_requires			(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**package_ids,
							 gboolean	 recursive);
void		pk_backend_get_update_detail		(PkBackend	*backend,
							 gchar		**package_ids);
void		pk_backend_get_updates			(PkBackend	*backend,
							 PkBitfield	 filters);
void		pk_backend_install_packages		(PkBackend	*backend,
							 gboolean	 only_trusted,
							 gchar		**package_ids);
void		pk_backend_install_signature		(PkBackend	*backend,
							 PkSigTypeEnum	 type,
							 const gchar	*key_id,
							 const gchar	*package_id);
void		pk_backend_install_files		(PkBackend	*backend,
							 gboolean	 only_trusted,
							 gchar		**full_paths);
void		pk_backend_refresh_cache		(PkBackend	*backend,
							 gboolean	 force);
void		pk_backend_remove_packages		(PkBackend	*backend,
							 gchar		**package_ids,
							 gboolean	 allow_deps,
							 gboolean	 autoremove);
void		pk_backend_resolve			(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**packages);
void		pk_backend_rollback			(PkBackend	*backend,
							 const gchar	*transaction_id);
void		pk_backend_search_details		(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**search);
void		pk_backend_search_files			(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**search);
void		pk_backend_search_groups		(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**search);
void		pk_backend_search_names			(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**search);
void		pk_backend_update_packages		(PkBackend	*backend,
							 gboolean	 only_trusted,
							 gchar		**package_ids);
void		pk_backend_update_system		(PkBackend	*backend,
							 gboolean	 only_trusted);
void		pk_backend_get_repo_list		(PkBackend	*backend,
							 PkBitfield	 filters);
void		pk_backend_repo_enable			(PkBackend	*backend,
							 const gchar	*repo_id,
							 gboolean	 enabled);
void		pk_backend_repo_set_data		(PkBackend	*backend,
							 const gchar	*repo_id,
							 const gchar	*parameter,
							 const gchar	*value);
void		pk_backend_what_provides		(PkBackend	*backend,
							 PkBitfield	 filters,
							 PkProvidesEnum provides,
							 gchar		**search);
void		pk_backend_get_packages			(PkBackend	*backend,
							 PkBitfield	 filters);
void		pk_backend_simulate_install_files	(PkBackend	*backend,
							 gchar		**full_paths);
void		pk_backend_simulate_install_packages	(PkBackend	*backend,
							 gchar		**package_ids);
void		pk_backend_simulate_remove_packages	(PkBackend	*backend,
							 gchar		**package_ids,
							 gboolean	 autoremove);
void		pk_backend_simulate_update_packages	(PkBackend	*backend,
							 gchar		**package_ids);

G_END_DECLS

#endif /* __PK_BACKEND_INTERNAL_H */

