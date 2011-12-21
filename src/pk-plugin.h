/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_PLUGIN_H
#define __PK_PLUGIN_H

#include <glib-object.h>

#include "pk-backend.h"
#include "pk-transaction.h"

G_BEGIN_DECLS

typedef struct PkPluginPrivate PkPluginPrivate;

typedef struct {
	GModule			*module;
	PkBackend		*backend;
	PkPluginPrivate		*priv;
} PkPlugin;


typedef enum {
	PK_PLUGIN_PHASE_INIT,				/* plugin started */
	PK_PLUGIN_PHASE_TRANSACTION_CONTENT_TYPES,	/* for adding content types */
	PK_PLUGIN_PHASE_TRANSACTION_RUN,		/* only this running */
	PK_PLUGIN_PHASE_TRANSACTION_STARTED,		/* all signals connected */
	PK_PLUGIN_PHASE_TRANSACTION_FINISHED_START,	/* finshed with all signals */
	PK_PLUGIN_PHASE_TRANSACTION_FINISHED_RESULTS,	/* finished with some signals */
	PK_PLUGIN_PHASE_TRANSACTION_FINISHED_END,	/* finished with no signals */
	PK_PLUGIN_PHASE_DESTROY,			/* plugin finalized */
	PK_PLUGIN_PHASE_UNKNOWN
} PkPluginPhase;

/* this is used to connect/disconnect backend signals */
typedef enum {
	PK_BACKEND_SIGNAL_ALLOW_CANCEL,
	PK_BACKEND_SIGNAL_DETAILS,
	PK_BACKEND_SIGNAL_ERROR_CODE,
	PK_BACKEND_SIGNAL_DISTRO_UPGRADE,
	PK_BACKEND_SIGNAL_FINISHED,
	PK_BACKEND_SIGNAL_MESSAGE,
	PK_BACKEND_SIGNAL_PACKAGE,
	PK_BACKEND_SIGNAL_FILES,
	PK_BACKEND_SIGNAL_NOTIFY_PERCENTAGE,
	PK_BACKEND_SIGNAL_NOTIFY_SUBPERCENTAGE,
	PK_BACKEND_SIGNAL_NOTIFY_REMAINING,
	PK_BACKEND_SIGNAL_NOTIFY_SPEED,
	PK_BACKEND_SIGNAL_REPO_DETAIL,
	PK_BACKEND_SIGNAL_REPO_SIGNATURE_REQUIRED,
	PK_BACKEND_SIGNAL_EULA_REQUIRED,
	PK_BACKEND_SIGNAL_MEDIA_CHANGE_REQUIRED,
	PK_BACKEND_SIGNAL_REQUIRE_RESTART,
	PK_BACKEND_SIGNAL_STATUS_CHANGED,
	PK_BACKEND_SIGNAL_UPDATE_DETAIL,
	PK_BACKEND_SIGNAL_CATEGORY,
	PK_BACKEND_SIGNAL_ITEM_PROGRESS
} PkBackendSignal;

#define	PK_TRANSACTION_PLUGIN_GET_PRIVATE(x)		g_new0 (x,1)

typedef const gchar	*(*PkPluginGetDescFunc)		(void);
typedef void		 (*PkPluginFunc)		(PkPlugin	*plugin);
typedef void		 (*PkPluginTransactionFunc)	(PkPlugin	*plugin,
							 PkTransaction	*transaction);

const gchar	*pk_plugin_get_description		(void);
void		 pk_plugin_initialize			(PkPlugin	*plugin);
void		 pk_plugin_destroy			(PkPlugin	*plugin);
void		 pk_plugin_transaction_run		(PkPlugin	*plugin,
							 PkTransaction	*transaction);
void		 pk_plugin_transaction_started		(PkPlugin	*plugin,
							 PkTransaction	*transaction);
void		 pk_plugin_transaction_finished_start	(PkPlugin	*plugin,
							 PkTransaction	*transaction);
void		 pk_plugin_transaction_finished_results	(PkPlugin	*plugin,
							 PkTransaction	*transaction);
void		 pk_plugin_transaction_finished_end	(PkPlugin	*plugin,
							 PkTransaction	*transaction);
void		 pk_plugin_transaction_content_types	(PkPlugin	*plugin,
							 PkTransaction	*transaction);

G_END_DECLS

#endif /* __PK_PLUGIN_H */
