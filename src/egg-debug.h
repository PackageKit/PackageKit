/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __EGG_DEBUG_H
#define __EGG_DEBUG_H

#include <stdarg.h>
#include <glib.h>

G_BEGIN_DECLS

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
/**
 * egg_debug:
 *
 * Non critical debugging
 */
#define egg_debug(...) egg_debug_real (__func__, __FILE__, __LINE__, __VA_ARGS__)

/**
 * egg_warning:
 *
 * Important debugging
 */
#define egg_warning(...) egg_warning_real (__func__, __FILE__, __LINE__, __VA_ARGS__)

/**
 * egg_error:
 *
 * Critical debugging, with exit
 */
#define egg_error(...) egg_error_real (__func__, __FILE__, __LINE__, __VA_ARGS__)

#elif defined(__GNUC__) && __GNUC__ >= 3
#define egg_debug(...) egg_debug_real (__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define egg_warning(...) egg_warning_real (__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define egg_error(...) egg_error_real (__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#else
#define egg_debug(...)
#define egg_warning(...)
#define egg_error(...)
#endif

gboolean	 egg_debug_init			(gint		*argc,
						 gchar		***argv);
GOptionGroup	*egg_debug_get_option_group	(void);
gboolean	 egg_debug_is_verbose		(void);
void		 egg_debug_backtrace		(void);
void		 egg_debug_set_log_filename	(const gchar	*filename);
void		 egg_debug_real			(const gchar	*func,
						 const gchar	*file,
						 gint		 line,
						 const gchar	*format, ...) __attribute__((format (printf,4,5)));
void		 egg_warning_real		(const gchar	*func,
						 const gchar	*file,
						 gint		 line,
						 const gchar	*format, ...) __attribute__((format (printf,4,5)));
void		 egg_error_real			(const gchar	*func,
						 const gchar	*file,
						 gint		 line,
						 const gchar	*format, ...) G_GNUC_NORETURN __attribute__((format (printf,4,5)));

G_END_DECLS

#endif /* __EGG_DEBUG_H */
