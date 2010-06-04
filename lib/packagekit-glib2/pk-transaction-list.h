/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_TRANSACTION_LIST_H
#define __PK_TRANSACTION_LIST_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_TRANSACTION_LIST		(pk_transaction_list_get_type ())
#define PK_TRANSACTION_LIST(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TRANSACTION_LIST, PkTransactionList))
#define PK_TRANSACTION_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TRANSACTION_LIST, PkTransactionListClass))
#define PK_IS_TRANSACTION_LIST(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TRANSACTION_LIST))
#define PK_IS_TRANSACTION_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TRANSACTION_LIST))
#define PK_TRANSACTION_LIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TRANSACTION_LIST, PkTransactionListClass))

typedef struct _PkTransactionListPrivate	PkTransactionListPrivate;
typedef struct _PkTransactionList		PkTransactionList;
typedef struct _PkTransactionListClass		PkTransactionListClass;

struct _PkTransactionList
{
	GObject				 parent;
	PkTransactionListPrivate	*priv;
};

struct _PkTransactionListClass
{
	GObjectClass	parent_class;
	void		(* added)			(PkTransactionList	*tlist,
							 const gchar		*tid);
	void		(* removed)			(PkTransactionList	*tlist,
							 const gchar		*tid);
};

GType			 pk_transaction_list_get_type		(void);
PkTransactionList	*pk_transaction_list_new		(void);
void			 pk_transaction_list_test		(gpointer	 user_data);

gchar			**pk_transaction_list_get_ids		(PkTransactionList	*tlist);

G_END_DECLS

#endif /* __PK_TRANSACTION_LIST_H */

