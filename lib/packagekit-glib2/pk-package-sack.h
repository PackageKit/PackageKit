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

#ifndef __PK_PACKAGE_SACK_H
#define __PK_PACKAGE_SACK_H

#include <glib-object.h>
#include <gio/gio.h>
#include <packagekit-glib2/pk-progress.h>
#include <packagekit-glib2/pk-package.h>

G_BEGIN_DECLS

#define PK_TYPE_PACKAGE_SACK		(pk_package_sack_get_type ())
#define PK_PACKAGE_SACK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_PACKAGE_SACK, PkPackageSack))
#define PK_PACKAGE_SACK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_PACKAGE_SACK, PkPackageSackClass))
#define PK_IS_PACKAGE_SACK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_PACKAGE_SACK))
#define PK_IS_PACKAGE_SACK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_PACKAGE_SACK))
#define PK_PACKAGE_SACK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_PACKAGE_SACK, PkPackageSackClass))
#define PK_PACKAGE_SACK_TYPE_ERROR	(pk_package_sack_error_get_type ())

typedef struct _PkPackageSackPrivate	PkPackageSackPrivate;
typedef struct _PkPackageSack		PkPackageSack;
typedef struct _PkPackageSackClass	PkPackageSackClass;
typedef struct _PkPackageSackResults	PkPackageSackResults;

struct _PkPackageSack
{
	 GObject		 parent;
	 PkPackageSackPrivate	*priv;
};

struct _PkPackageSackClass
{
	GObjectClass	parent_class;

	/* signals */
	void		(* changed)			(PkPackageSack	*sack);
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

typedef enum {
	PK_PACKAGE_SACK_SORT_TYPE_NAME,
	PK_PACKAGE_SACK_SORT_TYPE_INFO,
	PK_PACKAGE_SACK_SORT_TYPE_PACKAGE_ID,
	PK_PACKAGE_SACK_SORT_TYPE_SUMMARY,
	PK_PACKAGE_SACK_SORT_TYPE_LAST
} PkPackageSackSortType;

GType		 pk_package_sack_get_type		(void);
PkPackageSack	*pk_package_sack_new			(void);
void		 pk_package_sack_test			(gpointer		 user_data);

typedef gboolean (*PkPackageSackFilterFunc)		(PkPackage		*package,
							 gpointer		 user_data);

/* managing the array */
void		 pk_package_sack_clear			(PkPackageSack		*sack);
gchar		**pk_package_sack_get_ids		(PkPackageSack		*sack);
guint		 pk_package_sack_get_size		(PkPackageSack		*sack);
GPtrArray	*pk_package_sack_get_array		(PkPackageSack		*sack);
void		 pk_package_sack_sort			(PkPackageSack		*sack,
							 PkPackageSackSortType	 type);
gboolean	 pk_package_sack_add_package		(PkPackageSack		*sack,
							 PkPackage		*package);
gboolean	 pk_package_sack_add_package_by_id	(PkPackageSack		*sack,
							 const gchar		*package_id,
							 GError			**error);
gboolean	 pk_package_sack_add_packages_from_file	(PkPackageSack		*sack,
							 GFile			*file,
							 GError			**error);
gboolean	 pk_package_sack_remove_package		(PkPackageSack		*sack,
							 PkPackage		*package);
gboolean	 pk_package_sack_remove_package_by_id	(PkPackageSack		*sack,
							 const gchar		*package_id);
gboolean	 pk_package_sack_remove_by_filter	(PkPackageSack		*sack,
							 PkPackageSackFilterFunc filter_cb,
							 gpointer		 user_data);
PkPackage	*pk_package_sack_find_by_id		(PkPackageSack		*sack,
							 const gchar		*package_id);
PkPackageSack	*pk_package_sack_filter_by_info		(PkPackageSack		*sack,
							 PkInfoEnum		 info);
PkPackageSack	*pk_package_sack_filter			(PkPackageSack		*sack,
							 PkPackageSackFilterFunc filter_cb,
							 gpointer		 user_data);
guint64		 pk_package_sack_get_total_bytes	(PkPackageSack		*sack);

gboolean	 pk_package_sack_merge_generic_finish	(PkPackageSack		*sack,
							 GAsyncResult		*res,
							 GError			**error);

/* merging in data to the array using Resolve() */
void		 pk_package_sack_resolve_async		(PkPackageSack		*sack,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);

/* merging in data to the array using Details() */
void		 pk_package_sack_get_details_async	(PkPackageSack		*sack,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);

/* merging in data to the array using UpdateDetail() */
void		 pk_package_sack_get_update_detail_async (PkPackageSack		*sack,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);

G_END_DECLS

#endif /* __PK_PACKAGE_SACK_H */

