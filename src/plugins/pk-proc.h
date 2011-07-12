/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_PROC_H
#define __PK_PROC_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_PROC		(pk_proc_get_type ())
#define PK_PROC(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_PROC, PkProc))
#define PK_PROC_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_PROC, PkProcClass))
#define PK_IS_PROC(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_PROC))
#define PK_IS_PROC_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_PROC))
#define PK_PROC_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_PROC, PkProcClass))

typedef struct PkProcPrivate PkProcPrivate;

typedef struct
{
	GObject		 parent;
	PkProcPrivate	*priv;
} PkProc;

typedef struct
{
	GObjectClass	parent_class;
} PkProcClass;

GType		 pk_proc_get_type			(void);
PkProc		*pk_proc_new				(void);

gboolean	 pk_proc_refresh			(PkProc		*proc);
gchar		*pk_proc_get_process_for_cmdlines	(PkProc		*proc,
							 gchar		**filenames);
gboolean	 pk_proc_find_execs			(PkProc		*proc,
							 gchar		**filenames);
gboolean	 pk_proc_find_exec			(PkProc		*proc,
							 const gchar	*filename);

G_END_DECLS

#endif /* __PK_PROC_H */

