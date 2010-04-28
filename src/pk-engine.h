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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
	PK_ENGINE_ERROR_INVALID_STATE,
	PK_ENGINE_ERROR_REFUSED_BY_POLICY,
	PK_ENGINE_ERROR_CANNOT_SET_PROXY,
	PK_ENGINE_ERROR_CANNOT_SET_ROOT,
	PK_ENGINE_ERROR_NOT_SUPPORTED,
	PK_ENGINE_ERROR_CANNOT_ALLOCATE_TID,
	PK_ENGINE_ERROR_CANNOT_CHECK_AUTH,
	PK_ENGINE_ERROR_LAST
} PkEngineError;


GQuark		 pk_engine_error_quark			(void);
GType		 pk_engine_error_get_type		(void);
GType		 pk_engine_get_type		  	(void);
PkEngine	*pk_engine_new				(void);

/* general */
guint		 pk_engine_get_seconds_idle		(PkEngine	*engine);

/* dbus methods */
void		 pk_engine_get_tid			(PkEngine	*engine,
							 DBusGMethodInvocation *context);
gboolean	 pk_engine_get_daemon_state		(PkEngine	*engine,
							 gchar		**state,
							 GError		**error);
gboolean	 pk_engine_get_time_since_action	(PkEngine	*engine,
							 const gchar	*role_text,
							 guint		*seconds,
							 GError		**error);
gboolean	 pk_engine_get_transaction_list		(PkEngine	*engine,
							 gchar		***transaction_list,
							 GError		**error);
gboolean	 pk_engine_state_has_changed		(PkEngine	*engine,
							 const gchar	*reason,
							 GError		**error);
gboolean	 pk_engine_suggest_daemon_quit		(PkEngine	*engine,
							 GError		**error);
void		 pk_engine_set_proxy			(PkEngine	*engine,
							 const gchar	*proxy_http,
							 const gchar	*proxy_ftp,
							 DBusGMethodInvocation *context);
void		 pk_engine_set_root			(PkEngine	*engine,
							 const gchar	*root,
							 DBusGMethodInvocation *context);
void		 pk_engine_can_authorize		(PkEngine	*engine,
							 const gchar	*action_id,
							 DBusGMethodInvocation *context);

G_END_DECLS

#endif /* __PK_ENGINE_H */
