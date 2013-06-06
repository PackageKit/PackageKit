/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2013 Richard Hughes <richard@hughsie.com>
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

#ifndef __HIF_CONFIG_H
#define __HIF_CONFIG_H

#include <glib-object.h>

G_BEGIN_DECLS

#define HIF_TYPE_CONFIG		(hif_config_get_type ())
#define HIF_CONFIG(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), HIF_TYPE_CONFIG, HifConfig))
#define HIF_CONFIG_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), HIF_TYPE_CONFIG, HifConfigClass))
#define HIF_IS_CONFIG(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), HIF_TYPE_CONFIG))
#define HIF_IS_CONFIG_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), HIF_TYPE_CONFIG))
#define HIF_CONFIG_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), HIF_TYPE_CONFIG, HifConfigClass))

typedef struct _HifConfig		HifConfig;
typedef struct _HifConfigPrivate	HifConfigPrivate;
typedef struct _HifConfigClass		HifConfigClass;

struct _HifConfig
{
	GObject			 parent;
	HifConfigPrivate	*priv;
};

struct _HifConfigClass
{
	GObjectClass		 parent_class;
};

GType		 hif_config_get_type		(void);
HifConfig	*hif_config_new			(void);

gboolean	 hif_config_set_filename	(HifConfig	*config,
						 const gchar	*filename,
						 GError		**error);
gboolean	 hif_config_unset		(HifConfig	*config,
						 const gchar	*key,
						 GError		**error);
gchar		*hif_config_get_string		(HifConfig	*config,
						 const gchar	*key,
						 GError		**error);
gchar		**hif_config_get_strv		(HifConfig	*config,
						 const gchar	*key,
						 GError		**error);
gboolean	 hif_config_get_boolean		(HifConfig	*config,
						 const gchar	*key,
						 GError		**error);
guint		 hif_config_get_uint		(HifConfig	*config,
						 const gchar	*key,
						 GError		**error);
gboolean	 hif_config_set_local		(HifConfig	*config,
						 const gchar	*key,
						 const gchar	*value,
						 GError		**error);
gboolean	 hif_config_set_string		(HifConfig	*config,
						 const gchar	*key,
						 const gchar	*value,
						 GError		**error);
gboolean	 hif_config_set_boolean		(HifConfig	*config,
						 const gchar	*key,
						 gboolean	 value,
						 GError		**error);
gboolean	 hif_config_set_uint		(HifConfig	*config,
						 const gchar	*key,
						 guint		 value,
						 GError		**error);
gboolean	 hif_config_reset_default	(HifConfig	*config,
						 GError		**error);
gchar		**hif_config_get_basearch_array	(HifConfig	*config);

G_END_DECLS

#endif /* __HIF_CONFIG_H */
