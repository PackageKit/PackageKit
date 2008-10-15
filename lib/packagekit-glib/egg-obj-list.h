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

#ifndef __EGG_OBJ_LIST_H
#define __EGG_OBJ_LIST_H

#include <glib-object.h>

G_BEGIN_DECLS

#define EGG_TYPE_OBJ_LIST		(egg_obj_list_get_type ())
#define EGG_OBJ_LIST(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), EGG_TYPE_OBJ_LIST, EggObjList))
#define EGG_OBJ_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), EGG_TYPE_OBJ_LIST, EggObjListClass))
#define EGG_IS_OBJ_LIST(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), EGG_TYPE_OBJ_LIST))
#define EGG_IS_OBJ_LIST_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), EGG_TYPE_OBJ_LIST))
#define EGG_OBJ_LIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), EGG_TYPE_OBJ_LIST, EggObjListClass))

typedef struct EggObjListPrivate EggObjListPrivate;

typedef struct
{
	GObject		     	 parent;
	EggObjListPrivate	*priv;
	guint			 len;
} EggObjList;

typedef struct
{
	GObjectClass			 parent_class;
} EggObjListClass;

typedef gpointer (*EggObjListNewFunc)		(void);
typedef gpointer (*EggObjListCopyFunc)		(gconstpointer		 data);
typedef void	 (*EggObjListFreeFunc)		(gpointer		 data);
typedef gint	 (*EggObjListCompareFunc)	(gconstpointer		 data1,
						 gconstpointer		 data2);
typedef gpointer (*EggObjListFromStringFunc)	(const gchar		*data);
typedef gchar	*(*EggObjListToStringFunc)	(gconstpointer		 data);

GType		 egg_obj_list_get_type		(void) G_GNUC_CONST;
EggObjList	*egg_obj_list_new		(void);

void		 egg_obj_list_set_new		(EggObjList		*list,
						 EggObjListNewFunc	 func);
void		 egg_obj_list_set_copy		(EggObjList		*list,
						 EggObjListCopyFunc	 func);
void		 egg_obj_list_set_free		(EggObjList		*list,
						 EggObjListFreeFunc	 func);
void		 egg_obj_list_set_compare	(EggObjList		*list,
						 EggObjListCompareFunc	 func);
void		 egg_obj_list_set_to_string	(EggObjList		*list,
						 EggObjListToStringFunc	 func);
void		 egg_obj_list_set_from_string	(EggObjList		*list,
						 EggObjListFromStringFunc func);
void		 egg_obj_list_clear		(EggObjList		*list);
void		 egg_obj_list_print		(EggObjList		*list);
gchar		*egg_obj_list_to_string		(EggObjList		*list)
						 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 egg_obj_list_to_file		(EggObjList		*list,
						 const gchar		*filename);
gboolean	 egg_obj_list_from_file		(EggObjList		*list,
						 const gchar		*filename);
void		 egg_obj_list_add		(EggObjList		*list,
						 gconstpointer		 data);
void		 egg_obj_list_add_list		(EggObjList		*list,
						 const EggObjList	*data);
void		 egg_obj_list_add_array		(EggObjList		*list,
						 const GPtrArray	*data);
void		 egg_obj_list_remove_list	(EggObjList		*list,
						 const EggObjList	*data);
void		 egg_obj_list_remove_duplicate	(EggObjList		*list);
gboolean	 egg_obj_list_exists		(EggObjList		*list,
						 gconstpointer		 data);
gboolean	 egg_obj_list_remove		(EggObjList		*list,
						 gconstpointer		 data);
gboolean	 egg_obj_list_remove_index	(EggObjList		*list,
						 guint			 index);
gconstpointer	 egg_obj_list_index		(const EggObjList	*list,
						 guint			 index);
const GPtrArray	*egg_obj_list_get_array		(const EggObjList	*list);

G_END_DECLS

#endif /* __EGG_OBJ_LIST_H */
