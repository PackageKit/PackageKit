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

#ifndef __PK_TRANSACTION_DB_H
#define __PK_TRANSACTION_DB_H

#include <glib-object.h>
#include <pk-enum.h>

G_BEGIN_DECLS

#define PK_TYPE_TRANSACTION_DB		(pk_transaction_db_get_type ())
#define PK_TRANSACTION_DB(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TRANSACTION_DB, PkTransactionDb))
#define PK_TRANSACTION_DB_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TRANSACTION_DB, PkTransactionDbClass))
#define PK_IS_TRANSACTION_DB(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TRANSACTION_DB))
#define PK_IS_TRANSACTION_DB_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TRANSACTION_DB))
#define PK_TRANSACTION_DB_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TRANSACTION_DB, PkTransactionDbClass))

typedef struct PkTransactionDbPrivate PkTransactionDbPrivate;

typedef struct
{
	 GObject		 parent;
	 PkTransactionDbPrivate	*priv;
} PkTransactionDb;

typedef struct
{
	GObjectClass	parent_class;
} PkTransactionDbClass;

GType		 pk_transaction_db_get_type		(void);
PkTransactionDb	*pk_transaction_db_new			(void);
gboolean	 pk_transaction_db_empty		(PkTransactionDb	*tdb);
gboolean	 pk_transaction_db_add			(PkTransactionDb	*tdb,
							 const gchar		*tid);
gboolean	 pk_transaction_db_print		(PkTransactionDb	*tdb);
gboolean	 pk_transaction_db_set_role		(PkTransactionDb	*tdb,
							 const gchar		*tid,
							 PkRoleEnum		 role);
gboolean	 pk_transaction_db_set_finished		(PkTransactionDb	*tdb,
							 const gchar		*tid,
							 gboolean		 success,
							 guint			 runtime);
gboolean	 pk_transaction_db_set_data		(PkTransactionDb	*tdb,
							 const gchar		*tid,
							 const gchar		*data);
gboolean	 pk_transaction_db_get_list		(PkTransactionDb	*tdb,
							 guint			 limit);

G_END_DECLS

#endif /* __PK_TRANSACTION_DB_H */
