/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2014 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_BACKEND_H
#define __PK_BACKEND_H

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>

/* these include the includes the backends should be using */
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-bitfield.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-package-ids.h>
#include <packagekit-glib2/pk-bitfield.h>

#include "pk-backend.h"
#include "pk-backend-job.h"
#include "pk-cleanup.h"

G_BEGIN_DECLS

#define PK_TYPE_BACKEND		(pk_backend_get_type ())
#define PK_BACKEND(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_BACKEND, PkBackend))
#define PK_BACKEND_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_BACKEND, PkBackendClass))
#define PK_IS_BACKEND(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_BACKEND))
#define PK_IS_BACKEND_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_BACKEND))
#define PK_BACKEND_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_BACKEND, PkBackendClass))

typedef struct PkBackendPrivate PkBackendPrivate;

typedef struct
{
	 GObject		 parent;
	 PkBackendPrivate	*priv;
} PkBackend;

typedef struct
{
	GObjectClass		 parent_class;
} PkBackendClass;

/**
 * PK_BACKEND_PERCENTAGE_INVALID:
 *
 * The unknown percentage value
 */
#define PK_BACKEND_PERCENTAGE_INVALID		101

GType		 pk_backend_get_type			(void);
PkBackend	*pk_backend_new				(GKeyFile		*conf);

/* utililties */
gboolean	 pk_backend_load			(PkBackend	*backend,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_backend_unload			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;

gboolean	 pk_backend_is_implemented		(PkBackend	*backend,
							 PkRoleEnum	 role);
gchar		*pk_backend_get_accepted_eula_string	(PkBackend	*backend);
void		 pk_backend_repo_list_changed		(PkBackend      *backend);
void		 pk_backend_installed_db_changed	(PkBackend      *backend);


gboolean	 pk_backend_updates_changed		(PkBackend	*backend);
gboolean	 pk_backend_updates_changed_delay	(PkBackend	*backend,
							 guint		 timeout);

void		 pk_backend_transaction_inhibit_start	(PkBackend      *backend);
void		 pk_backend_transaction_inhibit_end	(PkBackend      *backend);
const gchar	*pk_backend_bool_to_string		(gboolean	 value);
gboolean	 pk_backend_is_online			(PkBackend	*backend);
gchar		*pk_backend_convert_uri			(const gchar	*proxy);

/* config changed functions */
typedef void	(*PkBackendFileChanged)			(PkBackend	*backend,
							 gpointer	 data);
gboolean	 pk_backend_watch_file			(PkBackend	*backend,
							 const gchar	*filename,
							 PkBackendFileChanged func,
							 gpointer	 data);

/* call into the backend using a vfunc */
const gchar	*pk_backend_get_name			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
const gchar	*pk_backend_get_description		(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
const gchar	*pk_backend_get_author			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
PkBitfield	 pk_backend_get_groups			(PkBackend	*backend);
PkBitfield	 pk_backend_get_filters			(PkBackend	*backend);
PkBitfield	 pk_backend_get_roles			(PkBackend	*backend);
gchar		**pk_backend_get_mime_types		(PkBackend	*backend);
gboolean	 pk_backend_supports_parallelization	(PkBackend	*backend);
void		 pk_backend_initialize			(GKeyFile		*conf,
							 PkBackend	*backend);
void		 pk_backend_destroy			(PkBackend	*backend);
void		 pk_backend_start_job			(PkBackend	*backend,
							 PkBackendJob	*job);
void		 pk_backend_stop_job			(PkBackend	*backend,
							 PkBackendJob	*job);
void		 pk_backend_cancel			(PkBackend	*backend,
							 PkBackendJob	*job);
void		 pk_backend_download_packages		(PkBackend	*backend,
							 PkBackendJob	*job,
							 gchar		**package_ids,
							 const gchar	*directory);
void		 pk_backend_get_categories		(PkBackend	*backend,
							 PkBackendJob	*job);
void		 pk_backend_depends_on			(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**package_ids,
							 gboolean	 recursive);
void		 pk_backend_get_details			(PkBackend	*backend,
							 PkBackendJob	*job,
							 gchar		**package_ids);
void		 pk_backend_get_details_local		(PkBackend	*backend,
							 PkBackendJob	*job,
							 gchar		**files);
void		 pk_backend_get_files_local		(PkBackend	*backend,
							 PkBackendJob	*job,
							 gchar		**files);
void		 pk_backend_get_distro_upgrades		(PkBackend	*backend,
							 PkBackendJob	*job);
void		 pk_backend_get_files			(PkBackend	*backend,
							 PkBackendJob	*job,
							 gchar		**package_ids);
void		 pk_backend_required_by		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**package_ids,
							 gboolean	 recursive);
void		 pk_backend_get_update_detail		(PkBackend	*backend,
							 PkBackendJob	*job,
							 gchar		**package_ids);
void		 pk_backend_get_updates			(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters);
void		 pk_backend_install_packages		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags,
							 gchar		**package_ids);
void		 pk_backend_install_signature		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkSigTypeEnum	 type,
							 const gchar	*key_id,
							 const gchar	*package_id);
void		 pk_backend_install_files		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags,
							 gchar		**full_paths);
void		 pk_backend_refresh_cache		(PkBackend	*backend,
							 PkBackendJob	*job,
							 gboolean	 force);
void		 pk_backend_remove_packages		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags,
							 gchar		**package_ids,
							 gboolean	 allow_deps,
							 gboolean	 autoremove);
void		 pk_backend_resolve			(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**packages);
void		 pk_backend_search_details		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**search);
void		 pk_backend_search_files		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**search);
void		 pk_backend_search_groups		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**search);
void		 pk_backend_search_names		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**search);
void		 pk_backend_update_packages		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags,
							 gchar		**package_ids);
void		 pk_backend_get_repo_list		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters);
void		 pk_backend_repo_enable			(PkBackend	*backend,
							 PkBackendJob	*job,
							 const gchar	*repo_id,
							 gboolean	 enabled);
void		 pk_backend_repo_set_data		(PkBackend	*backend,
							 PkBackendJob	*job,
							 const gchar	*repo_id,
							 const gchar	*parameter,
							 const gchar	*value);
void		 pk_backend_repo_remove			(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags,
							 const gchar	*repo_id,
							 gboolean	 autoremove);
void		 pk_backend_what_provides		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**search);
void		 pk_backend_get_packages		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters);
void		 pk_backend_repair_system		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags);

/* thread helpers */
void		 pk_backend_thread_start		(PkBackend	*backend,
							 PkBackendJob	*job,
							 gpointer	 func);
void		 pk_backend_thread_stop			(PkBackend	*backend,
							 PkBackendJob	*job,
							 gpointer	 func);

/* global backend state */
void		 pk_backend_accept_eula			(PkBackend	*backend,
							 const gchar	*eula_id);
gboolean	 pk_backend_is_eula_valid		(PkBackend	*backend,
							 const gchar	*eula_id);
gpointer	 pk_backend_get_user_data		(PkBackend	*backend);
void		 pk_backend_set_user_data		(PkBackend	*backend,
							 gpointer	 user_data);

G_END_DECLS

#endif /* __PK_BACKEND_H */

