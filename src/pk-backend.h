/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2012 Richard Hughes <richard@hughsie.com>
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

/* this is used to connect/disconnect backend signals */
typedef enum {
	PK_BACKEND_SIGNAL_ALLOW_CANCEL,
	PK_BACKEND_SIGNAL_DETAILS,
	PK_BACKEND_SIGNAL_ERROR_CODE,
	PK_BACKEND_SIGNAL_DISTRO_UPGRADE,
	PK_BACKEND_SIGNAL_FINISHED,
	PK_BACKEND_SIGNAL_MESSAGE,
	PK_BACKEND_SIGNAL_PACKAGE,
	PK_BACKEND_SIGNAL_ITEM_PROGRESS,
	PK_BACKEND_SIGNAL_FILES,
	PK_BACKEND_SIGNAL_NOTIFY_PERCENTAGE,
	PK_BACKEND_SIGNAL_NOTIFY_REMAINING,
	PK_BACKEND_SIGNAL_NOTIFY_SPEED,
	PK_BACKEND_SIGNAL_REPO_DETAIL,
	PK_BACKEND_SIGNAL_REPO_SIGNATURE_REQUIRED,
	PK_BACKEND_SIGNAL_EULA_REQUIRED,
	PK_BACKEND_SIGNAL_MEDIA_CHANGE_REQUIRED,
	PK_BACKEND_SIGNAL_REQUIRE_RESTART,
	PK_BACKEND_SIGNAL_STATUS_CHANGED,
	PK_BACKEND_SIGNAL_UPDATE_DETAIL,
	PK_BACKEND_SIGNAL_CATEGORY,
	PK_BACKEND_SIGNAL_LAST
} PkBackendSignal;

GType		 pk_backend_get_type			(void);
PkBackend	*pk_backend_new				(void);
gboolean	 pk_backend_load			(PkBackend	*backend,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_backend_unload			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_backend_reset			(PkBackend	*backend);
gboolean	 pk_backend_set_proxy			(PkBackend	*backend,
							 const gchar	*proxy_http,
							 const gchar	*proxy_https,
							 const gchar	*proxy_ftp,
							 const gchar	*proxy_socks,
							 const gchar	*no_proxy,
							 const gchar	*pac);
gboolean	 pk_backend_set_root			(PkBackend	*backend,
							 const gchar	*root);
gboolean	 pk_backend_set_uid			(PkBackend	*backend,
							 guint		 uid);
gboolean	 pk_backend_set_cmdline			(PkBackend	*backend,
							 const gchar	*cmdline);
gboolean	 pk_backend_get_is_finished		(PkBackend	*backend);

const gchar	*pk_backend_get_name			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
const gchar	*pk_backend_get_description		(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
const gchar	*pk_backend_get_author			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;

typedef gchar	*(*PkBackendGetCompatStringFunc)	(PkBackend	*backend);
PkBitfield	 pk_backend_get_groups			(PkBackend	*backend);
PkBitfield	 pk_backend_get_filters			(PkBackend	*backend);
PkBitfield	 pk_backend_get_roles			(PkBackend	*backend);
gchar		*pk_backend_get_mime_types		(PkBackend	*backend);
gboolean	 pk_backend_has_set_error_code		(PkBackend	*backend);
gboolean	 pk_backend_is_implemented		(PkBackend	*backend,
							 PkRoleEnum	 role);
void		 pk_backend_implement			(PkBackend	*backend,
							 PkRoleEnum	 role);
gchar		*pk_backend_get_accepted_eula_string	(PkBackend	*backend);
void		pk_backend_cancel			(PkBackend	*backend);
void		pk_backend_download_packages		(PkBackend	*backend,
							 gchar		**package_ids,
							 const gchar	*directory);
void		pk_backend_initialize			(PkBackend	*backend);
void		pk_backend_destroy			(PkBackend	*backend);
void		pk_backend_transaction_start		(PkBackend	*backend);
void		pk_backend_transaction_stop		(PkBackend	*backend);
void		pk_backend_transaction_reset		(PkBackend	*backend);
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
							 PkBitfield	 transaction_flags,
							 gchar		**package_ids);
void		pk_backend_install_signature		(PkBackend	*backend,
							 PkSigTypeEnum	 type,
							 const gchar	*key_id,
							 const gchar	*package_id);
void		pk_backend_install_files		(PkBackend	*backend,
							 PkBitfield	 transaction_flags,
							 gchar		**full_paths);
void		pk_backend_refresh_cache		(PkBackend	*backend,
							 gboolean	 force);
void		pk_backend_remove_packages		(PkBackend	*backend,
							 PkBitfield	 transaction_flags,
							 gchar		**package_ids,
							 gboolean	 allow_deps,
							 gboolean	 autoremove);
void		pk_backend_resolve			(PkBackend	*backend,
							 PkBitfield	 filters,
							 gchar		**packages);
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
							 PkBitfield	 transaction_flags,
							 gchar		**package_ids);
void		pk_backend_update_system		(PkBackend	*backend,
							 PkBitfield	 transaction_flags);
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
void		pk_backend_upgrade_system		(PkBackend	*backend,
							 const gchar	*distro_id,
							 PkUpgradeKindEnum upgrade_kind);
