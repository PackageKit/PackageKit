/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_DESKTOP_H
#define __PK_DESKTOP_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_DESKTOP		(pk_desktop_get_type ())
#define PK_DESKTOP(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_DESKTOP, PkDesktop))
#define PK_DESKTOP_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_DESKTOP, PkDesktopClass))
#define PK_IS_DESKTOP(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_DESKTOP))
#define PK_IS_DESKTOP_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_DESKTOP))
#define PK_DESKTOP_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_DESKTOP, PkDesktopClass))

/**
 * PK_DESKTOP_DEFAULT_DATABASE:
 *
 * The default location for the database
 */
#define PK_DESKTOP_DEFAULT_DATABASE		LOCALSTATEDIR "/lib/PackageKit/desktop-files.db"

/**
 * PK_DESKTOP_DEFAULT_APPLICATION_DIR:
 *
 * The default location for the desktop files
 */
#ifndef __FreeBSD__
#define PK_DESKTOP_DEFAULT_APPLICATION_DIR	"/usr/share/applications"
#else
#define PK_DESKTOP_DEFAULT_APPLICATION_DIR	"/usr/local/share/applications"
#endif

typedef struct _PkDesktopPrivate	PkDesktopPrivate;
typedef struct _PkDesktop		PkDesktop;
typedef struct _PkDesktopClass		PkDesktopClass;

struct _PkDesktop
{
	GObject			 parent;
	PkDesktopPrivate	*priv;
};

struct _PkDesktopClass
{
	GObjectClass	parent_class;
};

GType		 pk_desktop_get_type			(void);
PkDesktop	*pk_desktop_new				(void);
void		 pk_desktop_test			(gpointer	 user_data);

gboolean	 pk_desktop_open_database		(PkDesktop	*desktop,
							 GError		**error);
GPtrArray	*pk_desktop_get_files_for_package	(PkDesktop	*desktop,
							 const gchar	*package,
							 GError		**error);
GPtrArray	*pk_desktop_get_shown_for_package	(PkDesktop	*desktop,
							 const gchar	*package,
							 GError		**error);
gchar		*pk_desktop_get_package_for_file	(PkDesktop	*desktop,
							 const gchar	*filename,
							 GError		**error);

G_END_DECLS

#endif /* __PK_DESKTOP_H */

