/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_TRANSACTION_PRIVATE_H
#define __PK_TRANSACTION_PRIVATE_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/* only here for the self test program to use */
void	pk_transaction_get_updates	(PkTransaction	*transaction,
					 GVariant	*params,
					 GDBusMethodInvocation *context);
void	pk_transaction_search_details	(PkTransaction	*transaction,
					 GVariant	*params,
					 GDBusMethodInvocation *context);
void	pk_transaction_search_names	(PkTransaction	*transaction,
					 GVariant	*params,
					 GDBusMethodInvocation *context);
gboolean	 pk_transaction_set_sender			(PkTransaction	*transaction,
								 const gchar	*sender);
gboolean	 pk_transaction_filter_check			(const gchar	*filter,
								 GError		**error);
gboolean	 pk_transaction_strvalidate			(const gchar	*textr,
								 GError		**error);
gboolean	 pk_transaction_set_tid				(PkTransaction	*transaction,
								 const gchar	*tid);


G_END_DECLS

#endif /* __PK_TRANSACTION_PRIVATE_H */
