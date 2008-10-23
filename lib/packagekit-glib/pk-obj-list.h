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

#ifndef __PK_OBJ_LIST_H
#define __PK_OBJ_LIST_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_OBJ_LIST		(pk_obj_list_get_type ())
#define PK_OBJ_LIST(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_OBJ_LIST, PkObjList))
#define PK_OBJ_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_OBJ_LIST, PkObjListClass))
#define PK_IS_OBJ_LIST(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_OBJ_LIST))
#define PK_IS_OBJ_LIST_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_OBJ_LIST))
#define PK_OBJ_LIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_OBJ_LIST, PkObjListClass))

typedef struct PkObjListPrivate PkObjListPrivate;

typedef struct
{
	GObject		     	 parent;
	PkObjListPrivate	*priv;
	guint			 len;
} PkObjList;

typedef struct
{
	GObjectClass			 parent_class;
} PkObjListClass;

typedef gpointer (*PkObjListNewFunc)		(void);
typedef gpointer (*PkObjListCopyFunc)		(gconstpointer		 data);
typedef void	 (*PkObjListFreeFunc)		(gpointer		 data);
typedef gint	 (*PkObjListCompareFunc)	(gconstpointer		 data1,
						 gconstpointer		 data2);
typedef gboolean (*PkObjListEqualFunc)		(gconstpointer		 data1,
						 gconstpointer		 data2);
typedef gpointer (*PkObjListFromStringFunc)	(const gchar		*data);
typedef gchar	*(*PkObjListToStringFunc)	(gconstpointer		 data);

GType		 pk_obj_list_get_type		(void) G_GNUC_CONST;
PkObjList	*pk_obj_list_new		(void);

void		 pk_obj_list_set_new		(PkObjList		*list,
						 PkObjListNewFunc	 func);
void		 pk_obj_list_set_copy		(PkObjList		*list,
						 PkObjListCopyFunc	 func);
void		 pk_obj_list_set_free		(PkObjList		*list,
						 PkObjListFreeFunc	 func);
void		 pk_obj_list_set_compare	(PkObjList		*list,
						 PkObjListCompareFunc	 func);
void		 pk_obj_list_set_equal		(PkObjList		*list,
						 PkObjListEqualFunc	 func);
void		 pk_obj_list_set_to_string	(PkObjList		*list,
						 PkObjListToStringFunc	 func);
void		 pk_obj_list_set_from_string	(PkObjList		*list,
						 PkObjListFromStringFunc func);
void		 pk_obj_list_clear		(PkObjList		*list);
void		 pk_obj_list_print		(PkObjList		*list);
gchar		*pk_obj_list_to_string		(PkObjList		*list)
						 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_obj_list_to_file		(PkObjList		*list,
						 const gchar		*filename);
gboolean	 pk_obj_list_from_file		(PkObjList		*list,
						 const gchar		*filename);
void		 pk_obj_list_add		(PkObjList		*list,
						 gconstpointer		 data);
void		 pk_obj_list_sort		(PkObjList		*list,
						 GCompareFunc		 sort_func);
void		 pk_obj_list_add_list		(PkObjList		*list,
						 const PkObjList	*data);
void		 pk_obj_list_add_array		(PkObjList		*list,
						 const GPtrArray	*data);
void		 pk_obj_list_add_strv		(PkObjList		*list,
						 gpointer		**data);
void		 pk_obj_list_remove_list	(PkObjList		*list,
						 const PkObjList	*data);
void		 pk_obj_list_remove_duplicate	(PkObjList		*list);
gboolean	 pk_obj_list_exists		(PkObjList		*list,
						 gconstpointer		 data);
gboolean	 pk_obj_list_remove		(PkObjList		*list,
						 gconstpointer		 data);
gboolean	 pk_obj_list_remove_index	(PkObjList		*list,
						 guint			 index);
gconstpointer	 pk_obj_list_index		(const PkObjList	*list,
						 guint			 index);
const GPtrArray	*pk_obj_list_get_array		(const PkObjList	*list);

G_END_DECLS

#endif /* __PK_OBJ_LIST_H */
