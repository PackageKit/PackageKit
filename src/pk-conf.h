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

#ifndef __PK_CONF_H
#define __PK_CONF_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_CONF		(pk_conf_get_type ())
#define PK_CONF(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CONF, PkConf))
#define PK_CONF_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CONF, PkConfClass))
#define PK_IS_CONF(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CONF))
#define PK_IS_CONF_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CONF))
#define PK_CONF_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CONF, PkConfClass))

typedef struct PkConfPrivate PkConfPrivate;

typedef struct
{
	GObject		      parent;
	PkConfPrivate	     *priv;
} PkConf;

typedef struct
{
	GObjectClass	parent_class;
} PkConfClass;

#define PK_CONF_VALUE_INT_MISSING	-1
#define PK_CONF_VALUE_STRING_MISSING	NULL

GType		 pk_conf_get_type		(void);
PkConf		*pk_conf_new			(void);

gchar		*pk_conf_get_filename		(void);
gchar		*pk_conf_get_string		(PkConf		*conf,
						 const gchar	*key)
						 G_GNUC_WARN_UNUSED_RESULT;
gchar		**pk_conf_get_strv		(PkConf		*conf,
						 const gchar	*key)
						 G_GNUC_WARN_UNUSED_RESULT;
gint		 pk_conf_get_int		(PkConf		*conf,
						 const gchar	*key);
gboolean	 pk_conf_get_bool		(PkConf		*conf,
						 const gchar	*key);

void		 pk_conf_set_bool		(PkConf		*conf,
						 const gchar	*key,
						 gboolean	 value);
void		 pk_conf_set_string		(PkConf		*conf,
						 const gchar	*key,
						 const gchar	*value);

G_END_DECLS

#endif /* __PK_CONF_H */
