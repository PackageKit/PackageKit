/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_SERVICE_PACK_H
#define __PK_SERVICE_PACK_H

#include <glib-object.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-service-pack.h>
#include <packagekit-glib2/pk-progress.h>

G_BEGIN_DECLS

#define PK_TYPE_SERVICE_PACK		(pk_service_pack_get_type ())
#define PK_SERVICE_PACK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_SERVICE_PACK, PkServicePack))
#define PK_SERVICE_PACK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_SERVICE_PACK, PkServicePackClass))
#define PK_IS_SERVICE_PACK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_SERVICE_PACK))
#define PK_IS_SERVICE_PACK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_SERVICE_PACK))
#define PK_SERVICE_PACK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_SERVICE_PACK, PkServicePackClass))
#define PK_SERVICE_PACK_ERROR	 	(pk_service_pack_error_quark ())
#define PK_SERVICE_PACK_TYPE_ERROR	(pk_service_pack_error_get_type ())

/* the file extension to a servicepack */
#define PK_SERVICE_PACK_FILE_EXTENSION	"servicepack"

typedef enum
{
	PK_SERVICE_PACK_ERROR_FAILED_SETUP,
	PK_SERVICE_PACK_ERROR_FAILED_DOWNLOAD,
	PK_SERVICE_PACK_ERROR_FAILED_EXTRACTION,
	PK_SERVICE_PACK_ERROR_FAILED_CREATE,
	PK_SERVICE_PACK_ERROR_NOTHING_TO_DO,
	PK_SERVICE_PACK_ERROR_NOT_COMPATIBLE
} PkServicePackError;

typedef struct _PkServicePackPrivate	PkServicePackPrivate;
typedef struct _PkServicePack		PkServicePack;
typedef struct _PkServicePackClass	PkServicePackClass;

struct _PkServicePack
{
	GObject			 parent;
	PkServicePackPrivate	*priv;
};

struct _PkServicePackClass
{
	GObjectClass	parent_class;
	/* Padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
};

GQuark		 pk_service_pack_error_quark		(void);
GType		 pk_service_pack_get_type		(void);
PkServicePack	*pk_service_pack_new			(void);
void		 pk_service_pack_test			(gpointer		 user_data);

/* used by the server */
gboolean	 pk_service_pack_check_valid		(PkServicePack		*pack,
							 const gchar		*filename,
							 GError			**error);

/* used by clients */
gboolean	 pk_service_pack_set_temp_directory	(PkServicePack		*pack,
							 const gchar		*directory);

gboolean	 pk_service_pack_generic_finish		(PkServicePack		*pack,
							 GAsyncResult		*res,
							 GError			**error);

void		 pk_service_pack_create_for_package_ids_async (PkServicePack	*pack,
							 const gchar		*filename,
							 gchar			**package_ids,
							 gchar			**package_ids_exclude,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
void		 pk_service_pack_create_for_updates_async (PkServicePack	*pack,
							 const gchar		*filename,
							 gchar			**package_ids_exclude,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);

G_END_DECLS

#endif /* __PK_SERVICE_PACK_H */

