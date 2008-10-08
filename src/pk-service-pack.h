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

typedef struct PkServicePackPrivate PkServicePackPrivate;

typedef struct
{
	GObject		      parent;
	PkServicePackPrivate     *priv;
} PkServicePack;

typedef struct
{
	GObjectClass	parent_class;
} PkServicePackClass;

GType		 pk_service_pack_get_type			(void) G_GNUC_CONST;
PkServicePack	*pk_service_pack_new				(void);

/* used by the server */
gboolean	 pk_service_pack_check_valid			(PkServicePack	*pack,
								 const gchar	*filename,
								 GError		**error);

/* used by clients */
gboolean	 pk_service_pack_set_filename			(PkServicePack	*pack,
								 const gchar	*filename);
gboolean	 pk_service_pack_set_temp_directory		(PkServicePack	*pack,
								 const gchar	*directory);
gboolean	 pk_service_pack_set_exclude_list		(PkServicePack	*pack,
								 PkPackageList	*list);

gboolean	 pk_service_pack_create_for_package		(PkServicePack	*pack,
								 const gchar	*package,
								 GError		**error);
gboolean	 pk_service_pack_create_for_updates		(PkServicePack	*pack,
								 GError		**error);

G_END_DECLS

#endif /* __PK_SERVICE_PACK_H */

