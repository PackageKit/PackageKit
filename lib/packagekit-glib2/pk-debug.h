/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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

#if !defined (PK_COMPILATION)
#error "Can only include this internally!"
#endif

#ifndef __PK_DEBUG_H__
#define __PK_DEBUG_H__

#include <glib.h>

gboolean	 pk_debug_is_verbose		(void);
GOptionGroup	*pk_debug_get_option_group	(void);
void		 pk_debug_add_log_domain	(const gchar	*log_domain);

#endif /* __PK_DEBUG_H__ */
