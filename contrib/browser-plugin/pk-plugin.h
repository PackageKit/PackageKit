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

#ifndef __PK_PLUGIN_H
#define __PK_PLUGIN_H

#include <glib-object.h>
#include <cairo-xlib.h>

G_BEGIN_DECLS

#define PK_TYPE_PLUGIN		(pk_plugin_get_type ())
#define PK_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_PLUGIN, PkPlugin))
#define PK_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_PLUGIN, PkPluginClass))
#define PK_IS_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_PLUGIN))
#define PK_IS_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_PLUGIN))
#define PK_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_PLUGIN, PkPluginClass))

typedef struct PkPluginPrivate PkPluginPrivate;

typedef struct
{
	GObject		 parent;
	PkPluginPrivate	*priv;
} PkPlugin;

typedef struct
{
	GObjectClass	 parent_class;
	/* signals */
	void		 (*refresh)		(PkPlugin	*plugin);

	/* vtable */
	gboolean	 (*start)		(PkPlugin	*plugin);
	gboolean	 (*draw)		(PkPlugin	*plugin,
						 cairo_t	*cr);
	gboolean	 (*button_press)	(PkPlugin	*plugin,
						 gint		 x,
						 gint		 y,
						 Time		 event_time);
	gboolean	 (*button_release)	(PkPlugin	*plugin,
						 gint		 x,
						 gint		 y,
						 Time		 event_time);
	gboolean	 (*motion)		(PkPlugin	*plugin,
						 gint		 x,
						 gint		 y);
	gboolean	 (*enter)		(PkPlugin	*plugin,
						 gint		 x,
						 gint		 y);
	gboolean	 (*leave)		(PkPlugin	*plugin,
						 gint		 x,
						 gint		 y);
} PkPluginClass;

GType		 pk_plugin_get_type		(void);
PkPlugin	*pk_plugin_new			(void);
gboolean	 pk_plugin_set_data		(PkPlugin	*plugin,
						 const gchar	*name,
						 const gchar	*value);
const gchar	*pk_plugin_get_data		(PkPlugin	*plugin,
						 const gchar	*name);
gboolean	 pk_plugin_start		(PkPlugin	*plugin);
gboolean	 pk_plugin_draw			(PkPlugin	*plugin,
						 cairo_t	*cr);
gboolean	 pk_plugin_button_press		(PkPlugin	*plugin,
						 gint		 x,
						 gint		 y,
						 Time		 event_time);
gboolean	 pk_plugin_button_release	(PkPlugin	*plugin,
						 gint		 x,
						 gint		 y,
						 Time		 event_time);
gboolean	 pk_plugin_motion		(PkPlugin	*plugin,
						 gint		 x,
						 gint		 y);
gboolean	 pk_plugin_enter		(PkPlugin	*plugin,
						 gint		 x,
						 gint		 y);
gboolean	 pk_plugin_leave		(PkPlugin	*plugin,
						 gint		 x,
						 gint		 y);
gboolean	 pk_plugin_request_refresh	(PkPlugin	*plugin);

G_END_DECLS

#endif /* __PK_PLUGIN_H */
