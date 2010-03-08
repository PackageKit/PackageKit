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

#ifndef __PK_BACKEND_H
#define __PK_BACKEND_H

#include <glib.h>
#include <gmodule.h>

/* these include the includes the backends should be using */
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-bitfield.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-package-ids.h>

#include "egg-debug.h"
#include "egg-string.h"

G_BEGIN_DECLS

/**
 * PK_BACKEND_PERCENTAGE_INVALID:
 *
 * The unknown percentage value
 */
#define PK_BACKEND_PERCENTAGE_INVALID		101

typedef struct _PkBackend PkBackend;

/* set the state */
gboolean	 pk_backend_accept_eula			(PkBackend	*backend,
							 const gchar	*eula_id);
gboolean	 pk_backend_is_eula_valid		(PkBackend	*backend,
							 const gchar	*eula_id);
gboolean	 pk_backend_set_role			(PkBackend	*backend,
							 PkRoleEnum	 role);
PkRoleEnum	 pk_backend_get_role			(PkBackend	*backend);
gboolean	 pk_backend_set_status			(PkBackend	*backend,
							 PkStatusEnum	 status);
gboolean	 pk_backend_set_allow_cancel		(PkBackend	*backend,
							 gboolean	 allow_cancel);
gboolean	 pk_backend_set_percentage		(PkBackend	*backend,
							 guint		 percentage);
gboolean	 pk_backend_set_sub_percentage		(PkBackend	*backend,
							 guint		 percentage);
gboolean	 pk_backend_set_speed			(PkBackend	*backend,
							 guint		 speed);
gboolean	 pk_backend_set_exit_code		(PkBackend	*backend,
							 PkExitEnum	 exit);
gboolean	 pk_backend_set_transaction_data	(PkBackend	*backend,
							 const gchar	*data);
gboolean	 pk_backend_set_simultaneous_mode	(PkBackend	*backend,
							 gboolean	 simultaneous);
gboolean	 pk_backend_set_locale			(PkBackend	*backend,
							 const gchar	*code);

/* get the state */
gboolean	 pk_backend_get_allow_cancel		(PkBackend	*backend);
gboolean	 pk_backend_get_progress		(PkBackend	*backend,
							 guint		*percentage,
							 guint		*subpercentage,
							 guint		*elapsed,
							 guint		*remaining);
guint		 pk_backend_get_runtime			(PkBackend	*backend);
gchar		*pk_backend_get_proxy_ftp		(PkBackend	*backend);
gchar		*pk_backend_get_proxy_http		(PkBackend	*backend);
gchar		*pk_backend_get_locale			(PkBackend	*backend);

/* signal helpers */
gboolean	 pk_backend_finished			(PkBackend	*backend);
gboolean	 pk_backend_package			(PkBackend	*backend,
							 PkInfoEnum	 info,
							 const gchar	*package_id,
							 const gchar	*summary);
gboolean	 pk_backend_repo_detail			(PkBackend	*backend,
							 const gchar	*repo_id,
							 const gchar	*description,
							 gboolean	 enabled);
gboolean	 pk_backend_update_detail		(PkBackend	*backend,
							 const gchar	*package_id,
							 const gchar	*updates,
							 const gchar	*obsoletes,
							 const gchar	*vendor_url,
							 const gchar	*bugzilla_url,
							 const gchar	*cve_url,
							 PkRestartEnum	 restart,
							 const gchar	*update_text,
							 const gchar	*changelog,
							 PkUpdateStateEnum state,
							 const gchar	*issued,
							 const gchar	*updated);
gboolean	 pk_backend_require_restart		(PkBackend	*backend,
							 PkRestartEnum	 restart,
							 const gchar	*package_id);
gboolean	 pk_backend_message			(PkBackend	*backend,
							 PkMessageEnum	 message,
							 const gchar	*details, ...);
gboolean	 pk_backend_details			(PkBackend	*backend,
							 const gchar	*package_id,
							 const gchar	*license,
							 PkGroupEnum	 group,
							 const gchar	*description,
							 const gchar	*url,
							 gulong          size);
gboolean 	 pk_backend_files 			(PkBackend 	*backend,
							 const gchar	*package_id,
							 const gchar 	*filelist);
gboolean 	 pk_backend_distro_upgrade		(PkBackend 	*backend,
							 PkDistroUpgradeEnum type,
							 const gchar 	*name,
							 const gchar 	*summary);
