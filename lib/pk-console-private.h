/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2026 Matthias Klumpp <matthias@tenstral.net>
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

#ifndef __PK_CONSOLE_PRIVATE_H
#define __PK_CONSOLE_PRIVATE_H

#include <glib.h>
#include "pk-client.h"
#include "pk-bitfield.h"
#include "pk-enum.h"

G_BEGIN_DECLS

guint  pk_console_str_width (const gchar *text);
gchar *pk_console_text_truncate (const gchar *text,
				 guint	      max_width);
gchar *pk_console_strpad (const gchar *text,
			  guint	       width);

#define PK_CONSOLE_DEFAULT_WIDTH 100

/* values longer than this do not widen their column in package lists */
#define PK_CONSOLE_NAME_WIDTH_MAX    30
#define PK_CONSOLE_VERSION_WIDTH_MAX 20
#define PK_CONSOLE_ARCH_WIDTH_MAX    8
#define PK_CONSOLE_ORIGIN_WIDTH_MAX  20

guint			      pk_console_get_width (void);
gchar			     *pk_console_text_wrap (const gchar *text,
						    guint	 width,
						    guint	 indent);

gint			      pk_console_package_sort_func (gconstpointer a,
							    gconstpointer b);

typedef struct PkConsoleTable PkConsoleTable;

PkConsoleTable		     *pk_console_table_new (guint n_columns);
void			      pk_console_table_free (PkConsoleTable *table);
void			      pk_console_table_set_header (PkConsoleTable *table,
							   const gchar	  *first,
							   ...);
void			      pk_console_table_set_indent (PkConsoleTable *table,
							   guint	   indent);
void			      pk_console_table_set_flexible (PkConsoleTable *table,
							     guint	     column);
void			      pk_console_table_set_spacing (PkConsoleTable *table,
							    guint	    column,
							    guint	    spacing);
void			      pk_console_table_set_max_width (PkConsoleTable *table,
							      guint	      column,
							      guint	      max_width);
void			      pk_console_table_add_row (PkConsoleTable *table,
							const gchar    *first,
							...);
void			      pk_console_table_add_text (PkConsoleTable *table,
							 const gchar	*text);
void			      pk_console_table_set_color (PkConsoleTable *table,
							  guint		  column,
							  const gchar	 *color);
gchar			     *pk_console_table_render (PkConsoleTable *table,
						       guint	       max_width,
						       gboolean	       colors);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PkConsoleTable, pk_console_table_free)

guint	 pk_console_get_number (const gchar *question,
				guint	     maxnum);
gboolean pk_console_get_prompt (const gchar *question,
				gboolean     defaultyes);

G_END_DECLS

#endif /* __PK_CONSOLE_PRIVATE_H */
