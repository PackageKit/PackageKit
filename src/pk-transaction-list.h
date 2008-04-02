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

#ifndef __PK_TRANSACTION_LIST_H
#define __PK_TRANSACTION_LIST_H

#include <glib-object.h>
#include "pk-runner.h"
#include "pk-package-list.h"

G_BEGIN_DECLS

#define PK_TYPE_TRANSACTION_LIST		(pk_transaction_list_get_type ())
#define PK_TRANSACTION_LIST(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TRANSACTION_LIST, PkTransactionList))
#define PK_TRANSACTION_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TRANSACTION_LIST, PkTransactionListClass))
#define PK_IS_TRANSACTION_LIST(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TRANSACTION_LIST))
#define PK_IS_TRANSACTION_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TRANSACTION_LIST))
#define PK_TRANSACTION_LIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TRANSACTION_LIST, PkTransactionListClass))
#define PK_TRANSACTION_LIST_ERROR		(pk_transaction_list_error_quark ())
#define PK_TRANSACTION_LIST_TYPE_ERROR		(pk_transaction_list_error_get_type ()) 

typedef struct PkTransactionListPrivate PkTransactionListPrivate;

typedef struct
{
	 GObject			 parent;
	 PkTransactionListPrivate	*priv;
} PkTransactionList;

typedef struct
{
	GObjectClass	parent_class;
} PkTransactionListClass;

typedef struct {
	gboolean		 committed;
	gboolean		 running;
	gboolean		 finished;
	PkRunner		*runner;
	gchar			*tid;
} PkTransactionItem;

GType		 pk_transaction_list_get_type	  	(void) G_GNUC_CONST;
PkTransactionList *pk_transaction_list_new		(void);

PkTransactionItem *pk_transaction_list_create		(PkTransactionList	*tlist);
gboolean	 pk_transaction_list_remove		(PkTransactionList	*tlist,
							 PkTransactionItem	*item);
gboolean	 pk_transaction_list_commit		(PkTransactionList	*tlist,
							 PkTransactionItem	*item)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_transaction_list_role_present	(PkTransactionList	*tlist,
							 PkRoleEnum		 role);
gchar		**pk_transaction_list_get_array		(PkTransactionList	*tlist)
							 G_GNUC_WARN_UNUSED_RESULT;
guint		 pk_transaction_list_get_size		(PkTransactionList	*tlist);
PkTransactionItem *pk_transaction_list_get_from_tid	(PkTransactionList	*tlist,
							 const gchar		*tid);
PkTransactionItem *pk_transaction_list_get_from_runner	(PkTransactionList	*tlist,
							 PkRunner		*runner);

G_END_DECLS

#endif /* __PK_TRANSACTION_LIST_H */