void		pk_backend_repair_system		(PkBackend	*backend,
							 PkBitfield	 transaction_flags);

/* set the state */
gboolean	 pk_backend_accept_eula			(PkBackend	*backend,
							 const gchar	*eula_id);
gboolean	 pk_backend_is_eula_valid		(PkBackend	*backend,
							 const gchar	*eula_id);
gboolean	 pk_backend_set_role			(PkBackend	*backend,
							 PkRoleEnum	 role);
PkRoleEnum	 pk_backend_get_role			(PkBackend	*backend);
PkExitEnum	 pk_backend_get_exit_code		(PkBackend	*backend);
gboolean	 pk_backend_set_status			(PkBackend	*backend,
							 PkStatusEnum	 status);
gboolean	 pk_backend_set_allow_cancel		(PkBackend	*backend,
							 gboolean	 allow_cancel);
gboolean	 pk_backend_set_percentage		(PkBackend	*backend,
							 guint		 percentage);
gboolean	 pk_backend_set_item_progress		(PkBackend	*backend,
							 const gchar	*package_id,
							 guint		 percentage);
gboolean	 pk_backend_set_speed			(PkBackend	*backend,
							 guint		 speed);
gboolean	 pk_backend_set_exit_code		(PkBackend	*backend,
							 PkExitEnum	 exit);
gboolean	 pk_backend_set_transaction_data	(PkBackend	*backend,
							 const gchar	*data);
gboolean	 pk_backend_set_simultaneous_mode	(PkBackend	*backend,
							 gboolean	 simultaneous);
gboolean	 pk_backend_set_locked			(PkBackend	*backend,
							 gboolean	 locked);
gboolean	 pk_backend_set_locale			(PkBackend	*backend,
							 const gchar	*code);
gboolean	 pk_backend_set_frontend_socket		(PkBackend	*backend,
							 const gchar	*frontend_socket);
void		 pk_backend_set_cache_age		(PkBackend	*backend,
							 guint		 cache_age);

/* get the state */
gboolean	 pk_backend_get_allow_cancel		(PkBackend	*backend);
gboolean	 pk_backend_get_locked			(PkBackend	*backend);
gboolean         pk_backend_get_is_error_set		(PkBackend	*backend);
guint		 pk_backend_get_runtime			(PkBackend	*backend);
gchar		*pk_backend_get_proxy_ftp		(PkBackend	*backend);
gchar		*pk_backend_get_proxy_http		(PkBackend	*backend);
gchar		*pk_backend_get_proxy_https		(PkBackend	*backend);
gchar		*pk_backend_get_proxy_socks		(PkBackend	*backend);
gchar		*pk_backend_get_no_proxy		(PkBackend	*backend);
gchar		*pk_backend_get_pac			(PkBackend	*backend);
const gchar	*pk_backend_get_root			(PkBackend	*backend);
gchar		*pk_backend_get_locale			(PkBackend	*backend);
gchar		*pk_backend_get_frontend_socket		(PkBackend	*backend);
guint		 pk_backend_get_cache_age		(PkBackend	*backend);

/* transaction vfuncs */
typedef void	 (*PkBackendVFunc)			(PkBackend	*backend,
							 GObject	*object,
							 gpointer	 user_data);
void		 pk_backend_set_vfunc			(PkBackend	*backend,
							 PkBackendSignal signal_kind,
							 PkBackendVFunc	 vfunc,
							 gpointer	 user_data);

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
void		 pk_backend_thread_finished		(PkBackend	*backend);

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
							 PkBitfield	 transaction_flags,
							 gchar		**full_paths);
	void		(*install_packages)		(PkBackend	*backend,
							 PkBitfield	 transaction_flags,
							 gchar		**package_ids);
	void		(*install_signature)		(PkBackend	*backend,
							 PkSigTypeEnum	 type,
							 const gchar	*key_id,
							 const gchar	*package_id);
	void		(*refresh_cache)		(PkBackend	*backend,
							 gboolean	 force);
	void		(*remove_packages)		(PkBackend	*backend,
							 PkBitfield	 transaction_flags,
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
							 PkBitfield	 transaction_flags,
							 gchar		**package_ids);
	void		(*update_system)		(PkBackend	*backend,
							 PkBitfield	 transaction_flags);
	void		(*what_provides)		(PkBackend	*backend,
							 PkBitfield	 filters,
							 PkProvidesEnum	 provides,
							 gchar		**values);
	void		(*transaction_start)		(PkBackend	*backend);
	void		(*transaction_stop)		(PkBackend	*backend);
	void		(*upgrade_system)		(PkBackend	*backend,
							 const gchar	*distro_id,
							 PkUpgradeKindEnum upgrade_kind);
	void		(*transaction_reset)		(PkBackend	*backend);
	void		(*repair_system)		(PkBackend	*backend,
							 PkBitfield	 transaction_flags);
	gpointer	padding[20];
} PkBackendDesc;

G_END_DECLS

#endif /* __PK_BACKEND_H */