gboolean	 pk_backend_error_code			(PkBackend	*backend,
							 PkErrorEnum	 code,
							 const gchar	*details, ...);
gboolean         pk_backend_repo_signature_required	(PkBackend      *backend,
							 const gchar	*package_id,
							 const gchar    *repository_name,
							 const gchar    *key_url,
							 const gchar    *key_userid,
							 const gchar    *key_id,
							 const gchar    *key_fingerprint,
							 const gchar    *key_timestamp,
							 PkSigTypeEnum   type);
gboolean         pk_backend_eula_required		(PkBackend      *backend,
							 const gchar	*eula_id,
							 const gchar    *package_id,
							 const gchar    *vendor_name,
							 const gchar    *license_agreement);
gboolean         pk_backend_media_change_required	(PkBackend      *backend,
							 PkMediaTypeEnum media_type,
							 const gchar    *media_id,
							 const gchar    *media_text);
gboolean         pk_backend_category			(PkBackend      *backend,
							 const gchar	*parent_id,
							 const gchar	*cat_id,
							 const gchar    *name,
							 const gchar    *summary,
							 const gchar    *icon);
gboolean         pk_backend_repo_list_changed		(PkBackend      *backend);

/* set backend instance data */
gboolean	 pk_backend_set_array			(PkBackend	*backend,
							 const gchar	*key,
							 GPtrArray	*data);
gboolean	 pk_backend_set_string			(PkBackend	*backend,
							 const gchar	*key,
							 const gchar	*data);
gboolean	 pk_backend_set_strv			(PkBackend	*backend,
							 const gchar	*key,
							 gchar		**data);
gboolean	 pk_backend_set_uint			(PkBackend	*backend,
							 const gchar	*key,
							 guint		 data);
gboolean	 pk_backend_set_bool			(PkBackend	*backend,
							 const gchar	*key,
							 gboolean	 data);
gboolean	 pk_backend_set_pointer			(PkBackend	*backend,
							 const gchar	*key,
							 gpointer	 data);

/* get backend instance data */
const gchar	*pk_backend_get_string			(PkBackend	*backend,
							 const gchar	*key);
const GPtrArray	*pk_backend_get_array			(PkBackend	*backend,
							 const gchar	*key);
gchar		**pk_backend_get_strv			(PkBackend	*backend,
							 const gchar	*key);
guint		 pk_backend_get_uint			(PkBackend	*backend,
							 const gchar	*key);
gboolean	 pk_backend_get_bool			(PkBackend	*backend,
							 const gchar	*key);
gpointer	 pk_backend_get_pointer			(PkBackend	*backend,
							 const gchar	*key);

/* helper functions */
const gchar	*pk_backend_bool_to_string		(gboolean	 value);
gboolean	 pk_backend_not_implemented_yet		(PkBackend	*backend,
							 const gchar	*method);
typedef gboolean (*PkBackendThreadFunc)			(PkBackend	*backend);
gboolean	 pk_backend_thread_create		(PkBackend	*backend,
							 PkBackendThreadFunc func);
gboolean	 pk_backend_is_online			(PkBackend	*backend);
gboolean	 pk_backend_use_background		(PkBackend	*backend);

/* config changed functions */
typedef void	(*PkBackendFileChanged)			(PkBackend	*backend,
							 gpointer	 data);
gboolean	 pk_backend_watch_file			(PkBackend	*backend,
							 const gchar	*filename,
							 PkBackendFileChanged func,
							 gpointer	 data);

/**
 * PkBackendDesc:
 */
