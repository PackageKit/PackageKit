/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_BACKEND_JOB_H
#define __PK_BACKEND_JOB_H

#include <glib-object.h>

#include "pk-shared.h"
#include <packagekit-glib2/pk-bitfield.h>

G_BEGIN_DECLS

#define PK_TYPE_BACKEND_JOB		(pk_backend_job_get_type ())
#define PK_BACKEND_JOB(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_BACKEND_JOB, PkBackendJob))
#define PK_BACKEND_JOB_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_BACKEND_JOB, PkBackendJobClass))
#define PK_IS_BACKEND_JOB(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_BACKEND_JOB))
#define PK_IS_BACKEND_JOB_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_BACKEND_JOB))
#define PK_BACKEND_JOB_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_BACKEND_JOB, PkBackendJobClass))

typedef struct PkBackendJobPrivate PkBackendJobPrivate;

/* this is used to connect/disconnect backend signals */
typedef enum {
	PK_BACKEND_SIGNAL_ALLOW_CANCEL,
	PK_BACKEND_SIGNAL_DETAILS,
	PK_BACKEND_SIGNAL_ERROR_CODE,
	PK_BACKEND_SIGNAL_DISTRO_UPGRADE,
	PK_BACKEND_SIGNAL_FINISHED,
	PK_BACKEND_SIGNAL_PACKAGE,
	PK_BACKEND_SIGNAL_ITEM_PROGRESS,
	PK_BACKEND_SIGNAL_FILES,
	PK_BACKEND_SIGNAL_PERCENTAGE,
	PK_BACKEND_SIGNAL_REMAINING,
	PK_BACKEND_SIGNAL_SPEED,
	PK_BACKEND_SIGNAL_DOWNLOAD_SIZE_REMAINING,
	PK_BACKEND_SIGNAL_REPO_DETAIL,
	PK_BACKEND_SIGNAL_REPO_SIGNATURE_REQUIRED,
	PK_BACKEND_SIGNAL_EULA_REQUIRED,
	PK_BACKEND_SIGNAL_MEDIA_CHANGE_REQUIRED,
	PK_BACKEND_SIGNAL_REQUIRE_RESTART,
	PK_BACKEND_SIGNAL_STATUS_CHANGED,
	PK_BACKEND_SIGNAL_LOCKED_CHANGED,
	PK_BACKEND_SIGNAL_UPDATE_DETAIL,
	PK_BACKEND_SIGNAL_CATEGORY,
	PK_BACKEND_SIGNAL_LAST
} PkBackendJobSignal;

typedef struct
{
	GObject			 parent;
	PkBackendJobPrivate	*priv;
} PkBackendJob;

typedef struct
{
	GObjectClass	parent_class;
} PkBackendJobClass;

GType		 pk_backend_job_get_type		(void);
PkBackendJob	*pk_backend_job_new			(GKeyFile		*conf);

void		 pk_backend_job_reset			(PkBackendJob	*job);
gpointer	 pk_backend_job_get_backend		(PkBackendJob	*job);
void		 pk_backend_job_set_backend		(PkBackendJob	*job,
							 gpointer	 backend);
gpointer	 pk_backend_job_get_user_data		(PkBackendJob	*job);
void		 pk_backend_job_set_user_data		(PkBackendJob	*job,
							 gpointer	 user_data);
PkBitfield	 pk_backend_job_get_transaction_flags	(PkBackendJob	*job);
void		 pk_backend_job_set_transaction_flags	(PkBackendJob	*job,
							 PkBitfield	 transaction_flags);
GVariant	*pk_backend_job_get_parameters		(PkBackendJob	*job);
void		 pk_backend_job_set_parameters		(PkBackendJob	*job,
							 GVariant	*params);
PkHintEnum	 pk_backend_job_get_background		(PkBackendJob	*job);
void		 pk_backend_job_set_background		(PkBackendJob	*job,
							 PkHintEnum	 background);
