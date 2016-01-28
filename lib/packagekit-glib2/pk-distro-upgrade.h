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

#ifndef __PK_DISTRO_UPGRADE_H
#define __PK_DISTRO_UPGRADE_H

#include <glib-object.h>

#include <packagekit-glib2/pk-source.h>
#include <packagekit-glib2/pk-enum.h>

G_BEGIN_DECLS

#define PK_TYPE_DISTRO_UPGRADE		(pk_distro_upgrade_get_type ())
#define PK_DISTRO_UPGRADE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_DISTRO_UPGRADE, PkDistroUpgrade))
#define PK_DISTRO_UPGRADE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_DISTRO_UPGRADE, PkDistroUpgradeClass))
#define PK_IS_DISTRO_UPGRADE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_DISTRO_UPGRADE))
#define PK_IS_DISTRO_UPGRADE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_DISTRO_UPGRADE))
#define PK_DISTRO_UPGRADE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_DISTRO_UPGRADE, PkDistroUpgradeClass))

typedef struct _PkDistroUpgradePrivate	PkDistroUpgradePrivate;
typedef struct _PkDistroUpgrade		PkDistroUpgrade;
typedef struct _PkDistroUpgradeClass	PkDistroUpgradeClass;

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkDistroUpgrade, g_object_unref)
#endif

struct _PkDistroUpgrade
{
	 PkSource		 parent;
	 PkDistroUpgradePrivate	*priv;
};

struct _PkDistroUpgradeClass
{
	PkSourceClass	parent_class;
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GType		 pk_distro_upgrade_get_type	 	(void);
PkDistroUpgrade	*pk_distro_upgrade_new			(void);

/* accessors */
const gchar	*pk_distro_upgrade_get_id		(PkDistroUpgrade	*distro_upgrade);
const gchar	*pk_distro_upgrade_get_summary		(PkDistroUpgrade	*distro_upgrade);
PkDistroUpgradeEnum pk_distro_upgrade_get_state		(PkDistroUpgrade	*distro_upgrade);


G_END_DECLS

#endif /* __PK_DISTRO_UPGRADE_H */

