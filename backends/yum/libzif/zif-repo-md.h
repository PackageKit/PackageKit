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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#if !defined (__ZIF_H_INSIDE__) && !defined (ZIF_COMPILATION)
#error "Only <zif.h> can be included directly."
#endif

#ifndef __ZIF_REPO_MD_H
#define __ZIF_REPO_MD_H

#include <glib-object.h>
#include <gio/gio.h>

#include "zif-repo-md.h"
#include "zif-completion.h"
#include "zif-store-remote.h"

G_BEGIN_DECLS

#define ZIF_TYPE_REPO_MD		(zif_repo_md_get_type ())
#define ZIF_REPO_MD(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_REPO_MD, ZifRepoMd))
#define ZIF_REPO_MD_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_REPO_MD, ZifRepoMdClass))
#define ZIF_IS_REPO_MD(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_REPO_MD))
#define ZIF_IS_REPO_MD_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_REPO_MD))
#define ZIF_REPO_MD_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_REPO_MD, ZifRepoMdClass))
#define ZIF_REPO_MD_ERROR		(zif_repo_md_error_quark ())

typedef struct _ZifRepoMd		ZifRepoMd;
typedef struct _ZifRepoMdPrivate	ZifRepoMdPrivate;
typedef struct _ZifRepoMdClass		ZifRepoMdClass;

struct _ZifRepoMd
{
	GObject			 parent;
	ZifRepoMdPrivate	*priv;
};

struct _ZifRepoMdClass
{
	GObjectClass				 parent_class;
	/* vtable */
	gboolean	 (*load)		(ZifRepoMd		*md,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	gboolean	 (*unload)		(ZifRepoMd		*md,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
};

/* types of metadata */
typedef enum {
	ZIF_REPO_MD_TYPE_PRIMARY,
	ZIF_REPO_MD_TYPE_PRIMARY_DB,
	ZIF_REPO_MD_TYPE_FILELISTS,
	ZIF_REPO_MD_TYPE_FILELISTS_DB,
	ZIF_REPO_MD_TYPE_OTHER,
	ZIF_REPO_MD_TYPE_OTHER_DB,
	ZIF_REPO_MD_TYPE_COMPS,
	ZIF_REPO_MD_TYPE_COMPS_XML,
	ZIF_REPO_MD_TYPE_METALINK,
	ZIF_REPO_MD_TYPE_MIRRORLIST,
	ZIF_REPO_MD_TYPE_PRESTODELTA,
	ZIF_REPO_MD_TYPE_UPDATEINFO,
	ZIF_REPO_MD_TYPE_UNKNOWN
} ZifRepoMdType;

typedef enum {
	ZIF_REPO_MD_ERROR_FAILED,
	ZIF_REPO_MD_ERROR_NO_SUPPORT,
	ZIF_REPO_MD_ERROR_FAILED_TO_LOAD,
	ZIF_REPO_MD_ERROR_FAILED_AS_OFFLINE,
	ZIF_REPO_MD_ERROR_FAILED_DOWNLOAD,
	ZIF_REPO_MD_ERROR_BAD_SQL,
	ZIF_REPO_MD_ERROR_LAST
} ZifRepoMdError;

GType		 zif_repo_md_get_type			(void);
GQuark		 zif_repo_md_error_quark			(void);
ZifRepoMd	*zif_repo_md_new			(void);

/* setters */
gboolean	 zif_repo_md_set_mdtype			(ZifRepoMd	*md,
							 ZifRepoMdType	 type);
gboolean	 zif_repo_md_set_store_remote		(ZifRepoMd	*md,
							 ZifStoreRemote	*remote);
gboolean	 zif_repo_md_set_id			(ZifRepoMd	*md,
							 const gchar	*id);
gboolean	 zif_repo_md_set_filename		(ZifRepoMd	*md,
							 const gchar	*filename);
gboolean	 zif_repo_md_set_timestamp		(ZifRepoMd	*md,
							 guint		 timestamp);
gboolean	 zif_repo_md_set_location		(ZifRepoMd	*md,
							 const gchar	*location);
gboolean	 zif_repo_md_set_checksum		(ZifRepoMd	*md,
							 const gchar	*checksum);
gboolean	 zif_repo_md_set_checksum_uncompressed	(ZifRepoMd	*md,
							 const gchar	*checksum_uncompressed);
gboolean	 zif_repo_md_set_checksum_type		(ZifRepoMd	*md,
							 GChecksumType	 checksum_type);
const gchar	*zif_repo_md_type_to_text		(ZifRepoMdType	 type);

/* getters */
const gchar	*zif_repo_md_get_id			(ZifRepoMd	*md);
ZifRepoMdType	 zif_repo_md_get_mdtype			(ZifRepoMd	*md);
const gchar	*zif_repo_md_get_filename		(ZifRepoMd	*md);
const gchar	*zif_repo_md_get_filename_uncompressed	(ZifRepoMd	*md);
guint		 zif_repo_md_get_age			(ZifRepoMd	*md,
							 GError		**error);
const gchar	*zif_repo_md_get_location		(ZifRepoMd	*md);

/* actions */
gboolean	 zif_repo_md_load			(ZifRepoMd	*md,
							 GCancellable	*cancellable,
							 ZifCompletion	*completion,
							 GError		**error);
gboolean	 zif_repo_md_unload			(ZifRepoMd	*md,
							 GCancellable	*cancellable,
							 ZifCompletion	*completion,
							 GError		**error);
gboolean	 zif_repo_md_clean			(ZifRepoMd	*md,
							 GError		**error);
gboolean	 zif_repo_md_file_check			(ZifRepoMd	*md,
							 gboolean	 use_uncompressed,
							 GError		**error);

G_END_DECLS

#endif /* __ZIF_REPO_MD_H */

