/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2013 Richard Hughes <richard@hughsie.com>
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

#ifndef __HIF_STATE_H
#define __HIF_STATE_H

#include <glib-object.h>
#include <gio/gio.h>

#include "hif-lock.h"

G_BEGIN_DECLS

#define HIF_TYPE_STATE		(hif_state_get_type ())
#define HIF_STATE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), HIF_TYPE_STATE, HifState))
#define HIF_STATE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), HIF_TYPE_STATE, HifStateClass))
#define HIF_IS_STATE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), HIF_TYPE_STATE))
#define HIF_IS_STATE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), HIF_TYPE_STATE))
#define HIF_STATE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), HIF_TYPE_STATE, HifStateClass))

typedef struct _HifState	HifState;
typedef struct _HifStatePrivate	HifStatePrivate;
typedef struct _HifStateClass	HifStateClass;

struct _HifState
{
	GObject			 parent;
	HifStatePrivate		*priv;
};

struct _HifStateClass
{
	GObjectClass	 parent_class;
	/* Signals */
	void		(* percentage_changed)		(HifState	*state,
							 guint		 value);
	void		(* subpercentage_changed)	(HifState	*state,
							 guint		 value);
	void		(* allow_cancel_changed)	(HifState	*state,
							 gboolean	 allow_cancel);
	void		(* action_changed)		(HifState	*state,
							 PkStatusEnum	 action,
							 const gchar	*action_hint);
	void		(* package_progress_changed)	(HifState	*state,
							 const gchar	*package_id,
							 PkStatusEnum	 action,
							 guint		 percentage);
};

#define hif_state_done(state, error)			hif_state_done_real(state, error, G_STRLOC)
#define hif_state_finished(state, error)		hif_state_finished_real(state, error, G_STRLOC)
#define hif_state_set_number_steps(state, steps)	hif_state_set_number_steps_real(state, steps, G_STRLOC)
#define hif_state_set_steps(state, error, value, args...)	hif_state_set_steps_real(state, error, G_STRLOC, value, ## args)

typedef gboolean (*HifStateErrorHandlerCb)		(const GError		*error,
							 gpointer		 user_data);
typedef gboolean (*HifStateLockHandlerCb)		(HifState		*state,
							 HifLock		*lock,
							 HifLockType		 lock_type,
							 GError			**error,
							 gpointer		 user_data);

GType		 hif_state_get_type			(void);
HifState	*hif_state_new				(void);
HifState	*hif_state_get_child			(HifState		*state);

/* percentage changed */
void		 hif_state_set_report_progress		(HifState		*state,
							 gboolean		 report_progress);
gboolean	 hif_state_set_number_steps_real	(HifState		*state,
							 guint			 steps,
							 const gchar		*strloc);
gboolean	 hif_state_set_steps_real		(HifState		*state,
							 GError			**error,
							 const gchar		*strloc,
							 gint			 value, ...);
gboolean	 hif_state_set_percentage		(HifState		*state,
							 guint			 percentage);
void		 hif_state_set_package_progress		(HifState		*state,
							 const gchar		*package_id,
							 PkStatusEnum		 action,
							 guint			 percentage);
guint		 hif_state_get_percentage		(HifState		*state);
gboolean	 hif_state_action_start			(HifState		*state,
							 PkStatusEnum		 action,
							 const gchar		*action_hint);
gboolean	 hif_state_action_stop			(HifState		*state);
PkStatusEnum	 hif_state_get_action			(HifState		*state);
const gchar	*hif_state_get_action_hint		(HifState		*state);
gboolean	 hif_state_check			(HifState		*state,
							 GError			 **error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 hif_state_done_real			(HifState		*state,
							 GError			 **error,
							 const gchar		*strloc)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 hif_state_finished_real		(HifState		*state,
							 GError			 **error,
							 const gchar		*strloc)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 hif_state_reset			(HifState		*state);
void		 hif_state_set_enable_profile		(HifState		*state,
							 gboolean		 enable_profile);

/* cancellation */
GCancellable	*hif_state_get_cancellable		(HifState		*state);
void		 hif_state_set_cancellable		(HifState		*state,
							 GCancellable		*cancellable);
gboolean	 hif_state_get_allow_cancel		(HifState		*state);
void		 hif_state_set_allow_cancel		(HifState		*state,
							 gboolean		 allow_cancel);
guint64		 hif_state_get_speed			(HifState		*state);
void		 hif_state_set_speed			(HifState		*state,
							 guint64		 speed);

/* lock handling */
void		 hif_state_set_lock_handler		(HifState		*state,
							 HifStateLockHandlerCb	 lock_handler_cb,
							 gpointer		 user_data);
gboolean	 hif_state_take_lock			(HifState		*state,
							 HifLockType		 lock_type,
							 HifLockMode		 lock_mode,
							 GError			**error);

G_END_DECLS

#endif /* __HIF_STATE_H */

