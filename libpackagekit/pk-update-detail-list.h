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

#ifndef __PK_UPDATE_DETAIL_LIST_H
#define __PK_UPDATE_DETAIL_LIST_H

#include <glib-object.h>
#include <pk-update-detail-obj.h>

G_BEGIN_DECLS

#define PK_TYPE_UPDATE_DETAIL_LIST		(pk_update_detail_list_get_type ())
#define PK_UPDATE_DETAIL_LIST(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_UPDATE_DETAIL_LIST, PkUpdateDetailList))
#define PK_UPDATE_DETAIL_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_UPDATE_DETAIL_LIST, PkUpdateDetailListClass))
#define PK_IS_UPDATE_DETAIL_LIST(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_UPDATE_DETAIL_LIST))
#define PK_IS_UPDATE_DETAIL_LIST_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_UPDATE_DETAIL_LIST))
#define PK_UPDATE_DETAIL_LIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_UPDATE_DETAIL_LIST, PkUpdateDetailListClass))

typedef struct PkUpdateDetailListPrivate PkUpdateDetailListPrivate;

typedef struct
{
	GObject		     		 parent;
	PkUpdateDetailListPrivate	*priv;
} PkUpdateDetailList;

typedef struct
{
	GObjectClass			 parent_class;
} PkUpdateDetailListClass;

GType			 pk_update_detail_list_get_type		(void) G_GNUC_CONST;
PkUpdateDetailList	 *pk_update_detail_list_new		(void);

gboolean		 pk_update_detail_list_clear		(PkUpdateDetailList	*list);
gboolean		 pk_update_detail_list_add_obj 		(PkUpdateDetailList	*list,
								 const PkUpdateDetailObj *obj);
const PkUpdateDetailObj	*pk_update_detail_list_get_obj 		(PkUpdateDetailList	*list,
								 const PkPackageId	*id);


G_END_DECLS

#endif /* __PK_UPDATE_DETAIL_LIST_H */
