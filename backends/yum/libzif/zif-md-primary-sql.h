/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_MD_PRIMARY_SQL_H
#define __ZIF_MD_PRIMARY_SQL_H

#include <glib-object.h>
#include <packagekit-glib2/packagekit.h>

#include "zif-md.h"

G_BEGIN_DECLS

#define ZIF_TYPE_MD_PRIMARY_SQL		(zif_md_primary_sql_get_type ())
#define ZIF_MD_PRIMARY_SQL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_MD_PRIMARY_SQL, ZifMdPrimarySql))
#define ZIF_MD_PRIMARY_SQL_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_MD_PRIMARY_SQL, ZifMdPrimarySqlClass))
#define ZIF_IS_MD_PRIMARY_SQL(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_MD_PRIMARY_SQL))
#define ZIF_IS_MD_PRIMARY_SQL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_MD_PRIMARY_SQL))
#define ZIF_MD_PRIMARY_SQL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_MD_PRIMARY_SQL, ZifMdPrimarySqlClass))

typedef struct _ZifMdPrimarySql		ZifMdPrimarySql;
typedef struct _ZifMdPrimarySqlPrivate	ZifMdPrimarySqlPrivate;
typedef struct _ZifMdPrimarySqlClass	ZifMdPrimarySqlClass;

struct _ZifMdPrimarySql
{
	ZifMd				 parent;
	ZifMdPrimarySqlPrivate		*priv;
};

struct _ZifMdPrimarySqlClass
{
	ZifMdClass			 parent_class;
};

GType		 zif_md_primary_sql_get_type		(void);
ZifMdPrimarySql	*zif_md_primary_sql_new			(void);

G_END_DECLS

#endif /* __ZIF_MD_PRIMARY_SQL_H */

