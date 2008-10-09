/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_SERVICE_PACK_H
#define __PK_SERVICE_PACK_H

#include <glib-object.h>
#include <pk-package-list.h>

G_BEGIN_DECLS

#define PK_TYPE_SERVICE_PACK		(pk_service_pack_get_type ())
#define PK_SERVICE_PACK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_SERVICE_PACK, PkServicePack))
#define PK_SERVICE_PACK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_SERVICE_PACK, PkServicePackClass))
#define PK_IS_SERVICE_PACK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_SERVICE_PACK))
#define PK_IS_SERVICE_PACK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_SERVICE_PACK))
#define PK_SERVICE_PACK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_SERVICE_PACK, PkServicePackClass))
#define PK_SERVICE_PACK_ERROR	 	(pk_service_pack_error_quark ())
#define PK_SERVICE_PACK_TYPE_ERROR	(pk_service_pack_error_get_type ())

typedef struct PkServicePackPrivate PkServicePackPrivate;

typedef enum
{
	PK_SERVICE_PACK_ERROR_FAILED_SETUP,
	PK_SERVICE_PACK_ERROR_FAILED_DOWNLOAD,
	PK_SERVICE_PACK_ERROR_FAILED_EXTRACTION,
	PK_SERVICE_PACK_ERROR_FAILED_CREATE,
	PK_SERVICE_PACK_ERROR_NOTHING_TO_DO,
	PK_SERVICE_PACK_ERROR_NOT_COMPATIBLE
} PkServicePackError;

typedef struct
{
	GObject		      parent;
	PkServicePackPrivate     *priv;
} PkServicePack;

typedef struct
{
	GObjectClass	parent_class;
	void		(* package)			(PkServicePack		*pack,
							 const PkPackageObj	*obj);
	/* Padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
} PkServicePackClass;

GQuark		 pk_service_pack_error_quark			(void);
GType		 pk_service_pack_error_get_type			(void);
GType		 pk_service_pack_get_type			(void) G_GNUC_CONST;
PkServicePack	*pk_service_pack_new				(void);

/* used by the server */
gboolean	 pk_service_pack_check_valid			(PkServicePack	*pack,
								 GError		**error);

/* used by clients */
gboolean	 pk_service_pack_set_filename			(PkServicePack	*pack,
								 const gchar	*filename);
gboolean	 pk_service_pack_set_temp_directory		(PkServicePack	*pack,
								 const gchar	*directory);
gboolean	 pk_service_pack_set_exclude_list		(PkServicePack	*pack,
								 PkPackageList	*list);

gboolean	 pk_service_pack_create_for_package_id		(PkServicePack	*pack,
								 const gchar	*package_id,
								 GError		**error);
gboolean	 pk_service_pack_create_for_package_ids		(PkServicePack	*pack,
								 gchar		**package_ids,
								 GError		**error);
gboolean	 pk_service_pack_create_for_updates		(PkServicePack	*pack,
								 GError		**error);

G_END_DECLS

#endif /* __PK_SERVICE_PACK_H */

