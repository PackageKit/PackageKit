/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_PROGRESS_BAR_H
#define __PK_PROGRESS_BAR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_PROGRESS_BAR		(pk_progress_bar_get_type ())
#define PK_PROGRESS_BAR(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_PROGRESS_BAR, PkProgressBar))
#define PK_PROGRESS_BAR_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_PROGRESS_BAR, PkProgressBarClass))
#define PK_IS_PROGRESS_BAR(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_PROGRESS_BAR))
#define PK_IS_PROGRESS_BAR_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_PROGRESS_BAR))
#define PK_PROGRESS_BAR_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_PROGRESS_BAR, PkProgressBarClass))

typedef struct PkProgressBarPrivate PkProgressBarPrivate;

typedef struct
{
	GObject			 parent;
	PkProgressBarPrivate	*priv;
} PkProgressBar;

typedef struct
{
	GObjectClass		 parent_class;
} PkProgressBarClass;

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkProgressBar, g_object_unref)
#endif

GType		 pk_progress_bar_get_type		(void);
PkProgressBar	*pk_progress_bar_new			(void);
gboolean	 pk_progress_bar_set_size		(PkProgressBar	*progress_bar,
							 guint		 size);
gboolean	 pk_progress_bar_set_padding		(PkProgressBar	*progress_bar,
							 guint		 padding);
gboolean	 pk_progress_bar_set_percentage		(PkProgressBar	*progress_bar,
							 gint		 percentage);
gboolean	 pk_progress_bar_start			(PkProgressBar	*progress_bar,
							 const gchar	*text);
gboolean	 pk_progress_bar_end			(PkProgressBar	*progress_bar);

G_END_DECLS

#endif /* __PK_PROGRESS_BAR_H */
