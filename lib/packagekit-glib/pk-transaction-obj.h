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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_TRANSACTION_OBJ_H
#define __PK_TRANSACTION_OBJ_H

#include <glib-object.h>
#include <packagekit-glib/pk-enum.h>

G_BEGIN_DECLS

/**
 * PkTransactionObj:
 *
 * Cached object to represent details about the transaction.
 **/
typedef struct
{
	gchar				*tid;
	gchar				*timespec;
	gboolean			 succeeded;
	PkRoleEnum			 role;
	guint				 duration;
	gchar				*data;
} PkTransactionObj;

PkTransactionObj	*pk_transaction_obj_new		(void);
PkTransactionObj	*pk_transaction_obj_copy		(const PkTransactionObj *obj);
PkTransactionObj	*pk_transaction_obj_new_from_data	(const gchar		*tid,
								 const gchar		*timespec,
								 gboolean		 succeeded,
								 PkRoleEnum		 role,
								 guint			 duration,
								 const gchar		*data);
gboolean		 pk_transaction_obj_free		(PkTransactionObj	*obj);

G_END_DECLS

#endif /* __PK_TRANSACTION_OBJ_H */
