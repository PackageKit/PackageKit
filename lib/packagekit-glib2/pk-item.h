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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_ITEM_H
#define __PK_ITEM_H

#include <glib.h>
#include <packagekit-glib2/pk-enum.h>

G_BEGIN_DECLS

/**
 * PkItemRequireRestart:
 *
 * Object to represent details about the require_restart.
 **/
typedef struct
{
	guint				 refcount;
	PkRestartEnum			 restart;
	gchar				*package_id;
	gpointer			 user_data;
} PkItemRequireRestart;

/**
 * PkItemTransaction:
 *
 * Object to represent details about the transaction.
 **/
typedef struct
{
	guint				 refcount;
	gchar				*tid;
	gchar				*timespec;
	gboolean			 succeeded;
	PkRoleEnum			 role;
	guint				 duration;
	gchar				*data;
	guint				 uid;
	gchar				*cmdline;
	gpointer			 user_data;
} PkItemTransaction;

/**
 * PkItemDetails:
 *
 * Object to represent details about the update.
 **/
typedef struct {
	guint				 refcount;
	gchar				*package_id;
	gchar				*license;
	PkGroupEnum			 group;
	gchar				*description;
	gchar				*url;
	guint64				 size;
	gpointer			 user_data;
} PkItemDetails;

/**
 * PkItemUpdateDetail:
 *
 * Object to represent details about the update.
 **/
typedef struct {
	guint				 refcount;
	gchar				*package_id;
	gchar				*updates;
	gchar				*obsoletes;
	gchar				*vendor_url;
	gchar				*bugzilla_url;
	gchar				*cve_url;
	PkRestartEnum			 restart;
	gchar				*update_text;
	gchar				*changelog;
	PkUpdateStateEnum		 state;
	GDate				*issued;
	GDate				*updated;
	gpointer			 user_data;
} PkItemUpdateDetail;

/**
 * PkItemPackage:
 *
 * Object to represent details about a package.
 **/
typedef struct {
	guint				 refcount;
	PkInfoEnum			 info;
	gchar				*package_id;
	gchar				*summary;
	gpointer			 user_data;
} PkItemPackage;

/**
 * PkItemDistroUpgrade:
 *
 * Object to represent details about the distribution update.
 **/
typedef struct
{
	guint				 refcount;
	PkUpdateStateEnum		 state;
	gchar				*name;
	gchar				*summary;
	gpointer			 user_data;
} PkItemDistroUpgrade;

/**
 * PkItemCategory:
 *
 * Object to represent details about the category.
 **/
typedef struct
{
	guint				 refcount;
	gchar				*parent_id;
	gchar				*cat_id;
	gchar				*name;
	gchar				*summary;
	gchar				*icon;
	gpointer			 user_data;
} PkItemCategory;

/**
 * PkItemFiles:
 *
 * Object to represent details about the files.
 **/
typedef struct
{
	guint				 refcount;
	gchar				*package_id;
	gchar				**files;
	gpointer			 user_data;
} PkItemFiles;

/**
 * PkItemRepoSignatureRequired:
 *
 * Object to represent details about the repository signature request.
 **/
typedef struct
{
	guint				 refcount;
	gchar				*package_id;
	gchar				*repository_name;
	gchar				*key_url;
	gchar				*key_userid;
	gchar				*key_id;
	gchar				*key_fingerprint;
	gchar				*key_timestamp;
	PkSigTypeEnum			 type;
	gpointer			 user_data;
} PkItemRepoSignatureRequired;

/**
 * PkItemEulaRequired:
 *
 * Object to represent details about the EULA request.
 **/
typedef struct
{
	guint				 refcount;
	gchar				*eula_id;
	gchar				*package_id;
	gchar				*vendor_name;
	gchar				*license_agreement;
	gpointer			 user_data;
} PkItemEulaRequired;

/**
 * PkItemMediaChangeRequired:
 *
 * Object to represent details about the media change request.
 **/
typedef struct
{
	guint				 refcount;
	PkMediaTypeEnum			 media_type;
	gchar				*media_id;
	gchar				*media_text;
	gpointer			 user_data;
} PkItemMediaChangeRequired;

/**
 * PkItemRepoDetail:
 *
 * Object to represent details about the remote repository.
 **/
typedef struct
{
	guint				 refcount;
	gchar				*repo_id;
	gchar				*description;
	gboolean			 enabled;
	gpointer			 user_data;
} PkItemRepoDetail;

/**
 * PkItemErrorCode:
 *
 * Object to represent details about the error code.
 **/
typedef struct
{
	guint				 refcount;
	PkErrorCodeEnum			 code;
	gchar				*details;
	gpointer			 user_data;
} PkItemErrorCode;

/**
 * PkItemMessage:
 *
 * Object to represent details about the message.
 **/
typedef struct
{
	guint				 refcount;
	PkMessageEnum			 type;
	gchar				*details;
	gpointer			 user_data;
} PkItemMessage;

void			 pk_item_test				(gpointer		 user_data);

