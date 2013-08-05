/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __HIF_DB_H
#define __HIF_DB_H

#include <glib-object.h>
#include <hawkey/packagelist.h>

G_BEGIN_DECLS

#define HIF_TYPE_DB		(hif_db_get_type ())
#define HIF_DB(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), HIF_TYPE_DB, HifDb))
#define HIF_DB_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), HIF_TYPE_DB, HifDbClass))
#define HIF_IS_DB(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), HIF_TYPE_DB))
#define HIF_IS_DB_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), HIF_TYPE_DB))
#define HIF_DB_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), HIF_TYPE_DB, HifDbClass))

typedef struct _HifDb		HifDb;
typedef struct _HifDbPrivate	HifDbPrivate;
typedef struct _HifDbClass	HifDbClass;

struct _HifDb
{
	GObject			 parent;
	HifDbPrivate		*priv;
};

struct _HifDbClass
{
	GObjectClass		 parent_class;
};

typedef enum {
	HIF_ERROR_FAILED,
	HIF_ERROR_LAST
} HifDbError;

GQuark		 hif_db_error_quark		(void);
GType		 hif_db_get_type		(void);
HifDb		*hif_db_new			(void);

gchar		*hif_db_get_string		(HifDb		*db,
						 HyPackage	 package,
						 const gchar	*key,
						 GError		**error);
gboolean	 hif_db_set_string		(HifDb		*db,
						 HyPackage	 package,
						 const gchar	*key,
						 const gchar	*value,
						 GError		**error);
gboolean	 hif_db_remove			(HifDb		*db,
						 HyPackage	 package,
						 const gchar	*key,
						 GError		**error);
gboolean	 hif_db_remove_all		(HifDb		*db,
						 HyPackage	 package,
						 GError		**error);

G_END_DECLS

#endif /* __HIF_DB_H */
