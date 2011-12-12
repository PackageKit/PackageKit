/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_CONSOLE_SHARED_H
#define __PK_CONSOLE_SHARED_H

#include <glib.h>
#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-bitfield.h>
#include <packagekit-glib2/pk-enum.h>

G_BEGIN_DECLS

void		 pk_console_test			(gpointer	 user_data);
guint		 pk_console_get_number			(const gchar	*question,
							 guint		 maxnum);
gboolean	 pk_console_get_prompt			(const gchar	*question,
							 gboolean	 defaultyes);
gchar		*pk_console_resolve_package		(PkClient	*client,
							 PkBitfield	 filter,
							 const gchar	*package,
							 GError		**error);
gchar		**pk_console_resolve_packages		(PkClient	*client,
							 PkBitfield	 filter,
							 gchar		**packages,
							 GError		**error);

G_END_DECLS

#endif /* __PK_CONSOLE_SHARED_H */

