/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(PK_COMPILATION)
#error "Can only include this internally!"
#endif

#ifndef __PK_DEBUG_H__
#define __PK_DEBUG_H__

#include <glib.h>

G_BEGIN_DECLS

gboolean      pk_debug_is_verbose (void);
GOptionGroup *pk_debug_get_option_group (void);
void	      pk_debug_add_log_domain (const gchar *log_domain);
void	      pk_debug_set_verbose (gboolean verbose);

G_END_DECLS

#endif /* __PK_DEBUG_H__ */
