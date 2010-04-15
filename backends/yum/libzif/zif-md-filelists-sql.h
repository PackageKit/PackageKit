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

#ifndef __ZIF_MD_FILELISTS_SQL_H
#define __ZIF_MD_FILELISTS_SQL_H

#include <glib-object.h>

#include "zif-md.h"

G_BEGIN_DECLS

#define ZIF_TYPE_MD_FILELISTS_SQL		(zif_md_filelists_sql_get_type ())
#define ZIF_MD_FILELISTS_SQL(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_MD_FILELISTS_SQL, ZifMdFilelistsSql))
#define ZIF_MD_FILELISTS_SQL_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_MD_FILELISTS_SQL, ZifMdFilelistsSqlClass))
#define ZIF_IS_MD_FILELISTS_SQL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_MD_FILELISTS_SQL))
#define ZIF_IS_MD_FILELISTS_SQL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_MD_FILELISTS_SQL))
#define ZIF_MD_FILELISTS_SQL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_MD_FILELISTS_SQL, ZifMdFilelistsSqlClass))

typedef struct _ZifMdFilelistsSql		ZifMdFilelistsSql;
typedef struct _ZifMdFilelistsSqlPrivate	ZifMdFilelistsSqlPrivate;
typedef struct _ZifMdFilelistsSqlClass		ZifMdFilelistsSqlClass;

struct _ZifMdFilelistsSql
{
	ZifMd					 parent;
	ZifMdFilelistsSqlPrivate		*priv;
};

struct _ZifMdFilelistsSqlClass
{
	ZifMdClass				 parent_class;
};

GType			 zif_md_filelists_sql_get_type		(void);
ZifMdFilelistsSql	*zif_md_filelists_sql_new		(void);

G_END_DECLS

#endif /* __ZIF_MD_FILELISTS_SQL_H */

