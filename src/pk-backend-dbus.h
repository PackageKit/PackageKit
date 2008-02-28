/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_BACKEND_DBUS_H
#define __PK_BACKEND_DBUS_H

#include <glib-object.h>
#include <pk-enum-list.h>
#include "pk-backend.h"

G_BEGIN_DECLS

#define PK_TYPE_BACKEND_DBUS		(pk_backend_dbus_get_type ())
#define PK_BACKEND_DBUS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_BACKEND_DBUS, PkBackendDbus))
#define PK_BACKEND_DBUS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_BACKEND_DBUS, PkBackendDbusClass))
#define PK_IS_BACKEND_DBUS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_BACKEND_DBUS))
#define PK_IS_BACKEND_DBUS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_BACKEND_DBUS))
#define PK_BACKEND_DBUS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_BACKEND_DBUS, PkBackendDbusClass))

/**
 * PK_DBUS_BACKEND_INTERFACE:
 *
 * Interface to use for the dbus backend
 */
#define PK_DBUS_BACKEND_INTERFACE	"org.freedesktop.PackageKitBackend"

/**
 * PK_DBUS_BACKEND_PATH:
 *
 * Path to use for the dbus backend
 */
#define PK_DBUS_BACKEND_PATH		"/org/freedesktop/PackageKitBackend"

typedef struct PkBackendDbusPrivate PkBackendDbusPrivate;

typedef struct
{
	 GObject		 parent;
	 PkBackendDbusPrivate	*priv;
} PkBackendDbus;

typedef struct
{
	GObjectClass	parent_class;
} PkBackendDbusClass;

GType		 pk_backend_dbus_get_type		(void);
PkBackendDbus	*pk_backend_dbus_new			(void);
gboolean	 pk_backend_dbus_refresh_cache		(PkBackendDbus	*backend_dbus,
							 gboolean	 force);
gboolean	 pk_backend_dbus_update_system		(PkBackendDbus	*backend_dbus);
gboolean	 pk_backend_dbus_resolve		(PkBackendDbus	*backend_dbus,
							 const gchar	*filter,
							 const gchar	*package);
gboolean	 pk_backend_dbus_rollback		(PkBackendDbus	*backend_dbus,
							 const gchar	*transaction_id);
gboolean	 pk_backend_dbus_search_name		(PkBackendDbus	*backend_dbus,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_backend_dbus_search_details		(PkBackendDbus	*backend_dbus,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_backend_dbus_search_group		(PkBackendDbus	*backend_dbus,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_backend_dbus_search_file		(PkBackendDbus	*backend_dbus,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_backend_dbus_get_depends		(PkBackendDbus	*backend_dbus,
							 const gchar	*package_id,
							 gboolean	 recursive);
gboolean	 pk_backend_dbus_get_requires		(PkBackendDbus	*backend_dbus,
							 const gchar	*package_id,
							 gboolean	 recursive);
gboolean	 pk_backend_dbus_get_update_detail	(PkBackendDbus	*backend_dbus,
							 const gchar	*package_id);
gboolean	 pk_backend_dbus_get_description	(PkBackendDbus	*backend_dbus,
							 const gchar	*package_id);
gboolean	 pk_backend_dbus_get_files		(PkBackendDbus	*backend_dbus,
							 const gchar	*package_id);
gboolean	 pk_backend_dbus_remove_package		(PkBackendDbus	*backend_dbus,
							 const gchar	*package_id,
							 gboolean	 allow_deps,
							 gboolean	 autoremove);
gboolean	 pk_backend_dbus_install_package	(PkBackendDbus	*backend_dbus,
							 const gchar	*package_id);
gboolean	 pk_backend_dbus_update_package		(PkBackendDbus	*backend_dbus,
							 const gchar	*package_id);
gboolean	 pk_backend_dbus_install_file		(PkBackendDbus	*backend_dbus,
							 const gchar	*full_path);
gboolean	 pk_backend_dbus_service_pack		(PkBackendDbus	*backend_dbus,
							 const gchar	*location);
gboolean	 pk_backend_dbus_kill			(PkBackendDbus	*backend_dbus);
gboolean	 pk_backend_dbus_repo_enable		(PkBackendDbus	*backend_dbus,
							 const gchar	*rid,
							 gboolean	 enabled);
gboolean	 pk_backend_dbus_repo_set_data		(PkBackendDbus	*backend_dbus,
							 const gchar	*rid,
							 const gchar	*parameter,
							 const gchar	*value);
gboolean	 pk_backend_dbus_get_repo_list		(PkBackendDbus	*backend_dbus);
gboolean	 pk_backend_dbus_cancel			(PkBackendDbus	*backend_dbus);
gboolean	 pk_backend_dbus_get_updates		(PkBackendDbus	*backend_dbus,
							 const gchar	*filter);
gboolean	 pk_backend_dbus_set_name		(PkBackendDbus	*backend_dbus,
							 const gchar	*service);

G_END_DECLS

#endif /* __PK_BACKEND_DBUS_H */
