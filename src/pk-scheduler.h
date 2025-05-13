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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __PK_SCHEDULER_H
#define __PK_SCHEDULER_H

#include <glib-object.h>
#include <packagekit-glib2/pk-enum.h>

#include "pk-transaction.h"

G_BEGIN_DECLS

#define PK_TYPE_SCHEDULER		(pk_scheduler_get_type ())
G_DECLARE_FINAL_TYPE (PkScheduler, pk_scheduler, PK, SCHEDULER, GObject)

#define PK_SCHEDULER_ERROR		(pk_scheduler_error_quark ())
#define PK_SCHEDULER_TYPE_ERROR	(pk_scheduler_error_get_type ())


PkScheduler	*pk_scheduler_new		(GKeyFile	*conf);

gboolean	 pk_scheduler_create		(PkScheduler	*scheduler,
						 const gchar	*tid,
						 const gchar	*sender,
						 GError		**error);
gboolean	 pk_scheduler_remove		(PkScheduler	*scheduler,
						 const gchar	*tid);
gboolean	 pk_scheduler_role_present	(PkScheduler	*scheduler,
						 PkRoleEnum	 role);
gchar		**pk_scheduler_get_array	(PkScheduler	*scheduler)
						 G_GNUC_WARN_UNUSED_RESULT;
gchar		*pk_scheduler_get_state		(PkScheduler	*scheduler)
						 G_GNUC_WARN_UNUSED_RESULT;
guint		 pk_scheduler_get_size		(PkScheduler	*scheduler);
gboolean	 pk_scheduler_get_locked	(PkScheduler	*scheduler);
gboolean	 pk_scheduler_get_inhibited	(PkScheduler	*scheduler);
PkTransaction	*pk_scheduler_get_transaction	(PkScheduler	*scheduler,
						 const gchar	*tid);
void		 pk_scheduler_cancel_background	(PkScheduler	*scheduler);
void		 pk_scheduler_cancel_queued	(PkScheduler	*scheduler);
void		 pk_scheduler_set_backend	(PkScheduler	*scheduler,
						 PkBackend	*backend);

G_END_DECLS

#endif /* __PK_SCHEDULER_H */
