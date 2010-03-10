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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#if !defined (__ZIF_H_INSIDE__) && !defined (ZIF_COMPILATION)
#error "Only <zif.h> can be included directly."
#endif

#ifndef __ZIF_CONFIG_H
#define __ZIF_CONFIG_H

#include <glib-object.h>

G_BEGIN_DECLS

#define ZIF_TYPE_CONFIG		(zif_config_get_type ())
#define ZIF_CONFIG(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_CONFIG, ZifConfig))
#define ZIF_CONFIG_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_CONFIG, ZifConfigClass))
#define ZIF_IS_CONFIG(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_CONFIG))
#define ZIF_IS_CONFIG_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_CONFIG))
#define ZIF_CONFIG_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_CONFIG, ZifConfigClass))
#define ZIF_CONFIG_ERROR	(zif_config_error_quark ())

typedef struct _ZifConfig		ZifConfig;
typedef struct _ZifConfigPrivate	ZifConfigPrivate;
typedef struct _ZifConfigClass		ZifConfigClass;

struct _ZifConfig
{
	GObject			 parent;
	ZifConfigPrivate	*priv;
};

struct _ZifConfigClass
{
	GObjectClass		 parent_class;
};

typedef enum {
	ZIF_CONFIG_ERROR_FAILED,
	ZIF_CONFIG_ERROR_LAST
} ZifConfigError;

GQuark		 zif_config_error_quark		(void);
GType		 zif_config_get_type		(void);
ZifConfig	*zif_config_new			(void);
gboolean	 zif_config_set_filename	(ZifConfig	*config,
						 const gchar	*filename,
						 GError		**error);
gchar		*zif_config_get_string		(ZifConfig	*config,
						 const gchar	*key,
						 GError		**error);
gboolean	 zif_config_get_boolean		(ZifConfig	*config,
						 const gchar	*key,
						 GError		**error);
guint		 zif_config_get_uint		(ZifConfig	*config,
						 const gchar	*key,
						 GError		**error);
guint		 zif_config_get_time		(ZifConfig	*config,
						 const gchar	*key,
						 GError		**error);
gboolean	 zif_config_set_local		(ZifConfig	*config,
						 const gchar	*key,
						 const gchar	*value,
						 GError		**error);
gboolean	 zif_config_reset_default	(ZifConfig	*config,
						 GError		**error);
gchar		*zif_config_expand_substitutions (ZifConfig	*config,
						 const gchar	*text,
						 GError		**error);
gchar		**zif_config_get_basearch_array	(ZifConfig	*config);

G_END_DECLS

#endif /* __ZIF_CONFIG_H */
