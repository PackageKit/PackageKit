/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_TIME_H
#define __PK_TIME_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_TIME		(pk_time_get_type ())
#define PK_TIME(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TIME, PkTime))
#define PK_TIME_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TIME, PkTimeClass))
#define PK_IS_TIME(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TIME))
#define PK_IS_TIME_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TIME))
#define PK_TIME_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TIME, PkTimeClass))
#define PK_TIME_ERROR		(pk_time_error_quark ())
#define PK_TIME_TYPE_ERROR	(pk_time_error_get_type ())

typedef struct PkTimePrivate PkTimePrivate;

typedef struct
{
	 GObject		 parent;
	 PkTimePrivate		*priv;
} PkTime;

typedef struct
{
	GObjectClass	parent_class;
} PkTimeClass;

GType		 pk_time_get_type		  	(void);
PkTime		*pk_time_new				(void);

gboolean	 pk_time_add_data			(PkTime		*pktime,
							 guint		 percentage);
void		 pk_time_advance_clock			(PkTime		*pktime,
							 guint		 offset);
gboolean	 pk_time_reset				(PkTime		*pktime);
guint		 pk_time_get_elapsed			(PkTime		*pktime);
guint		 pk_time_get_remaining			(PkTime		*pktime);
gboolean	 pk_time_set_average_limits		(PkTime		*pktime,
							 guint		 average_min,
							 guint		 average_max);
gboolean	 pk_time_set_value_limits		(PkTime		*pktime,
							 guint		 value_min,
							 guint		 value_max);

G_END_DECLS

#endif /* __PK_TIME_H */
