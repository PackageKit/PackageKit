/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2011 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_TRANSACTION_H
#define __PK_TRANSACTION_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>

#include "pk-conf.h"
#include "pk-backend.h"

G_BEGIN_DECLS

#define PK_TYPE_TRANSACTION		(pk_transaction_get_type ())
#define PK_TRANSACTION(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TRANSACTION, PkTransaction))
#define PK_TRANSACTION_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TRANSACTION, PkTransactionClass))
#define PK_IS_TRANSACTION(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TRANSACTION))
#define PK_IS_TRANSACTION_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TRANSACTION))
#define PK_TRANSACTION_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TRANSACTION, PkTransactionClass))
#define PK_TRANSACTION_ERROR		(pk_transaction_error_quark ())
#define PK_TRANSACTION_TYPE_ERROR	(pk_transaction_error_get_type ())

typedef struct PkTransactionPrivate PkTransactionPrivate;

typedef struct
{
	 GObject		 parent;
	 PkTransactionPrivate	*priv;
} PkTransaction;

typedef struct
{
	GObjectClass	parent_class;
} PkTransactionClass;

/* these have to be kept in order */
typedef enum {
	PK_TRANSACTION_STATE_NEW,
	PK_TRANSACTION_STATE_WAITING_FOR_AUTH,
	PK_TRANSACTION_STATE_COMMITTED,
	PK_TRANSACTION_STATE_READY,
	PK_TRANSACTION_STATE_RUNNING,
	PK_TRANSACTION_STATE_FINISHED,
	PK_TRANSACTION_STATE_UNKNOWN
} PkTransactionState;

GQuark		 pk_transaction_error_quark			(void);
GType		 pk_transaction_error_get_type			(void);
GType		 pk_transaction_get_type			(void);
PkTransaction	*pk_transaction_new				(void);

/* go go go! */
gboolean	 pk_transaction_run				(PkTransaction	*transaction)
								 G_GNUC_WARN_UNUSED_RESULT;
/* internal status */
void		 pk_transaction_cancel_bg			(PkTransaction	*transaction);
PkRoleEnum	 pk_transaction_get_role			(PkTransaction	*transaction);
PkConf		*pk_transaction_get_conf			(PkTransaction	*transaction);
PkBackend	*pk_transaction_get_backend			(PkTransaction	*transaction);
PkResults	*pk_transaction_get_results			(PkTransaction	*transaction);
gchar		**pk_transaction_get_package_ids		(PkTransaction	*transaction);
gchar		**pk_transaction_get_values			(PkTransaction	*transaction);
gchar		**pk_transaction_get_full_paths			(PkTransaction	*transaction);
PkTransactionState pk_transaction_get_state			(PkTransaction	*transaction);
gboolean	 pk_transaction_set_state			(PkTransaction	*transaction,
								 PkTransactionState state);
const gchar	*pk_transaction_state_to_string			(PkTransactionState state);
const gchar	*pk_transaction_get_tid				(PkTransaction	*transaction);
void		 pk_transaction_add_supported_mime_type		(PkTransaction	*transaction,
								 const gchar	*mime_type);

/* plugin support */
typedef const gchar	*(*PkTransactionPluginGetDescFunc)	(void);
typedef void		 (*PkTransactionPluginFunc)		(PkTransaction	*transaction);
const gchar	*pk_transaction_plugin_get_description		(void);
void		 pk_transaction_plugin_initialize		(PkTransaction	*transaction);
void		 pk_transaction_plugin_destroy			(PkTransaction	*transaction);
void		 pk_transaction_plugin_run			(PkTransaction	*transaction);
void		 pk_transaction_plugin_started			(PkTransaction	*transaction);
void		 pk_transaction_plugin_finished_start		(PkTransaction	*transaction);
void		 pk_transaction_plugin_finished_results		(PkTransaction	*transaction);
void		 pk_transaction_plugin_finished_end		(PkTransaction	*transaction);

G_END_DECLS

#endif /* __PK_TRANSACTION_H */