PkHintEnum	 pk_backend_job_get_interactive		(PkBackendJob	*job);
void		 pk_backend_job_set_interactive		(PkBackendJob	*job,
							 PkHintEnum	 interactive);
void		 pk_backend_job_set_locked		(PkBackendJob	*job,
							 gboolean	 locked);
gboolean	 pk_backend_job_get_locked		(PkBackendJob	*job);
void		 pk_backend_job_set_role		(PkBackendJob	*job,
							 PkRoleEnum	 role);
PkRoleEnum	 pk_backend_job_get_role		(PkBackendJob	*job);
PkExitEnum	 pk_backend_job_get_exit_code		(PkBackendJob	*job);
void		 pk_backend_job_set_exit_code		(PkBackendJob	*job,
							 PkExitEnum	 exit);
gboolean	 pk_backend_job_has_set_error_code	(PkBackendJob	*job);
void		 pk_backend_job_not_implemented_yet	(PkBackendJob	*job,
							 const gchar *method);
guint		 pk_backend_job_get_runtime		(PkBackendJob	*job);
gboolean	 pk_backend_job_get_is_finished		(PkBackendJob	*job);
gboolean	 pk_backend_job_get_is_error_set	(PkBackendJob	*job);
gboolean	 pk_backend_job_get_allow_cancel	(PkBackendJob	*job);
void		 pk_backend_job_set_proxy		(PkBackendJob	*job,
							 const gchar	*proxy_http,
							 const gchar	*proxy_https,
							 const gchar	*proxy_ftp,
							 const gchar	*proxy_socks,
							 const gchar	*no_proxy,
							 const gchar	*pac);
void		 pk_backend_job_set_uid			(PkBackendJob	*job,
							 guint		 uid);
guint		 pk_backend_job_get_uid			(PkBackendJob	*job);
void		 pk_backend_job_set_cmdline		(PkBackendJob	*job,
							 const gchar	*cmdline);
const gchar	*pk_backend_job_get_cmdline		(PkBackendJob	*job);
void		 pk_backend_job_set_locale		(PkBackendJob	*job,
							 const gchar	*code);
void		 pk_backend_job_set_frontend_socket	(PkBackendJob	*job,
							 const gchar	*frontend_socket);
void		 pk_backend_job_set_cache_age		(PkBackendJob	*job,
							 guint		 cache_age);
gchar		*pk_backend_job_get_proxy_ftp		(PkBackendJob	*job);
gchar		*pk_backend_job_get_proxy_http		(PkBackendJob	*job);
gchar		*pk_backend_job_get_proxy_https		(PkBackendJob	*job);
gchar		*pk_backend_job_get_proxy_socks		(PkBackendJob	*job);
gchar		*pk_backend_job_get_no_proxy		(PkBackendJob	*job);
gchar		*pk_backend_job_get_pac			(PkBackendJob	*job);
gchar		*pk_backend_job_get_locale		(PkBackendJob	*job);
gchar		*pk_backend_job_get_frontend_socket	(PkBackendJob	*job);
guint		 pk_backend_job_get_cache_age		(PkBackendJob	*job);
gboolean	 pk_backend_job_use_background		(PkBackendJob	*job);

/* transaction vfuncs */
typedef void	 (*PkBackendJobVFunc)			(PkBackendJob	*job,
							 gpointer	 object,
							 gpointer	 user_data);
void		 pk_backend_job_set_vfunc		(PkBackendJob	*job,
							 PkBackendJobSignal signal_kind,
							 PkBackendJobVFunc vfunc,
							 gpointer	 user_data);
gboolean	 pk_backend_job_get_vfunc_enabled	(PkBackendJob	*job,
							 PkBackendJobSignal signal_kind);

/* thread helpers */
typedef void	(*PkBackendJobThreadFunc)		(PkBackendJob	*job,
							 GVariant	*params,
							 gpointer	 user_data);
