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

#ifndef __PK_CLIENT_H
#define __PK_CLIENT_H

#include <glib-object.h>
#include "pk-enum.h"
#include "pk-package-list.h"
#include "pk-update-detail-obj.h"
#include "pk-details-obj.h"

G_BEGIN_DECLS

#define PK_TYPE_CLIENT		(pk_client_get_type ())
#define PK_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CLIENT, PkClient))
#define PK_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CLIENT, PkClientClass))
#define PK_IS_CLIENT(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CLIENT))
#define PK_IS_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CLIENT))
#define PK_CLIENT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CLIENT, PkClientClass))
#define PK_CLIENT_ERROR	 	(pk_client_error_quark ())
#define PK_CLIENT_TYPE_ERROR	(pk_client_error_get_type ())

/**
 * PK_CLIENT_PERCENTAGE_INVALID:
 *
 * The unknown percentage value
 */
#define PK_CLIENT_PERCENTAGE_INVALID	101

/**
 * PkClientError:
 * @PK_CLIENT_ERROR_FAILED: the transaction failed for an unknown reason
 * @PK_CLIENT_ERROR_NO_TID: the transaction id was not pre-allocated (internal error)
 * @PK_CLIENT_ERROR_ALREADY_TID: the transaction id has already been used (internal error)
 * @PK_CLIENT_ERROR_ROLE_UNKNOWN: the role was not set (internal error)
 * @PK_CLIENT_ERROR_INVALID_PACKAGEID: the package_id is invalid
 *
 * Errors that can be thrown
 */
typedef enum
{
	PK_CLIENT_ERROR_FAILED,
	PK_CLIENT_ERROR_FAILED_AUTH,
	PK_CLIENT_ERROR_NO_TID,
	PK_CLIENT_ERROR_ALREADY_TID,
	PK_CLIENT_ERROR_ROLE_UNKNOWN,
	PK_CLIENT_ERROR_INVALID_PACKAGEID
} PkClientError;

typedef struct _PkClientPrivate		PkClientPrivate;
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
	/* Signals */
	void		(* status_changed)		(PkClient	*client,
							 PkStatusEnum	 status);
	void		(* progress_changed)		(PkClient	*client,
							 guint		 percentage,
							 guint		 subpercentage,
							 guint		 elapsed,
							 guint		 remaining);
	void		(* package)			(PkClient	*client,
							 PkPackageObj	*obj);
	void		(* transaction)			(PkClient	*client,
							 const gchar	*tid,
							 const gchar	*timespec,
							 gboolean	 succeeded,
							 PkRoleEnum	 role,
							 guint		 duration,
							 const gchar	*data);
	void		(* update_detail)		(PkClient	*client,
							 PkUpdateDetailObj	*update_detail);
	void		(* details)			(PkClient	*client,
							 PkDetailsObj	*package_detail);
	void		(* files)			(PkClient	*client,
							 const gchar	*package_id,
							 const gchar	*filelist);
	void		(* repo_signature_required)	(PkClient	*client,
							 const gchar	*package_id,
							 const gchar	*repository_name,
							 const gchar	*key_url,
							 const gchar	*key_userid,
							 const gchar	*key_id,
							 const gchar	*key_fingerprint,
							 const gchar	*key_timestamp,
							 PkSigTypeEnum	 type);
	void		(* eula_required)		(PkClient	*client,
							 const gchar	*eula_id,
							 const gchar	*package_id,
							 const gchar	*vendor_name,
							 const gchar	*license_agreement);
	void		(* repo_detail)			(PkClient	*client,
							 const gchar	*repo_id,
							 const gchar	*description,
							 gboolean	 enabled);
	void		(* error_code)			(PkClient	*client,
							 PkErrorCodeEnum code,
							 const gchar	*details);
	void		(* require_restart)		(PkClient	*client,
							 PkRestartEnum	 restart,
							 const gchar	*details);
	void		(* message)			(PkClient	*client,
							 PkMessageEnum	 message,
							 const gchar	*details);
	void		(* allow_cancel)		(PkClient	*client,
							 gboolean	 allow_cancel);
	void		(* caller_active_changed)	(PkClient	*client,
							 gboolean	 is_active);
	void		(* finished)			(PkClient	*client,
							 PkExitEnum	 exit,
							 guint		 runtime);
	/* Padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GQuark		 pk_client_error_quark			(void);
GType		 pk_client_error_get_type		(void);
gboolean	 pk_client_error_print			(GError		**error);

GType		 pk_client_get_type			(void) G_GNUC_CONST;
PkClient	*pk_client_new				(void);

gboolean	 pk_client_set_tid			(PkClient	*client,
							 const gchar	*tid,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		*pk_client_get_tid			(PkClient	*client);

gboolean	 pk_client_set_use_buffer		(PkClient	*client,
							 gboolean	 use_buffer,
							 GError		**error);
gboolean	 pk_client_set_synchronous		(PkClient	*client,
							 gboolean	 synchronous,
							 GError		**error);
gboolean	 pk_client_get_use_buffer		(PkClient	*client);
gboolean	 pk_client_get_allow_cancel		(PkClient	*client,
							 gboolean	*allow_cancel,
							 GError		**error);

/* general methods */
gboolean	 pk_client_get_status			(PkClient	*client,
							 PkStatusEnum	*status,
							 GError		**error);
