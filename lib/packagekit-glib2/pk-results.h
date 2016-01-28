/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_RESULTS_H
#define __PK_RESULTS_H

#include <glib-object.h>
#include <packagekit-glib2/pk-bitfield.h>
#include <packagekit-glib2/pk-category.h>
#include <packagekit-glib2/pk-details.h>
#include <packagekit-glib2/pk-distro-upgrade.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-error.h>
#include <packagekit-glib2/pk-eula-required.h>
#include <packagekit-glib2/pk-files.h>
#include <packagekit-glib2/pk-media-change-required.h>
#include <packagekit-glib2/pk-package-sack.h>
#include <packagekit-glib2/pk-repo-detail.h>
#include <packagekit-glib2/pk-repo-signature-required.h>
#include <packagekit-glib2/pk-require-restart.h>
#include <packagekit-glib2/pk-transaction-past.h>
#include <packagekit-glib2/pk-update-detail.h>

G_BEGIN_DECLS

#define PK_TYPE_RESULTS		(pk_results_get_type ())
#define PK_RESULTS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_RESULTS, PkResults))
#define PK_RESULTS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_RESULTS, PkResultsClass))
#define PK_IS_RESULTS(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_RESULTS))
#define PK_IS_RESULTS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_RESULTS))
#define PK_RESULTS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_RESULTS, PkResultsClass))
#define PK_RESULTS_TYPE_ERROR	(pk_results_error_get_type ())

typedef struct _PkResultsPrivate	PkResultsPrivate;
typedef struct _PkResults		PkResults;
typedef struct _PkResultsClass		PkResultsClass;

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkResults, g_object_unref)
#endif

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

GType		 pk_results_get_type		  	(void);
PkResults	*pk_results_new				(void);
void		 pk_results_test			(gpointer		 user_data);

/* set */
gboolean	 pk_results_set_exit_code		(PkResults		*results,
							 PkExitEnum		 exit_enum);
gboolean	 pk_results_set_error_code 		(PkResults		*results,
							 PkError		*item);

/* add */
gboolean	 pk_results_add_package			(PkResults		*results,
							 PkPackage		*item);
gboolean	 pk_results_add_details			(PkResults		*results,
							 PkDetails		*item);
gboolean	 pk_results_add_update_detail		(PkResults		*results,
							 PkUpdateDetail		*item);
gboolean	 pk_results_add_category		(PkResults		*results,
							 PkCategory		*item);
gboolean	 pk_results_add_distro_upgrade		(PkResults		*results,
							 PkDistroUpgrade	*item);
gboolean	 pk_results_add_require_restart		(PkResults		*results,
							 PkRequireRestart	*item);
gboolean	 pk_results_add_transaction		(PkResults		*results,
							 PkTransactionPast	*item);
gboolean	 pk_results_add_files 			(PkResults		*results,
							 PkFiles		*item);
gboolean	 pk_results_add_repo_signature_required	(PkResults		*results,
							 PkRepoSignatureRequired *item);
gboolean	 pk_results_add_eula_required		(PkResults		*results,
							 PkEulaRequired		*item);
gboolean	 pk_results_add_media_change_required	(PkResults		*results,
							 PkMediaChangeRequired	*item);
gboolean	 pk_results_add_repo_detail 		(PkResults		*results,
							 PkRepoDetail		*item);

/* get single data */
PkExitEnum	 pk_results_get_exit_code		(PkResults		*results);
PkPackageSack	*pk_results_get_package_sack		(PkResults		*results);
PkError		*pk_results_get_error_code		(PkResults		*results);
PkRoleEnum	 pk_results_get_role			(PkResults		*results);
PkBitfield	 pk_results_get_transaction_flags	(PkResults		*results);
PkRestartEnum	 pk_results_get_require_restart_worst	(PkResults		*results);

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

G_END_DECLS

#endif /* __PK_RESULTS_H */