/* refcount */
PkItemPackage		*pk_item_package_ref			(PkItemPackage		*item);
PkItemPackage		*pk_item_package_unref			(PkItemPackage		*item);
PkItemDetails		*pk_item_details_ref			(PkItemDetails		*item);
PkItemDetails		*pk_item_details_unref			(PkItemDetails		*item);
PkItemUpdateDetail	*pk_item_update_detail_ref		(PkItemUpdateDetail	*item);
PkItemUpdateDetail	*pk_item_update_detail_unref		(PkItemUpdateDetail	*item);
PkItemCategory		*pk_item_category_ref			(PkItemCategory		*item);
PkItemCategory		*pk_item_category_unref			(PkItemCategory		*item);
PkItemDistroUpgrade	*pk_item_distro_upgrade_ref		(PkItemDistroUpgrade	*item);
PkItemDistroUpgrade	*pk_item_distro_upgrade_unref		(PkItemDistroUpgrade	*item);
PkItemRequireRestart	*pk_item_require_restart_ref		(PkItemRequireRestart	*item);
PkItemRequireRestart	*pk_item_require_restart_unref		(PkItemRequireRestart	*item);
PkItemTransaction	*pk_item_transaction_ref		(PkItemTransaction	*item);
PkItemTransaction	*pk_item_transaction_unref		(PkItemTransaction	*item);
PkItemFiles		*pk_item_files_ref			(PkItemFiles		*item);
PkItemFiles		*pk_item_files_unref			(PkItemFiles		*item);
PkItemRepoSignatureRequired	*pk_item_repo_signature_required_ref	(PkItemRepoSignatureRequired	*item);
PkItemRepoSignatureRequired	*pk_item_repo_signature_required_unref	(PkItemRepoSignatureRequired	*item);
PkItemEulaRequired	*pk_item_eula_required_ref		(PkItemEulaRequired	*item);
PkItemEulaRequired	*pk_item_eula_required_unref		(PkItemEulaRequired	*item);
PkItemMediaChangeRequired	*pk_item_media_change_required_ref	(PkItemMediaChangeRequired	*item);
PkItemMediaChangeRequired	*pk_item_media_change_required_unref	(PkItemMediaChangeRequired	*item);
PkItemRepoDetail	*pk_item_repo_detail_ref		(PkItemRepoDetail	*item);
PkItemRepoDetail	*pk_item_repo_detail_unref		(PkItemRepoDetail	*item);
PkItemErrorCode		*pk_item_error_code_ref			(PkItemErrorCode	*item);
PkItemErrorCode		*pk_item_error_code_unref		(PkItemErrorCode	*item);
PkItemMessage		*pk_item_message_ref			(PkItemMessage		*item);
PkItemMessage		*pk_item_message_unref			(PkItemMessage		*item);

/* create */
PkItemPackage		*pk_item_package_new			(PkInfoEnum		 info_enum,
								 const gchar		*package_id,
								 const gchar		*summary);
PkItemDetails		*pk_item_details_new			(const gchar		*package_id,
								 const gchar		*license,
								 PkGroupEnum		 group_enum,
								 const gchar		*description,
								 const gchar		*url,
								 guint64		 size);
PkItemUpdateDetail	*pk_item_update_detail_new		(const gchar		*package_id,
								 const gchar		*updates,
								 const gchar		*obsoletes,
								 const gchar		*vendor_url,
								 const gchar		*bugzilla_url,
								 const gchar		*cve_url,
								 PkRestartEnum		 restart_enum,
								 const gchar		*update_text,
								 const gchar		*changelog,
								 PkUpdateStateEnum	 state_enum,
								 GDate			*issued,
								 GDate			*updated);
PkItemCategory		*pk_item_category_new			(const gchar		*parent_id,
								 const gchar		*cat_id,
								 const gchar		*name,
								 const gchar		*summary,
								 const gchar		*icon);
PkItemDistroUpgrade	*pk_item_distro_upgrade_new		(PkUpdateStateEnum	 state_enum,
								 const gchar		*name,
								 const gchar		*summary);
PkItemRequireRestart	*pk_item_require_restart_new		(PkRestartEnum		 restart_enum,
								 const gchar		*package_id);
PkItemTransaction	*pk_item_transaction_new		(const gchar		*tid,
								 const gchar		*timespec,
								 gboolean		 succeeded,
								 PkRoleEnum		 role_enum,
								 guint			 duration,
								 const gchar		*data,
								 guint			 uid,
								 const gchar		*cmdline);
PkItemFiles		*pk_item_files_new			(const gchar		*package_id,
								 gchar			**files);
PkItemRepoSignatureRequired *pk_item_repo_signature_required_new (const gchar		*package_id,
								 const gchar		*repository_name,
								 const gchar		*key_url,
								 const gchar		*key_userid,
								 const gchar		*key_id,
								 const gchar		*key_fingerprint,
								 const gchar		*key_timestamp,
								 PkSigTypeEnum		 type_enum);
PkItemEulaRequired	*pk_item_eula_required_new		(const gchar		*eula_id,
								 const gchar		*package_id,
								 const gchar		*vendor_name,
								 const gchar		*license_agreement);
PkItemMediaChangeRequired *pk_item_media_change_required_new	(PkMediaTypeEnum	 media_type_enum,
								 const gchar		*media_id,
								 const gchar		*media_text);
PkItemRepoDetail	*pk_item_repo_detail_new		(const gchar		*repo_id,
								 const gchar		*description,
								 gboolean		 enabled);
PkItemErrorCode		*pk_item_error_code_new			(PkErrorCodeEnum	 code_enum,
								 const gchar		*details);
PkItemMessage		*pk_item_message_new			(PkMessageEnum		 type_enum,
								 const gchar		*details);

G_END_DECLS

#endif /* __PK_ITEM_H */