gboolean	 pk_backend_job_thread_create		(PkBackendJob	*job,
							 PkBackendJobThreadFunc func,
							 gpointer	 user_data,
							 GDestroyNotify destroy_func);

/* signal helpers */
void		 pk_backend_job_finished		(PkBackendJob	*job);
void		 pk_backend_job_package			(PkBackendJob	*job,
							 PkInfoEnum	 info,
							 const gchar	*package_id,
							 const gchar	*summary);
void		 pk_backend_job_repo_detail		(PkBackendJob	*job,
							 const gchar	*repo_id,
							 const gchar	*description,
							 gboolean	 enabled);
void		 pk_backend_job_update_detail		(PkBackendJob	*job,
							 const gchar	*package_id,
							 gchar		**updates,
							 gchar		**obsoletes,
							 gchar		**vendor_urls,
							 gchar		**bugzilla_urls,
							 gchar		**cve_urls,
							 PkRestartEnum	 restart,
							 const gchar	*update_text,
							 const gchar	*changelog,
							 PkUpdateStateEnum state,
							 const gchar	*issued,
							 const gchar	*updated);
void		 pk_backend_job_require_restart		(PkBackendJob	*job,
							 PkRestartEnum	 restart,
							 const gchar	*package_id);
void		 pk_backend_job_details			(PkBackendJob	*job,
							 const gchar	*package_id,
							 const gchar	*license,
							 PkGroupEnum	 group,
							 const gchar	*description,
							 const gchar	*url,
							 gulong	  size);
void	 	 pk_backend_job_files 			(PkBackendJob	*job,
							 const gchar	*package_id,
							 gchar	 	**files);
void	 	 pk_backend_job_distro_upgrade		(PkBackendJob	*job,
							 PkDistroUpgradeEnum type,
							 const gchar 	*name,
							 const gchar 	*summary);
void		 pk_backend_job_error_code		(PkBackendJob	*job,
							 PkErrorEnum	 code,
							 const gchar	*details, ...)
							 G_GNUC_PRINTF(3,4);
void		 pk_backend_job_repo_signature_required	(PkBackendJob	*job,
							 const gchar	*package_id,
							 const gchar    *repository_name,
							 const gchar    *key_url,
							 const gchar    *key_userid,
							 const gchar    *key_id,
							 const gchar    *key_fingerprint,
							 const gchar    *key_timestamp,
							 PkSigTypeEnum   type);
void		 pk_backend_job_eula_required		(PkBackendJob	*job,
							 const gchar	*eula_id,
							 const gchar    *package_id,
							 const gchar    *vendor_name,
							 const gchar    *license_agreement);
void		 pk_backend_job_media_change_required	(PkBackendJob	*job,
							 PkMediaTypeEnum media_type,
							 const gchar    *media_id,
							 const gchar    *media_text);
void		 pk_backend_job_category		(PkBackendJob	*job,
							 const gchar	*parent_id,
							 const gchar	*cat_id,
							 const gchar    *name,
							 const gchar    *summary,
							 const gchar    *icon);
void		 pk_backend_job_set_status		(PkBackendJob	*job,
							 PkStatusEnum	 status);
void		 pk_backend_job_set_allow_cancel	(PkBackendJob	*job,
							 gboolean	 allow_cancel);
void		 pk_backend_job_set_percentage		(PkBackendJob	*job,
							 guint		 percentage);
void		 pk_backend_job_set_item_progress	(PkBackendJob	*job,
							 const gchar	*package_id,
							 PkStatusEnum	 status,
							 guint		 percentage);
void		 pk_backend_job_set_speed		(PkBackendJob	*job,
							 guint		 speed);
void		 pk_backend_job_set_download_size_remaining (PkBackendJob	*job,
							 guint64	 download_size_remaining);
void		 pk_backend_job_set_started		(PkBackendJob *job,
							 gboolean started);
gboolean	 pk_backend_job_get_started		(PkBackendJob *job);

G_END_DECLS

#endif /* __PK_BACKEND_JOB_H */

