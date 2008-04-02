/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_EXTRA_H
#define __PK_EXTRA_H

#include <glib-object.h>
#include "pk-enum.h"
#include "pk-enum-list.h"
#include "pk-package-list.h"

G_BEGIN_DECLS

#define PK_TYPE_EXTRA		(pk_extra_get_type ())
#define PK_EXTRA(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_EXTRA, PkExtra))
#define PK_EXTRA_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_EXTRA, PkExtraClass))
#define PK_IS_EXTRA(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_EXTRA))
#define PK_IS_EXTRA_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_EXTRA))
#define PK_EXTRA_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_EXTRA, PkExtraClass))

/**
 * PK_EXTRA_DEFAULT_DATABASE:
 *
 * The default location for the database, for client convenience
 */
#define PK_EXTRA_DEFAULT_DATABASE	"/var/lib/PackageKit/extra-data.db"

typedef struct _PkExtraPrivate		PkExtraPrivate;
typedef struct _PkExtra			PkExtra;
typedef struct _PkExtraClass		PkExtraClass;

struct _PkExtra
{
	GObject		 parent;
	PkExtraPrivate	*priv;
};

struct _PkExtraClass
{
	GObjectClass	parent_class;
};

GType		 pk_extra_get_type			(void) G_GNUC_CONST;
PkExtra		*pk_extra_new				(void);

gboolean	 pk_extra_set_locale			(PkExtra	*extra,
							 const gchar	*locale);
const gchar	*pk_extra_get_locale			(PkExtra	*extra);
gboolean	 pk_extra_set_database			(PkExtra	*extra,
							 const gchar	*filename)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_extra_get_localised_detail		(PkExtra	*extra,
							 const gchar	*package,
							 gchar		**summary);
gboolean	 pk_extra_set_localised_detail		(PkExtra	*extra,
							 const gchar	*package,
							 const gchar	*summary);
gboolean	 pk_extra_get_package_detail		(PkExtra	*extra,
							 const gchar	*package,
							 gchar		**icon,
							 gchar		**exec);
gboolean	 pk_extra_set_package_detail		(PkExtra	*extra,
							 const gchar	*package,
							 const gchar	*icon,
							 const gchar	*exec);

G_END_DECLS

#endif /* __PK_EXTRA_H */
