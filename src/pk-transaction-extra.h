/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_POST_TRANS_H
#define __PK_POST_TRANS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_POST_TRANS		(pk_transaction_extra_get_type ())
#define PK_POST_TRANS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_POST_TRANS, PkTransactionExtra))
#define PK_POST_TRANS_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_POST_TRANS, PkTransactionExtraClass))
#define PK_IS_POST_TRANS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_POST_TRANS))
#define PK_IS_POST_TRANS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_POST_TRANS))
#define PK_POST_TRANS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_POST_TRANS, PkTransactionExtraClass))

typedef struct PkTransactionExtraPrivate PkTransactionExtraPrivate;

typedef struct
{
	GObject		      parent;
	PkTransactionExtraPrivate     *priv;
} PkTransactionExtra;

typedef struct
{
	GObjectClass	parent_class;
} PkTransactionExtraClass;

GType		 pk_transaction_extra_get_type			(void);
PkTransactionExtra	*pk_transaction_extra_new		(void);

gboolean	 pk_transaction_extra_import_desktop_files	(PkTransactionExtra	*extra);
gboolean	 pk_transaction_extra_check_running_process	(PkTransactionExtra	*extra,
								 gchar			**package_ids);
gboolean	 pk_transaction_extra_check_desktop_files	(PkTransactionExtra	*extra,
								 gchar			**package_ids);
gboolean	 pk_transaction_extra_check_library_restart	(PkTransactionExtra	*extra);
gboolean	 pk_transaction_extra_check_library_restart_pre	(PkTransactionExtra	*extra,
								 gchar			**package_ids);

G_END_DECLS

#endif /* __PK_POST_TRANS_H */

