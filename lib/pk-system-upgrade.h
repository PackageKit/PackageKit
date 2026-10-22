/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(__PACKAGEKIT_H_INSIDE__) && !defined(PK_COMPILATION)
#error "Only <packagekit/packagekit.h> can be included directly."
#endif

#ifndef __PK_SYSTEM_UPGRADE_H
#define __PK_SYSTEM_UPGRADE_H

#include <glib-object.h>

#include "pk-source.h"
#include "pk-enum.h"

G_BEGIN_DECLS

#define PK_TYPE_SYSTEM_UPGRADE (pk_system_upgrade_get_type ())
#define PK_SYSTEM_UPGRADE(o) \
	(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_SYSTEM_UPGRADE, PkSystemUpgrade))
#define PK_SYSTEM_UPGRADE_CLASS(k) \
	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_SYSTEM_UPGRADE, PkSystemUpgradeClass))
#define PK_IS_SYSTEM_UPGRADE(o)	      (G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_SYSTEM_UPGRADE))
#define PK_IS_SYSTEM_UPGRADE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_SYSTEM_UPGRADE))
#define PK_SYSTEM_UPGRADE_GET_CLASS(o) \
	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_SYSTEM_UPGRADE, PkSystemUpgradeClass))

typedef struct _PkSystemUpgradePrivate PkSystemUpgradePrivate;
typedef struct _PkSystemUpgrade	       PkSystemUpgrade;
typedef struct _PkSystemUpgradeClass   PkSystemUpgradeClass;

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkSystemUpgrade, g_object_unref)
#endif

struct _PkSystemUpgrade
{
	PkSource		parent;
	PkSystemUpgradePrivate *priv;
};

struct _PkSystemUpgradeClass
{
	PkSourceClass parent_class;
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GType		  pk_system_upgrade_get_type (void);
PkSystemUpgrade	 *pk_system_upgrade_new (void);
PkSystemUpgrade	 *pk_system_upgrade_new_full (const gchar      *id,
					      const gchar      *os_id,
					      const gchar      *version,
					      const gchar      *name,
					      const gchar      *summary,
					      PkReleaseKindEnum kind,
					      guint64		download_size,
					      const gchar      *url);

/* accessors */
const gchar	 *pk_system_upgrade_get_id (PkSystemUpgrade *system_upgrade);
const gchar	 *pk_system_upgrade_get_os_id (PkSystemUpgrade *system_upgrade);
const gchar	 *pk_system_upgrade_get_version (PkSystemUpgrade *system_upgrade);
const gchar	 *pk_system_upgrade_get_name (PkSystemUpgrade *system_upgrade);
const gchar	 *pk_system_upgrade_get_summary (PkSystemUpgrade *system_upgrade);
const gchar	 *pk_system_upgrade_get_url (PkSystemUpgrade *system_upgrade);
PkReleaseKindEnum pk_system_upgrade_get_kind (PkSystemUpgrade *system_upgrade);
guint64		  pk_system_upgrade_get_download_size (PkSystemUpgrade *system_upgrade);

G_END_DECLS

#endif /* __PK_SYSTEM_UPGRADE_H */