typedef struct {
	const gchar	*description;
	const gchar	*author;
	void		(*initialize)			(PkBackend	*backend);
	void		(*destroy)			(PkBackend	*backend);
	PkBitfield	(*get_groups)			(PkBackend	*backend);
	PkBitfield	(*get_filters)			(PkBackend	*backend);
	PkBitfield	(*get_roles)			(PkBackend	*backend);
	gchar		*(*get_mime_types)		(PkBackend	*backend);
	void		(*cancel)			(PkBackend	*backend);
	void		(*download_packages)		(PkBackend	*backend,
							 gchar		**package_ids,
							 const gchar	*directory);
	void		(*get_categories)		(PkBackend	*backend);
	void		(*get_depends)			(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**package_ids,
							 gboolean	 recursive);
	void		(*get_details)			(PkBackend	*backend,
							 gchar		**package_ids);
	void		(*get_distro_upgrades)		(PkBackend	*backend);
	void		(*get_files)			(PkBackend	*backend,
							 gchar		**package_ids);
	void		(*get_packages)			(PkBackend	*backend,
							 PkBitfield	 filters);
	void		(*get_repo_list)		(PkBackend	*backend,
							 PkBitfield	 filters);
	void		(*get_requires)			(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**package_ids,
							 gboolean	 recursive);
	void		(*get_update_detail)		(PkBackend	*backend,
							 gchar		**package_ids);
	void		(*get_updates)			(PkBackend	*backend,
							 PkBitfield	 filters);
	void		(*install_files)		(PkBackend	*backend,
							 gboolean	 only_trusted,
							 gchar		**full_paths);
	void		(*install_packages)		(PkBackend	*backend,
							 gboolean	 only_trusted,
							 gchar		**package_ids);
	void		(*install_signature)		(PkBackend	*backend,
							 PkSigTypeEnum	 type,
							 const gchar	*key_id,
							 const gchar	*package_id);
	void		(*refresh_cache)		(PkBackend	*backend,
							 gboolean	 force);
	void		(*remove_packages)		(PkBackend	*backend,
							 gchar		**package_ids,
							 gboolean	 allow_deps,
							 gboolean	 autoremove);
	void		(*repo_enable)			(PkBackend	*backend,
							 const gchar	*repo_id,
							 gboolean	 enabled);
	void		(*repo_set_data)		(PkBackend	*backend,
							 const gchar	*repo_id,
							 const gchar	*parameter,
							 const gchar	*value);
	void		(*resolve)			(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**packages);
	void		(*rollback)			(PkBackend	*backend,
							 const gchar	*transaction_id);
	void		(*search_details)		(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**values);
	void		(*search_files)			(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**values);
	void		(*search_groups)		(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**values);
	void		(*search_names)			(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**values);
	void		(*update_packages)		(PkBackend	*backend,
							 gboolean	 only_trusted,
							 gchar		**package_ids);
	void		(*update_system)		(PkBackend	*backend,
							 gboolean	 only_trusted);
	void		(*what_provides)		(PkBackend	*backend,
							 PkBitfield	 filters,
							 PkProvidesEnum	 provides,
							 gchar		**values);
	void		(*simulate_install_files)	(PkBackend	*backend,
							 gchar		**full_paths);
	void		(*simulate_install_packages)	(PkBackend	*backend,
							 gchar		**package_ids);
	void		(*simulate_remove_packages)	(PkBackend	*backend,
							 gchar		**package_ids,
							 gboolean	 autoremove);
	void		(*simulate_update_packages)	(PkBackend	*backend,
							 gchar		**package_ids);
	gpointer	padding[10];
} PkBackendDesc;

#define PK_BACKEND_OPTIONS(description, author, initialize, destroy, get_groups, get_filters, get_roles, \
			   get_mime_types, cancel, download_packages, get_categories, get_depends,	\
			   get_details, get_distro_upgrades, get_files, get_packages, get_repo_list,	\
			   get_requires, get_update_detail, get_updates, install_files,			\
			   install_packages, install_signature, refresh_cache, remove_packages,		\
			   repo_enable, repo_set_data, resolve, rollback, search_details, search_file,	\
			   search_group, search_name, update_packages, update_system, what_provides,	\
			   simulate_install_files, simulate_install_packages, simulate_remove_packages,	\
			   simulate_update_packages )							\
	G_MODULE_EXPORT const PkBackendDesc pk_backend_desc = { 					\
		description,			\
		author,				\
		initialize,			\
		destroy,			\
		get_groups,			\
		get_filters,			\
		get_roles,			\
		get_mime_types,			\
		cancel,				\
		download_packages,		\
		get_categories,			\
		get_depends,			\
		get_details,			\
		get_distro_upgrades,		\
		get_files,			\
		get_packages,			\
		get_repo_list,			\
		get_requires,			\
		get_update_detail,		\
		get_updates,			\
		install_files,			\
		install_packages,		\
		install_signature,		\
		refresh_cache,			\
		remove_packages,		\
		repo_enable,			\
		repo_set_data,			\
		resolve,			\
		rollback,			\
		search_details,			\
		search_file,			\
		search_group,			\
		search_name,			\
		update_packages,		\
		update_system,			\
		what_provides,			\
		simulate_install_files,		\
		simulate_install_packages,	\
		simulate_remove_packages,	\
		simulate_update_packages,	\
		{0} 				\
	}

G_END_DECLS

#endif /* __PK_BACKEND_H */