gboolean	 pk_client_get_role			(PkClient	*client,
							 PkRoleEnum	*role,
							 gchar		**package_id,
							 GError		**error);
gboolean	 pk_client_get_progress			(PkClient	*client,
							 guint		*percentage,
							 guint		*subpercentage,
							 guint		*elapsed,
							 guint		*remaining,
							 GError		**error);
gboolean	 pk_client_get_package			(PkClient	*client,
							 gchar		**package,
							 GError		**error);
gboolean	 pk_client_cancel			(PkClient	*client,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_download_packages		(PkClient	*client,
							 gchar		**package_ids,
							 const gchar	*directory,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;	
gboolean	 pk_client_get_updates			(PkClient	*client,
							 PkFilterEnum	 filters,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_update_system		(PkClient	*client,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_search_name			(PkClient	*client,
							 PkFilterEnum	 filters,
							 const gchar	*search,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_search_details		(PkClient	*client,
							 PkFilterEnum	 filters,
							 const gchar	*search,
							 GError		**error);
gboolean	 pk_client_search_group			(PkClient	*client,
							 PkFilterEnum	 filters,
							 const gchar	*search,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_search_file			(PkClient	*client,
							 PkFilterEnum	 filters,
							 const gchar	*search,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_get_depends			(PkClient	*client,
							 PkFilterEnum	 filters,
							 gchar		**package_ids,
							 gboolean	 recursive,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_get_packages			(PkClient	*client,
							 PkFilterEnum	 filters,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_get_update_detail		(PkClient	*client,
							 gchar		**package_ids,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_get_requires			(PkClient	*client,
							 PkFilterEnum	 filters,
							 gchar		**package_ids,
							 gboolean	 recursive,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_what_provides		(PkClient	*client,
							 PkFilterEnum	 filters,
							 PkProvidesEnum	 provides,
							 const gchar	*search,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_get_details			(PkClient	*client,
							 gchar		**package_ids,
							 GError		**error);
gboolean	 pk_client_get_files			(PkClient	*client,
							 gchar		**package_ids,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_remove_packages		(PkClient	*client,
							 gchar		**package_ids,
							 gboolean	 allow_deps,
							 gboolean	 autoremove,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_refresh_cache		(PkClient	*client,
							 gboolean	 force,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_install_packages		(PkClient	*client,
							 gchar		**package_ids,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_install_signature		(PkClient	*client,
							 PkSigTypeEnum	 type,
							 const gchar	*key_id,
							 const gchar	*package_id,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_update_packages		(PkClient	*client,
							 gchar		**package_ids,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_install_files		(PkClient	*client,
							 gboolean	 trusted,
							 gchar		**files_rel,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_install_file			(PkClient	*client,
							 gboolean	 trusted,
							 const gchar	*file_rel,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_resolve			(PkClient	*client,
							 PkFilterEnum	 filters,
							 gchar		**packages,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_rollback			(PkClient	*client,
							 const gchar	*transaction_id,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_cancel			(PkClient	*client,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_requeue			(PkClient	*client,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_accept_eula			(PkClient	*client,
							 const gchar	*eula_id,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;

/* repo stuff */
gboolean	 pk_client_get_repo_list		(PkClient	*client,
							 PkFilterEnum	 filters,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_repo_enable			(PkClient	*client,
							 const gchar	*repo_id,
							 gboolean	 enabled,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_repo_set_data		(PkClient	*client,
							 const gchar	*repo_id,
							 const gchar	*parameter,
							 const gchar	*value,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;

/* cached stuff */
PkPackageList	*pk_client_get_package_list		(PkClient	*client);
PkRestartEnum	 pk_client_get_require_restart		(PkClient	*client);

/* not job specific */
gboolean	 pk_client_reset			(PkClient	*client,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_client_get_old_transactions		(PkClient	*client,
							 guint		 number,
							 GError		**error);
gboolean	 pk_client_is_caller_active		(PkClient	*client,
							 gboolean	*is_active,
							 GError		**error);

G_END_DECLS

#endif /* __PK_CLIENT_H */
