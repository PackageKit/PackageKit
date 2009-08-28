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
 * SECTION:pk-results
 * @short_description: An abstract results GObject
 */

#ifndef __PK_RESULTS_H
#define __PK_RESULTS_H

#include <glib-object.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-package-sack.h>

G_BEGIN_DECLS

#define PK_TYPE_RESULTS		(pk_results_get_type ())
#define PK_RESULTS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_RESULTS, PkResults))
#define PK_RESULTS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_RESULTS, PkResultsClass))
#define PK_IS_RESULTS(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_RESULTS))
#define PK_IS_RESULTS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_RESULTS))
#define PK_RESULTS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_RESULTS, PkResultsClass))
#define PK_RESULTS_ERROR	(pk_results_error_quark ())
#define PK_RESULTS_TYPE_ERROR	(pk_results_error_get_type ())

typedef struct _PkResultsPrivate	PkResultsPrivate;
typedef struct _PkResults		PkResults;
typedef struct _PkResultsClass		PkResultsClass;

struct _PkResults
{
	 GObject		 parent;
	 PkResultsPrivate	*priv;
};

struct _PkResultsClass
{
	GObjectClass	parent_class;
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

/**
 * PkResultItemRequireRestart:
 *
 * Object to represent details about the require_restart.
 **/
typedef struct
{
	PkRestartEnum			 restart;
	gchar				*package_id;
} PkResultItemRequireRestart;

/**
 * PkResultItemTransaction:
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
} PkResultItemTransaction;

/**
 * PkResultItemDetails:
 *
 * Object to represent details about the update.
 **/
typedef struct {
	gchar		*package_id;
	gchar		*license;
	PkGroupEnum	 group_enum;
	gchar		*description;
	gchar		*url;
	guint64		 size;
} PkResultItemDetails;

/**
 * PkResultItemUpdateDetail:
 *
 * Object to represent details about the update.
 **/
typedef struct {
	gchar		*package_id;
	gchar		*updates;
	gchar		*obsoletes;
	gchar		*vendor_url;
	gchar		*bugzilla_url;
	gchar		*cve_url;
	PkRestartEnum	 restart_enum;
	gchar		*update_text;
	gchar		*changelog;
	PkUpdateStateEnum state_enum;
	GDate		*issued;
	GDate		*updated;
} PkResultItemUpdateDetail;

/**
 * PkResultItemPackage:
 *
 * Object to represent details about a package.
 **/
typedef struct {
	PkInfoEnum	 info_enum;
	gchar		*package_id;
	gchar		*summary;
} PkResultItemPackage;

/**
 * PkResultItemDistroUpgrade:
 *
 * Object to represent details about the distribution update.
 **/
typedef struct
{
	PkUpdateStateEnum		 state;
	gchar				*name;
	gchar				*summary;
} PkResultItemDistroUpgrade;

/**
 * PkResultItemCategory:
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
} PkResultItemCategory;

/**
 * PkResultItemFiles:
 *
 * Object to represent details about the files.
 **/
typedef struct
{
	gchar				*package_id;
	gchar				**files;
} PkResultItemFiles;

/**
 * PkResultItemRepoSignatureRequired:
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
} PkResultItemRepoSignatureRequired;

/**
 * PkResultItemEulaRequired:
 *
 * Object to represent details about the EULA request.
 **/
typedef struct
{
	gchar				*eula_id;
	gchar				*package_id;
	gchar				*vendor_name;
	gchar				*license_agreement;
} PkResultItemEulaRequired;

/**
 * PkResultItemMediaChangeRequired:
 *
 * Object to represent details about the media change request.
 **/
typedef struct
{
	PkMediaTypeEnum			 media_type;
	gchar				*media_id;
	gchar				*media_text;
} PkResultItemMediaChangeRequired;

/**
 * PkResultItemRepoDetail:
 *
 * Object to represent details about the remote repository.
 **/
typedef struct
{
	gchar				*repo_id;
	gchar				*description;
	gboolean			 enabled;
} PkResultItemRepoDetail;

/**
 * PkResultItemErrorCode:
 *
 * Object to represent details about the error code.
 **/
typedef struct
{
	PkErrorCodeEnum			 code;
	gchar				*details;
} PkResultItemErrorCode;

/**
 * PkResultItemMessage:
 *
 * Object to represent details about the message.
 **/
typedef struct
{
	PkMessageEnum			 message;
	gchar				*details;
} PkResultItemMessage;

GQuark		 pk_results_error_quark			(void);
GType		 pk_results_get_type		  	(void);
PkResults	*pk_results_new				(void);

/* set */
gboolean	 pk_results_set_exit_code		(PkResults	*results,
							 PkExitEnum	 exit_enum);

/* add */
gboolean	 pk_results_add_package			(PkResults		*results,
							 PkInfoEnum		 info_enum,
							 const gchar		*package_id,
							 const gchar		*summary);
gboolean	 pk_results_add_details			(PkResults		*results,
							 const gchar		*package_id,
							 const gchar		*license,
							 PkGroupEnum		 group_enum,
							 const gchar		*description,
							 const gchar		*url,
							 guint64		 size);
gboolean	 pk_results_add_update_detail		(PkResults		*results,
							 const gchar		*package_id,
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
gboolean	 pk_results_add_category		(PkResults		*results,
							 const gchar		*parent_id,
							 const gchar		*cat_id,
							 const gchar		*name,
							 const gchar		*summary,
							 const gchar		*icon);
gboolean	 pk_results_add_distro_upgrade		(PkResults		*results,
							 PkUpdateStateEnum	 state_enum,
							 const gchar		*name,
							 const gchar		*summary);
gboolean	 pk_results_add_require_restart		(PkResults		*results,
							 PkRestartEnum		 restart_enum,
							 const gchar		*package_id);
gboolean	 pk_results_add_transaction		(PkResults		*results,
							 const gchar		*tid,
							 const gchar		*timespec,
							 gboolean		 succeeded,
							 PkRoleEnum		 role_enum,
							 guint			 duration,
							 const gchar		*data,
							 guint			 uid,
							 const gchar		*cmdline);
gboolean	 pk_results_add_files 			(PkResults		*results,
							 const gchar		*package_id,
							 gchar			**files);
gboolean	 pk_results_add_repo_signature_required	(PkResults		*results,
							 const gchar		*package_id,
							 const gchar		*repository_name,
							 const gchar		*key_url,
							 const gchar		*key_userid,
							 const gchar		*key_id,
							 const gchar		*key_fingerprint,
							 const gchar		*key_timestamp,
							 PkSigTypeEnum		 type_enum);
gboolean	 pk_results_add_eula_required		(PkResults		*results,
							 const gchar		*eula_id,
							 const gchar		*package_id,
							 const gchar		*vendor_name,
							 const gchar		*license_agreement);
gboolean	 pk_results_add_media_change_required	(PkResults		*results,
							 PkMediaTypeEnum	 media_type_enum,
							 const gchar		*media_id,
							 const gchar		*media_text);
gboolean	 pk_results_add_repo_detail 		(PkResults		*results,
							 const gchar		*repo_id,
							 const gchar		*description,
							 gboolean		 enabled);
gboolean	 pk_results_add_error_code 		(PkResults		*results,
							 PkErrorCodeEnum	 code_enum,
							 const gchar		*details);
gboolean	 pk_results_add_message 		(PkResults		*results,
							 PkMessageEnum		 message_enum,
							 const gchar		*details);

/* get single data */
PkExitEnum	 pk_results_get_exit_code		(PkResults		*results);
PkPackageSack	*pk_results_get_package_sack		(PkResults		*results);

/* get array objects */
GPtrArray	*pk_results_get_package_array		(PkResults		*results);
GPtrArray	*pk_results_get_details_array		(PkResults		*results);
GPtrArray	*pk_results_get_update_detail_array	(PkResults		*results);
GPtrArray	*pk_results_get_category_array		(PkResults		*results);
GPtrArray	*pk_results_get_distro_upgrade_array	(PkResults		*results);
GPtrArray	*pk_results_get_require_restart_array	(PkResults		*results);
GPtrArray	*pk_results_get_transaction_array	(PkResults		*results);
GPtrArray	*pk_results_get_files_array		(PkResults		*results);
GPtrArray	*pk_results_get_repo_signature_required_array (PkResults	*results);
GPtrArray	*pk_results_get_eula_required_array	(PkResults		*results);
GPtrArray	*pk_results_get_media_change_required_array (PkResults		*results);
GPtrArray	*pk_results_get_repo_detail_array	(PkResults		*results);
GPtrArray	*pk_results_get_error_code_array	(PkResults		*results);
GPtrArray	*pk_results_get_message_array		(PkResults		*results);

G_END_DECLS

#endif /* __PK_RESULTS_H */

