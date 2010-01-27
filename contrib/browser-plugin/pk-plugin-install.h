/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2008 Red Hat, Inc.
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

#ifndef __PK_PLUGIN_INSTALL_H
#define __PK_PLUGIN_INSTALL_H

#include <glib-object.h>
#include <cairo-xlib.h>

#include "pk-plugin.h"

G_BEGIN_DECLS

#define PK_TYPE_PLUGIN_INSTALL		(pk_plugin_install_get_type ())
#define PK_PLUGIN_INSTALL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_PLUGIN_INSTALL, PkPluginInstall))
#define PK_PLUGIN_INSTALL_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_PLUGIN_INSTALL, PkPluginInstallClass))
#define PK_IS_PLUGIN_INSTALL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_PLUGIN_INSTALL))
#define PK_IS_PLUGIN_INSTALL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_PLUGIN_INSTALL))
#define PK_PLUGIN_INSTALL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_PLUGIN_INSTALL, PkPluginInstallClass))

typedef struct PkPluginInstallPrivate PkPluginInstallPrivate;

typedef struct
{
	PkPlugin		 parent;
	PkPluginInstallPrivate	*priv;
} PkPluginInstall;

typedef struct
{
	PkPluginClass		 parent_class;
} PkPluginInstallClass;

GType		 pk_plugin_install_get_type		(void);
PkPluginInstall	*pk_plugin_install_new			(void);

G_END_DECLS

#endif /* __PK_PLUGIN_INSTALL_H */
