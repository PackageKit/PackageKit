/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_ENUM_LIST_H
#define __PK_ENUM_LIST_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_ENUM_LIST		(pk_enum_list_get_type ())
#define PK_ENUM_LIST(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_ENUM_LIST, PkEnumList))
#define PK_ENUM_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_ENUM_LIST, PkEnumListClass))
#define PK_IS_ENUM_LIST(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_ENUM_LIST))
#define PK_IS_ENUM_LIST_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_ENUM_LIST))
#define PK_ENUM_LIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_ENUM_LIST, PkEnumListClass))

typedef struct _PkEnumListPrivate	PkEnumListPrivate;
typedef struct _PkEnumList		PkEnumList;
typedef struct _PkEnumListClass		PkEnumListClass;

struct _PkEnumList
{
	GObject			 parent;
	PkEnumListPrivate	*priv;
};

struct _PkEnumListClass
{
	GObjectClass	parent_class;
};

typedef enum {
	PK_ENUM_LIST_TYPE_ROLE,
	PK_ENUM_LIST_TYPE_GROUP,
	PK_ENUM_LIST_TYPE_FILTER,
	PK_ENUM_LIST_TYPE_STATUS,
	PK_ENUM_LIST_TYPE_UNKNOWN
} PkEnumListType;

GType		 pk_enum_list_get_type			(void) G_GNUC_CONST;
PkEnumList	*pk_enum_list_new			(void);

gboolean	 pk_enum_list_set_type			(PkEnumList	*elist,
							 PkEnumListType	 type);
gboolean	 pk_enum_list_from_string		(PkEnumList	*elist,
							 const gchar	*enums);
gchar		*pk_enum_list_to_string			(PkEnumList	*elist)
							 G_GNUC_WARN_UNUSED_RESULT;
guint		 pk_enum_list_size			(PkEnumList	*elist);
guint		 pk_enum_list_get_item			(PkEnumList	*elist,
							 guint		 item);
gboolean	 pk_enum_list_contains			(PkEnumList	*elist,
							 guint		 value);
gint		 pk_enum_list_contains_priority		(PkEnumList	*elist,
							 gint		 value, ...);
gboolean	 pk_enum_list_append			(PkEnumList	*elist,
							 guint		 value);
gboolean	 pk_enum_list_remove			(PkEnumList	*elist,
							 guint		 value);
gboolean	 pk_enum_list_print			(PkEnumList	*elist);
gboolean	 pk_enum_list_append_multiple		(PkEnumList	*elist,
							 gint		 value, ...);

G_END_DECLS

#endif /* __PK_ENUM_LIST_H */
