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

/**
 * SECTION:pk-item
 * @short_description: Abstract items
 */

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
	PkRestartEnum			 restart;
	gchar				*package_id;
} PkItemRequireRestart;

/**
 * PkItemTransaction:
 *
 * Object to represent details about the transaction.
 **/
typedef struct
{
	gchar				*tid;
	gchar				*timespec;
	gboolean			 succeeded;
	PkRoleEnum			 role;
	guint				 duration;
	gchar				*data;
	guint				 uid;
	gchar				*cmdline;
} PkItemTransaction;

/**
 * PkItemDetails:
 *
 * Object to represent details about the update.
 **/
typedef struct {
	gchar				*package_id;
	gchar				*license;
	PkGroupEnum			 group_enum;
	gchar				*description;
	gchar				*url;
	guint64				 size;
} PkItemDetails;

/**
 * PkItemUpdateDetail:
 *
 * Object to represent details about the update.
 **/
typedef struct {
	gchar				*package_id;
	gchar				*updates;
	gchar				*obsoletes;
	gchar				*vendor_url;
	gchar				*bugzilla_url;
	gchar				*cve_url;
	PkRestartEnum			 restart_enum;
	gchar				*update_text;
	gchar				*changelog;
	PkUpdateStateEnum		 state_enum;
	GDate				*issued;
	GDate				*updated;
} PkItemUpdateDetail;

/**
 * PkItemPackage:
 *
 * Object to represent details about a package.
 **/
typedef struct {
	PkInfoEnum			 info_enum;
	gchar				*package_id;
	gchar				*summary;
} PkItemPackage;

/**
 * PkItemDistroUpgrade:
 *
 * Object to represent details about the distribution update.
 **/
typedef struct
{
	PkUpdateStateEnum		 state;
	gchar				*name;
	gchar				*summary;
} PkItemDistroUpgrade;

/**
 * PkItemCategory:
 *
 * Object to represent details about the category.
 **/
typedef struct
{
	gchar				*parent_id;
	gchar				*cat_id;
	gchar				*name;
	gchar				*summary;
	gchar				*icon;
} PkItemCategory;

/**
 * PkItemFiles:
 *
 * Object to represent details about the files.
 **/
typedef struct
{
	gchar				*package_id;
	gchar				**files;
} PkItemFiles;

/**
 * PkItemRepoSignatureRequired:
 *
 * Object to represent details about the repository signature request.
 **/
typedef struct
{
	gchar				*package_id;
	gchar				*repository_name;
	gchar				*key_url;
	gchar				*key_userid;
	gchar				*key_id;
	gchar				*key_fingerprint;
	gchar				*key_timestamp;
	PkSigTypeEnum			 type;
} PkItemRepoSignatureRequired;

/**
 * PkItemEulaRequired:
 *
 * Object to represent details about the EULA request.
 **/
typedef struct
{
	gchar				*eula_id;
	gchar				*package_id;
	gchar				*vendor_name;
	gchar				*license_agreement;
} PkItemEulaRequired;

/**
 * PkItemMediaChangeRequired:
 *
 * Object to represent details about the media change request.
 **/
typedef struct
{
	PkMediaTypeEnum			 media_type;
	gchar				*media_id;
	gchar				*media_text;
} PkItemMediaChangeRequired;

/**
 * PkItemRepoDetail:
 *
 * Object to represent details about the remote repository.
 **/
typedef struct
{
	gchar				*repo_id;
	gchar				*description;
	gboolean			 enabled;
} PkItemRepoDetail;

/**
 * PkItemErrorCode:
 *
 * Object to represent details about the error code.
 **/
typedef struct
{
	PkErrorCodeEnum			 code;
	gchar				*details;
} PkItemErrorCode;

/**
 * PkItemMessage:
 *
 * Object to represent details about the message.
 **/
typedef struct
{
	PkMessageEnum			 message;
	gchar				*details;
} PkItemMessage;

//GPtrArray	*pk_results_get_message_array		(PkResults		*results);
void		 pk_item_test				(gpointer		 user_data);
void		 pk_item_package_unref		(PkItemPackage	*item);
void		 pk_item_details_unref		(PkItemDetails	*item);
void		 pk_item_update_detail_unref		(PkItemUpdateDetail	*item);
void		 pk_item_category_unref		(PkItemCategory	*item);
void		 pk_item_distro_upgrade_unref		(PkItemDistroUpgrade	*item);
void		 pk_item_require_restart_unref		(PkItemRequireRestart	*item);
void		 pk_item_transaction_unref		(PkItemTransaction	*item);
void		 pk_item_files_unref		(PkItemFiles	*item);
void		 pk_item_repo_signature_required_unref		(PkItemRepoSignatureRequired	*item);
void		 pk_item_eula_required_unref		(PkItemEulaRequired	*item);
void		 pk_item_media_change_required_unref		(PkItemMediaChangeRequired	*item);
void		 pk_item_repo_detail_unref		(PkItemRepoDetail	*item);
void		 pk_item_error_code_unref		(PkItemErrorCode	*item);
void		 pk_item_message_unref		(PkItemMessage	*item);

G_END_DECLS

#endif /* __PK_ITEM_H */

