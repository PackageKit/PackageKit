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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __PK_ENGINE_H
#define __PK_ENGINE_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

#define	PK_DBUS_SERVICE			"org.freedesktop.PackageKit"
#define	PK_DBUS_PATH			"/org/freedesktop/PackageKit"
#define	PK_DBUS_INTERFACE		"org.freedesktop.PackageKit"

G_BEGIN_DECLS

#define PK_TYPE_ENGINE		(pk_engine_get_type ())
#define PK_ENGINE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_ENGINE, PkEngine))
#define PK_ENGINE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_ENGINE, PkEngineClass))
#define PK_IS_ENGINE(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_ENGINE))
#define PK_IS_ENGINE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_ENGINE))
#define PK_ENGINE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_ENGINE, PkEngineClass))
#define PK_ENGINE_ERROR		(pk_engine_error_quark ())
#define PK_ENGINE_TYPE_ERROR	(pk_engine_error_get_type ())

typedef struct PkEnginePrivate PkEnginePrivate;

typedef struct
{
	 GObject		 parent;
	 PkEnginePrivate	*priv;
} PkEngine;

typedef struct
{
	GObjectClass	parent_class;
} PkEngineClass;

typedef enum
{
	PK_ENGINE_ERROR_DENIED,
	PK_ENGINE_ERROR_NOT_SUPPORTED,
	PK_ENGINE_ERROR_NO_SUCH_JOB,
	PK_ENGINE_ERROR_REFUSED_BY_POLICY,
	PK_ENGINE_ERROR_PACKAGE_ID_INVALID,
	PK_ENGINE_ERROR_LAST
} PkEngineError;

GQuark		 pk_engine_error_quark			(void);
GType		 pk_engine_error_get_type		(void);
GType		 pk_engine_get_type		  	(void);
PkEngine	*pk_engine_new				(void);

gboolean	 pk_engine_get_updates			(PkEngine	*engine,
							 guint		*job,
							 GError		**error);
gboolean	 pk_engine_search_name			(PkEngine	*engine,
							 const gchar	*filter,
							 const gchar	*search,
							 guint		*job,
							 GError		**error);
gboolean	 pk_engine_search_details		(PkEngine	*engine,
							 const gchar	*filter,
							 const gchar	*search,
							 guint		*job,
							 GError		**error);
gboolean	 pk_engine_search_group			(PkEngine	*engine,
							 const gchar	*filter,
							 const gchar	*search,
							 guint		*job,
							 GError		**error);
gboolean	 pk_engine_search_file			(PkEngine	*engine,
							 const gchar	*filter,
							 const gchar	*search,
							 guint		*job,
							 GError		**error);
gboolean	 pk_engine_get_deps			(PkEngine	*engine,
							 const gchar	*package_id,
							 guint		*job,
							 GError		**error);
gboolean	 pk_engine_get_description		(PkEngine	*engine,
							 const gchar	*package_id,
							 guint		*job,
							 GError		**error);
gboolean	 pk_engine_refresh_cache		(PkEngine	*engine,
							 gboolean	 force,
							 guint		*job,
							 GError		**error);
void		 pk_engine_update_system		(PkEngine	*engine,
							 DBusGMethodInvocation *context,
							 GError		**error);
void		 pk_engine_remove_package		(PkEngine	*engine,
							 const gchar	*package_id,
							 gboolean	 allow_deps,
							 DBusGMethodInvocation *context,
							 GError		**error);
void		 pk_engine_install_package		(PkEngine	*engine,
							 const gchar	*package_id,
							 DBusGMethodInvocation *context,
							 GError		**error);
void		 pk_engine_update_package		(PkEngine	*engine,
							 const gchar	*package_id,
							 DBusGMethodInvocation *context,
							 GError		**error);
gboolean	 pk_engine_get_job_list			(PkEngine	*engine,
							 GArray		**job_list,
							 GError		**error);
gboolean	 pk_engine_get_job_status		(PkEngine	*engine,
							 guint		 job,
							 const gchar	**status,
							 const gchar	**package,
							 GError		**error);
gboolean	 pk_engine_cancel_job_try		(PkEngine	*engine,
							 guint		 job,
							 GError		**error);
gboolean	 pk_engine_get_actions			(PkEngine	*engine,
							 gchar		**actions,
							 GError		**error);
guint		 pk_engine_get_seconds_idle		(PkEngine	*engine);

G_END_DECLS

#endif /* __PK_ENGINE_H */
