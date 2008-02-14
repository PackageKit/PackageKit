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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __PK_TRANSACTION_ID_H
#define __PK_TRANSACTION_ID_H

G_BEGIN_DECLS

gchar		*pk_transaction_id_generate		(void);
gboolean	 pk_transaction_id_check		(const gchar	*tid);
gboolean	 pk_transaction_id_equal		(const gchar	*tid1,
							 const gchar	*tid2);

G_END_DECLS

#endif /* __PK_TRANSACTION_ID_H */
