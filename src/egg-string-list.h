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

#ifndef __EGG_STRING_LIST_H
#define __EGG_STRING_LIST_H

#include <glib-object.h>
#include "egg-obj-list.h"

G_BEGIN_DECLS

#define EGG_TYPE_STRING_LIST		(egg_string_list_get_type ())
#define EGG_STRING_LIST(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), EGG_TYPE_STRING_LIST, EggStringList))
#define EGG_STRING_LIST_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), EGG_TYPE_STRING_LIST, EggStringListClass))
#define EGG_IS_STRING_LIST(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), EGG_TYPE_STRING_LIST))
#define EGG_IS_STRING_LIST_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), EGG_TYPE_STRING_LIST))
#define EGG_STRING_LIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), EGG_TYPE_STRING_LIST, EggStringListClass))
#define EGG_STRING_LIST_ERROR		(egg_string_list_error_quark ())
#define EGG_STRING_LIST_TYPE_ERROR	(egg_string_list_error_get_type ())

typedef struct EggStringListPrivate EggStringListPrivate;

typedef struct
{
	 EggObjList		 parent;
} EggStringList;

typedef struct
{
	EggObjListClass		parent_class;
} EggStringListClass;

GType		 egg_string_list_get_type	  	(void) G_GNUC_CONST;
EggStringList	*egg_string_list_new			(void);
void		 egg_string_list_add_strv		(EggStringList	*list,
							 gchar		**data);
const gchar	*egg_string_list_index			(const EggStringList	*list,
							 guint		 index);
void		 egg_string_list_print			(EggStringList	*list);


G_END_DECLS

#endif /* __EGG_STRING_LIST_H */

