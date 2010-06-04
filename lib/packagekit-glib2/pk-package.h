/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_PACKAGE_H
#define __PK_PACKAGE_H

#include <glib-object.h>

#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-source.h>

G_BEGIN_DECLS

#define PK_TYPE_PACKAGE		(pk_package_get_type ())
#define PK_PACKAGE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_PACKAGE, PkPackage))
#define PK_PACKAGE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_PACKAGE, PkPackageClass))
#define PK_IS_PACKAGE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_PACKAGE))
#define PK_IS_PACKAGE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_PACKAGE))
#define PK_PACKAGE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_PACKAGE, PkPackageClass))
#define PK_PACKAGE_TYPE_ERROR	(pk_package_error_get_type ())

typedef struct _PkPackagePrivate	PkPackagePrivate;
typedef struct _PkPackage		PkPackage;
typedef struct _PkPackageClass		PkPackageClass;

struct _PkPackage
{
	 PkSource		 parent;
	 PkPackagePrivate	*priv;
};

struct _PkPackageClass
{
	PkSourceClass	parent_class;

	/* signals */
	void		(* changed)			(PkPackage	*package);
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GType		 pk_package_get_type		  	(void);
PkPackage	*pk_package_new				(void);
void		 pk_package_test			(gpointer	 user_data);

gboolean	 pk_package_set_id			(PkPackage	*package,
							 const gchar	*package_id,
							 GError		**error);
void		 pk_package_print			(PkPackage	*package);
gboolean	 pk_package_equal			(PkPackage	*package1,
							 PkPackage	*package2);
gboolean	 pk_package_equal_id			(PkPackage	*package1,
							 PkPackage	*package2);

/* accessors */
const gchar	*pk_package_get_id			(PkPackage	*package);
PkInfoEnum	 pk_package_get_info			(PkPackage	*package);
const gchar	*pk_package_get_summary			(PkPackage	*package);
const gchar	*pk_package_get_name			(PkPackage	*package);
const gchar	*pk_package_get_version			(PkPackage	*package);
const gchar	*pk_package_get_arch			(PkPackage	*package);
const gchar	*pk_package_get_data			(PkPackage	*package);

G_END_DECLS

#endif /* __PK_PACKAGE_H */

