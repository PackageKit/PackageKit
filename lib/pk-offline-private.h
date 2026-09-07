/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
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

#if !defined(__PACKAGEKIT_H_INSIDE__) && !defined(PK_COMPILATION)
#error "Only <packagekit-glib2/packagekit.h> can be included directly."
#endif

#ifndef __PK_OFFLINE_PRIVATE_H
#define __PK_OFFLINE_PRIVATE_H

/* FIXME: these have to remain here (and not in src until we remove the
 * pkexec helpers from contrib */

#include <glib.h>

#include "pk-offline.h"
#include "pk-results.h"

G_BEGIN_DECLS

/* the state files below live under the root directory set with
 * pk_offline_set_root_dir(), which is "/" unless the daemon or the self tests
 * point it somewhere else */
void	     pk_offline_set_root_dir (const gchar *root_dir);

/* the state file for regular offline update */
const gchar *pk_offline_get_prepared_filename (void);
/* the state file for offline system upgrade */
const gchar *pk_offline_get_prepared_upgrade_filename (void);
/* the trigger file that systemd uses to start a different boot target */
const gchar *pk_offline_get_trigger_filename (void);
/* the keyfile describing the outcome of the latest offline update */
const gchar *pk_offline_get_results_filename (void);
/* the action to take when the offline update has completed, e.g. restart */
const gchar *pk_offline_get_action_filename (void);

/* the group name for the offline updates results keyfile */
#define PK_OFFLINE_RESULTS_GROUP "PackageKit Offline Update Results"

gboolean pk_offline_auth_set_action (PkOfflineAction action,
				     GError	   **error);
gboolean pk_offline_auth_cancel (GError **error);
gboolean pk_offline_auth_clear_results (GError **error);
gboolean pk_offline_auth_trigger (PkOfflineAction action,
				  GError	**error);
gboolean pk_offline_auth_trigger_upgrade (PkOfflineAction action,
					  GError	**error);
gboolean pk_offline_auth_set_prepared_ids (gchar  **package_ids,
					   GError **error);
gboolean pk_offline_auth_set_prepared_upgrade (const gchar *name,
					       const gchar *release_ver,
					       GError	  **error);
gboolean pk_offline_get_prepared_upgrade (gchar	 **name,
					  gchar	 **release_ver,
					  GError **error);
gboolean pk_offline_auth_invalidate (GError **error);
gboolean pk_offline_auth_set_results (PkResults *results,
				      GError   **error);

G_END_DECLS

#endif /* __PK_OFFLINE_PRIVATE_H */
