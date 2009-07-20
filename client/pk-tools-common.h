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

#ifndef __PK_TOOLS_COMMON_H
#define __PK_TOOLS_COMMON_H

#include <glib/gi18n.h>
#include <packagekit-glib/packagekit.h>

/* Reserved exit codes:
 * 1		miscellaneous errors, such as "divide by zero"
 * 2		misuse of shell builtins
 * 126		command invoked cannot execute
 * 127		"command not found"
 * 128		invalid argument to exit
 * 128+n	fatal error signal "n"
 * 130		script terminated by Control-C
 * 255		exit status out of range
 */
#define	PK_EXIT_CODE_SUCCESS		0
#define	PK_EXIT_CODE_FAILED		1
#define	PK_EXIT_CODE_SYNTAX_INVALID	3
#define	PK_EXIT_CODE_FILE_NOT_FOUND	4

guint		 pk_console_get_number			(const gchar	*question,
							 guint		 maxnum);
gboolean	 pk_console_get_prompt			(const gchar	*question,
							 gboolean	 defaultyes);
PkPackageList	*pk_console_resolve			(PkBitfield	 filter,
							 const gchar	*package,
							 GError		**error);
gchar		*pk_console_resolve_package_id		(const PkPackageList *list,
							 GError		**error);

#endif /* __PK_TOOLS_COMMON_H */
