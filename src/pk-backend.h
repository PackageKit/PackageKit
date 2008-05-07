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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __PK_BACKEND_H
#define __PK_BACKEND_H

#include <glib.h>
#include <gmodule.h>
#include <pk-enum.h>
#include <pk-package-id.h>
#include <pk-debug.h>

G_BEGIN_DECLS

typedef struct _PkBackend PkBackend;

/* set the state */
gboolean	 pk_backend_set_current_tid		(PkBackend	*backend,
							 const gchar	*tid);
gboolean	 pk_backend_accept_eula			(PkBackend	*backend,
							 const gchar	*eula_id);
gboolean	 pk_backend_is_eula_valid		(PkBackend	*backend,
							 const gchar	*eula_id);
gboolean	 pk_backend_set_role			(PkBackend	*backend,
							 PkRoleEnum	 role);
gboolean	 pk_backend_set_status			(PkBackend	*backend,
							 PkStatusEnum	 status);
gboolean	 pk_backend_set_allow_cancel		(PkBackend	*backend,
							 gboolean	 allow_cancel);
gboolean	 pk_backend_set_percentage		(PkBackend	*backend,
							 guint		 percentage);
gboolean	 pk_backend_set_sub_percentage		(PkBackend	*backend,
							 guint		 percentage);
gboolean	 pk_backend_set_exit_code		(PkBackend	*backend,
							 PkExitEnum	 exit);
gboolean	 pk_backend_no_percentage_updates	(PkBackend	*backend);
gboolean	 pk_backend_set_transaction_data	(PkBackend	*backend,
							 const gchar	*data);

/* get the state */
const gchar	*pk_backend_get_current_tid		(PkBackend	*backend);
PkRoleEnum	 pk_backend_get_role			(PkBackend	*backend);
PkStatusEnum	 pk_backend_get_status			(PkBackend	*backend);
gboolean	 pk_backend_get_allow_cancel		(PkBackend	*backend);
gboolean	 pk_backend_get_progress		(PkBackend	*backend,
							 guint		*percentage,
							 guint		*subpercentage,
							 guint		*elapsed,
							 guint		*remaining);
guint		 pk_backend_get_runtime			(PkBackend	*backend);

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
							 const gchar	*update_text);
gboolean	 pk_backend_require_restart		(PkBackend	*backend,
							 PkRestartEnum	 restart,
							 const gchar	*details);
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
gboolean	 pk_backend_error_code			(PkBackend	*backend,
							 PkErrorCodeEnum code,
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

/* set backend instance data */
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
gchar		**pk_backend_get_strv			(PkBackend	*backend,
							 const gchar	*key);
guint		 pk_backend_get_uint			(PkBackend	*backend,
							 const gchar	*key);
gboolean	 pk_backend_get_bool			(PkBackend	*backend,
							 const gchar	*key);
gpointer	 pk_backend_get_pointer			(PkBackend	*backend,
							 const gchar	*key);

/* helper functions */
gboolean	 pk_backend_not_implemented_yet		(PkBackend	*backend,
							 const gchar	*method);
typedef gboolean (*PkBackendThreadFunc)			(PkBackend	*backend);
gboolean	 pk_backend_thread_create		(PkBackend	*backend,
							 PkBackendThreadFunc func);
gboolean	 pk_backend_is_online			(PkBackend	*backend);

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
	PkGroupEnum	(*get_groups)			(PkBackend	*backend);
	PkFilterEnum	(*get_filters)			(PkBackend	*backend);
	void		(*cancel)			(PkBackend	*backend);
	void		(*get_depends)			(PkBackend	*backend,
							 PkFilterEnum	 filters,
							 const gchar	*package_id,
							 gboolean	 recursive);
	void		(*get_details)		(PkBackend	*backend,
							 const gchar	*package_id);
	void		(*get_files)			(PkBackend	*backend,
							 const gchar	*package_id);
	void		(*get_packages)			(PkBackend	*backend,
							 PkFilterEnum	 filters);
	void		(*get_repo_list)		(PkBackend	*backend,
							 PkFilterEnum	 filters);
	void		(*get_requires)			(PkBackend	*backend,
							 PkFilterEnum	 filters,
							 const gchar	*package_id,
							 gboolean	 recursive);
	void		(*get_update_detail)		(PkBackend	*backend,
							 const gchar	*package_id);
	void		(*get_updates)			(PkBackend	*backend,
							 PkFilterEnum	 filters);
	void		(*install_file)			(PkBackend	*backend,
							 gboolean	 trusted,
							 const gchar	*full_path);
	void		(*install_package)		(PkBackend	*backend,
							 const gchar	*package_id);
	void		(*install_signature)		(PkBackend	*backend,
							 PkSigTypeEnum	 type,
							 const gchar	*key_id,
							 const gchar	*package_id);
	void		(*refresh_cache)		(PkBackend	*backend,
							 gboolean	 force);
	void		(*remove_package)		(PkBackend	*backend,
							 const gchar	*package_id,
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
							 PkFilterEnum	 filters,
							 const gchar	*package);
	void		(*rollback)			(PkBackend	*backend,
							 const gchar	*transaction_id);
	void		(*search_details)		(PkBackend	*backend,
							 PkFilterEnum	 filters,
							 const gchar	*search);
	void		(*search_file)			(PkBackend	*backend,
							 PkFilterEnum	 filters,
							 const gchar	*search);
	void		(*search_group)			(PkBackend	*backend,
							 PkFilterEnum	 filters,
							 const gchar	*search);
	void		(*search_name)			(PkBackend	*backend,
							 PkFilterEnum	 filters,
							 const gchar	*search);
	void		(*service_pack)			(PkBackend	*backend,
							 const gchar	*location,
							 gboolean	 enabled);
	void		(*update_packages)		(PkBackend	*backend,
							 gchar		**package_ids);
	void		(*update_system)		(PkBackend	*backend);
	void		(*what_provides)		(PkBackend	*backend,
							 PkFilterEnum	 filters,
							 PkProvidesEnum provide,
							 const gchar	*search);
	gpointer	padding[10];
} PkBackendDesc;

#define PK_BACKEND_OPTIONS(description, author, initialize, destroy, get_filters, get_groups, cancel,	\
			   get_depends, get_details, get_files, get_packages, get_repo_list, get_requires,	\
			   get_update_detail, get_updates, install_file, install_package,		\
			   install_signature, refresh_cache, remove_package, repo_enable,		\
			   repo_set_data, resolve, rollback, search_details, search_file, search_group,	\
			   search_name, service_pack, update_packages, update_system, what_provides)	\
	G_MODULE_EXPORT const PkBackendDesc pk_backend_desc = { 					\
		description,		\
		author,			\
		initialize,		\
		destroy,		\
		get_filters,		\
		get_groups,		\
		cancel,			\
		get_depends,		\
		get_details,	\
		get_files,		\
		get_packages,		\
		get_repo_list,		\
		get_requires,		\
		get_update_detail,	\
		get_updates,		\
		install_file,		\
		install_package,	\
		install_signature,	\
		refresh_cache,		\
		remove_package,		\
		repo_enable,		\
		repo_set_data,		\
		resolve,		\
		rollback,		\
		search_details,		\
		search_file,		\
		search_group,		\
		search_name,		\
		service_pack,		\
		update_packages,	\
		update_system,		\
		what_provides,		\
		{0} 			\
	}

G_END_DECLS

#endif /* __PK_BACKEND_H */

