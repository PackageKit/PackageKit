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

#ifndef __PK_THREAD_LIST_H
#define __PK_THREAD_LIST_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_THREAD_LIST		(pk_thread_list_get_type ())
#define PK_THREAD_LIST(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_THREAD_LIST, PkThreadList))
#define PK_THREAD_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_THREAD_LIST, PkThreadListClass))
#define PK_IS_THREAD_LIST(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_THREAD_LIST))
#define PK_IS_THREAD_LIST_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_THREAD_LIST))
#define PK_THREAD_LIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_THREAD_LIST, PkThreadListClass))

typedef struct PkThreadListPrivate PkThreadListPrivate;

typedef struct
{
	 GObject		 parent;
	 PkThreadListPrivate	*priv;
} PkThreadList;

typedef struct
{
	GObjectClass	parent_class;
} PkThreadListClass;

GType		 pk_thread_list_get_type	  	(void) G_GNUC_CONST;
PkThreadList	*pk_thread_list_new			(void);

typedef gboolean (*PkThreadFunc)			(PkThreadList	*tlist,
							 gpointer	 data);
gboolean	 pk_thread_list_create			(PkThreadList	*tlist,
							 PkThreadFunc	 func,
							 gpointer	 param,
							 gpointer	 data);
gboolean	 pk_thread_list_wait			(PkThreadList	*tlist);
guint		 pk_thread_list_number_running		(PkThreadList	*tlist);

G_END_DECLS

#endif /* __PK_THREAD_LIST_H */
