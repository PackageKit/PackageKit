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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_OFFLINE_H
#define __PK_OFFLINE_H

#include <glib.h>
#include <gio/gio.h>

#include "pk-package-sack.h"
#include "pk-results.h"

G_BEGIN_DECLS

#define PK_OFFLINE_ERROR	(pk_offline_error_quark ())

/**
 * PkOfflineAction:
 * @PK_OFFLINE_ACTION_UNKNOWN:		Unknown
 * @PK_OFFLINE_ACTION_REBOOT:		Reboot
 * @PK_OFFLINE_ACTION_POWER_OFF:	Power-off
 * @PK_OFFLINE_ACTION_UNSET:		No action set
 *
 * Actions that can be taken after an offline operation.
 */
typedef enum {
	PK_OFFLINE_ACTION_UNKNOWN,
	PK_OFFLINE_ACTION_REBOOT,
	PK_OFFLINE_ACTION_POWER_OFF,
	PK_OFFLINE_ACTION_UNSET,
	/*< private >*/
	PK_OFFLINE_ACTION_LAST
} PkOfflineAction;

/**
 * PkOfflineError:
 * @PK_OFFLINE_ERROR_FAILED:		No specific reason
 * @PK_OFFLINE_ERROR_INVALID_VALUE:	An invalid value was specified
 * @PK_OFFLINE_ERROR_NO_DATA:		No data was available
 *
 * Errors that can be thrown
 */
typedef enum
{
	PK_OFFLINE_ERROR_FAILED,
	PK_OFFLINE_ERROR_INVALID_VALUE,
	PK_OFFLINE_ERROR_NO_DATA,
	/*< private >*/
	PK_OFFLINE_ERROR_LAST
} PkOfflineError;

/**
 * PkOfflineFlags:
 * @PK_OFFLINE_FLAGS_NONE:		No specific flag
 * @PK_OFFLINE_FLAGS_INTERACTIVE:	Run the action in an interactive mode, allowing polkit authentication dialogs
 *
 * Flags to be used for the method invocations.
 *
 * Since: 1.2.5
 */
typedef enum
{
	PK_OFFLINE_FLAGS_NONE		= 0,
	PK_OFFLINE_FLAGS_INTERACTIVE	= 1 << 0
} PkOfflineFlags;

GQuark			 pk_offline_error_quark		(void);
const gchar		*pk_offline_action_to_string	(PkOfflineAction	 action);
PkOfflineAction		 pk_offline_action_from_string	(const gchar		*action);
PkOfflineAction		 pk_offline_get_action		(GError			**error);
gchar			**pk_offline_get_prepared_ids	(GError			**error);
gchar			*pk_offline_get_prepared_upgrade_name
							(GError			**error);
gchar			*pk_offline_get_prepared_upgrade_version
							(GError			**error);
PkPackageSack		*pk_offline_get_prepared_sack	(GError			**error);
GFileMonitor		*pk_offline_get_prepared_monitor(GCancellable		*cancellable,
							 GError			**error);
GFileMonitor		*pk_offline_get_prepared_upgrade_monitor
							(GCancellable		*cancellable,
							 GError			**error);
GFileMonitor		*pk_offline_get_action_monitor	(GCancellable		*cancellable,
							 GError			**error);
PkResults		*pk_offline_get_results		(GError			**error);
guint64			 pk_offline_get_results_mtime	(GError			**error);
gboolean		 pk_offline_cancel		(GCancellable		*cancellable,
							 GError			**error);
gboolean		 pk_offline_cancel_with_flags	(PkOfflineFlags		 flags,
							 GCancellable		*cancellable,
							 GError			**error);
gboolean		 pk_offline_clear_results	(GCancellable		*cancellable,
							 GError			**error);
gboolean		 pk_offline_clear_results_with_flags
							(PkOfflineFlags		 flags,
							 GCancellable		*cancellable,
							 GError			**error);
gboolean		 pk_offline_trigger		(PkOfflineAction	 action,
							 GCancellable		*cancellable,
							 GError			**error);
gboolean		 pk_offline_trigger_with_flags	(PkOfflineAction	 action,
							 PkOfflineFlags		 flags,
							 GCancellable		*cancellable,
							 GError			**error);
gboolean		 pk_offline_trigger_upgrade	(PkOfflineAction	 action,
							 GCancellable		*cancellable,
							 GError			**error);
gboolean		 pk_offline_trigger_upgrade_with_flags
							(PkOfflineAction	 action,
							 PkOfflineFlags		 flags,
							 GCancellable		*cancellable,
							 GError			**error);

G_END_DECLS

#endif /* __PK_OFFLINE_H */
