/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_TRANSACTION_DB_H
#define __PK_TRANSACTION_DB_H

#include <glib-object.h>
#include <packagekit-glib2/pk-enum.h>

G_BEGIN_DECLS

#define PK_TYPE_TRANSACTION_DB		(pk_transaction_db_get_type ())
#define PK_TRANSACTION_DB(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TRANSACTION_DB, PkTransactionDb))
#define PK_TRANSACTION_DB_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TRANSACTION_DB, PkTransactionDbClass))
#define PK_IS_TRANSACTION_DB(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TRANSACTION_DB))
#define PK_IS_TRANSACTION_DB_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TRANSACTION_DB))
#define PK_TRANSACTION_DB_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TRANSACTION_DB, PkTransactionDbClass))

#if PK_BUILD_LOCAL
#define PK_TRANSACTION_DB_FILE			"./transactions.db"
#else
#define PK_TRANSACTION_DB_FILE			PK_DB_DIR "/transactions.db"
#endif

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
gboolean	 pk_transaction_db_set_uid		(PkTransactionDb	*tdb,
							 const gchar		*tid,
							 guint			 uid);
gboolean	 pk_transaction_db_set_cmdline		(PkTransactionDb	*tdb,
							 const gchar		*tid,
							 const gchar		*cmdline);
gboolean	 pk_transaction_db_set_finished		(PkTransactionDb	*tdb,
							 const gchar		*tid,
							 gboolean		 success,
							 guint			 runtime);
gboolean	 pk_transaction_db_set_data		(PkTransactionDb	*tdb,
							 const gchar		*tid,
							 const gchar		*data);
gboolean	 pk_transaction_db_get_list		(PkTransactionDb	*tdb,
							 guint			 limit);
gboolean	 pk_transaction_db_action_time_reset	(PkTransactionDb	*tdb,
							 PkRoleEnum		 role);
guint		 pk_transaction_db_action_time_since	(PkTransactionDb	*tdb,
							 PkRoleEnum		 role);
gchar		*pk_transaction_db_generate_id		(PkTransactionDb	*tdb)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_transaction_db_get_proxy		(PkTransactionDb	*tdb,
							 guint			 uid,
							 const gchar		*session,
							 gchar			**proxy_http,
							 gchar			**proxy_https,
							 gchar			**proxy_ftp,
							 gchar			**proxy_socks,
							 gchar			**no_proxy,
							 gchar			**pac);
gboolean	 pk_transaction_db_set_proxy		(PkTransactionDb	*tdb,
							 guint			 uid,
							 const gchar		*session,
							 const gchar		*proxy_http,
							 const gchar		*proxy_https,
							 const gchar		*proxy_ftp,
							 const gchar		*proxy_socks,
							 const gchar		*no_proxy,
							 const gchar		*pac);
gboolean	 pk_transaction_db_get_root		(PkTransactionDb	*tdb,
							 guint			 uid,
							 const gchar		*session,
							 gchar			**root);
gboolean	 pk_transaction_db_set_root		(PkTransactionDb	*tdb,
							 guint			 uid,
							 const gchar		*session,
							 const gchar		*root);

G_END_DECLS

#endif /* __PK_TRANSACTION_DB_H */
