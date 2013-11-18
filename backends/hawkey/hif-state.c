/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2013 Richard Hughes <richard@hughsie.com>
 *
 * Most of this code was taken from Zif, libzif/zif-state.c
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib-unix.h>
#include <signal.h>
#include <rpm/rpmsq.h>

#include "hif-utils.h"
#include "hif-state.h"

#define HIF_STATE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), HIF_TYPE_STATE, HifStatePrivate))

struct _HifStatePrivate
{
	gboolean		 allow_cancel;
	gboolean		 allow_cancel_changed_state;
	gboolean		 allow_cancel_child;
	gboolean		 enable_profile;
	gboolean		 report_progress;
	GCancellable		*cancellable;
	gchar			*action_hint;
	gchar			*id;
	gdouble			 global_share;
	gdouble			*step_profile;
	gpointer		 error_handler_user_data;
	gpointer		 lock_handler_user_data;
	GTimer			*timer;
	guint64			 speed;
	guint64			*speed_data;
	guint			 current;
	guint			 last_percentage;
	guint			*step_data;
	guint			 steps;
	gulong			 action_child_id;
	gulong			 package_progress_child_id;
	gulong			 notify_speed_child_id;
	gulong			 allow_cancel_child_id;
	gulong			 percentage_child_id;
	gulong			 subpercentage_child_id;
	PkStatusEnum		 action;
	PkStatusEnum		 last_action;
	PkStatusEnum		 child_action;
	HifState		*child;
	HifStateErrorHandlerCb	 error_handler_cb;
	HifStateLockHandlerCb	 lock_handler_cb;
	HifState		*parent;
	GPtrArray		*lock_ids;
	HifLock			*lock;
};

enum {
	SIGNAL_PERCENTAGE_CHANGED,
	SIGNAL_SUBPERCENTAGE_CHANGED,
	SIGNAL_ALLOW_CANCEL_CHANGED,
	SIGNAL_ACTION_CHANGED,
	SIGNAL_PACKAGE_PROGRESS_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_SPEED,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (HifState, hif_state, G_TYPE_OBJECT)

#define HIF_STATE_SPEED_SMOOTHING_ITEMS		5

/**
 * hif_state_set_report_progress:
 **/
void
hif_state_set_report_progress (HifState *state, gboolean report_progress)
{
	g_return_if_fail (HIF_IS_STATE (state));
	state->priv->report_progress = report_progress;
}

/**
 * hif_state_set_enable_profile:
 **/
void
hif_state_set_enable_profile (HifState *state, gboolean enable_profile)
{
	g_return_if_fail (HIF_IS_STATE (state));
	state->priv->enable_profile = enable_profile;
}

/**
 * hif_state_set_lock_handler:
 **/
void
hif_state_set_lock_handler (HifState *state,
			    HifStateLockHandlerCb lock_handler_cb,
			    gpointer user_data)
{
	state->priv->lock_handler_cb = lock_handler_cb;
	state->priv->lock_handler_user_data = user_data;

	/* if there is an existing child, set the handler on this too */
	if (state->priv->child != NULL) {
		hif_state_set_lock_handler (state->priv->child,
					    lock_handler_cb,
					    user_data);
	}
}

/**
 * hif_state_take_lock:
 **/
gboolean
hif_state_take_lock (HifState *state,
		     HifLockType lock_type,
		     HifLockMode lock_mode,
		     GError **error)
{
	gboolean ret = TRUE;
	guint lock_id = 0;

	/* no custom handler */
	if (state->priv->lock_handler_cb == NULL) {
		lock_id = hif_lock_take (state->priv->lock,
					 lock_type,
					 lock_mode,
					 error);
		if (lock_id == 0)
			ret = FALSE;
	} else {
		lock_id = G_MAXUINT;
		ret = state->priv->lock_handler_cb (state,
						    state->priv->lock,
						    lock_type,
						    error,
						    state->priv->lock_handler_user_data);
	}
	if (!ret)
		goto out;

	/* add the lock to an array so we can release on completion */
	g_debug ("adding lock %i", lock_id);
	g_ptr_array_add (state->priv->lock_ids,
			 GUINT_TO_POINTER (lock_id));
out:
	return ret;
}

/**
 * hif_state_discrete_to_percent:
 **/
static gfloat
hif_state_discrete_to_percent (guint discrete, guint steps)
{
	/* check we are in range */
	if (discrete > steps)
		return 100;
	if (steps == 0) {
		g_warning ("steps is 0!");
		return 0;
	}
	return ((gfloat) discrete * (100.0f / (gfloat) (steps)));
}

/**
 * hif_state_print_parent_chain:
 **/
static void
hif_state_print_parent_chain (HifState *state, guint level)
{
	if (state->priv->parent != NULL)
		hif_state_print_parent_chain (state->priv->parent, level + 1);
	g_print ("%i) %s (%i/%i)\n",
		 level, state->priv->id, state->priv->current, state->priv->steps);
}

/**
 * hif_state_get_cancellable:
 **/
GCancellable *
hif_state_get_cancellable (HifState *state)
{
	g_return_val_if_fail (HIF_IS_STATE (state), NULL);
	return state->priv->cancellable;
}

/**
 * hif_state_set_cancellable:
 **/
void
hif_state_set_cancellable (HifState *state, GCancellable *cancellable)
{
	g_return_if_fail (HIF_IS_STATE (state));
	g_return_if_fail (state->priv->cancellable == NULL);
	state->priv->cancellable = g_object_ref (cancellable);
}

/**
 * hif_state_get_allow_cancel:
 **/
gboolean
hif_state_get_allow_cancel (HifState *state)
{
	g_return_val_if_fail (HIF_IS_STATE (state), FALSE);
	return state->priv->allow_cancel && state->priv->allow_cancel_child;
}

/**
 * hif_state_set_allow_cancel:
 **/
void
hif_state_set_allow_cancel (HifState *state, gboolean allow_cancel)
{
	g_return_if_fail (HIF_IS_STATE (state));

	state->priv->allow_cancel_changed_state = TRUE;

	/* quick optimisation that saves lots of signals */
	if (state->priv->allow_cancel == allow_cancel)
		return;
	state->priv->allow_cancel = allow_cancel;

	/* just emit if both this and child is okay */
	g_signal_emit (state, signals [SIGNAL_ALLOW_CANCEL_CHANGED], 0,
		       state->priv->allow_cancel && state->priv->allow_cancel_child);
}

/**
 * hif_state_get_speed:
 **/
guint64
hif_state_get_speed (HifState *state)
{
	g_return_val_if_fail (HIF_IS_STATE (state), 0);
	return state->priv->speed;
}

/**
 * hif_state_set_speed_internal:
 **/
static void
hif_state_set_speed_internal (HifState *state, guint64 speed)
{
	if (state->priv->speed == speed)
		return;
	state->priv->speed = speed;
	g_object_notify (G_OBJECT (state), "speed");
}

/**
 * hif_state_set_speed:
 **/
void
hif_state_set_speed (HifState *state, guint64 speed)
{
	guint i;
	guint64 sum = 0;
	guint sum_cnt = 0;
	g_return_if_fail (HIF_IS_STATE (state));

	/* move the data down one entry */
	for (i=HIF_STATE_SPEED_SMOOTHING_ITEMS-1; i > 0; i--)
		state->priv->speed_data[i] = state->priv->speed_data[i-1];
	state->priv->speed_data[0] = speed;

	/* get the average */
	for (i = 0; i < HIF_STATE_SPEED_SMOOTHING_ITEMS; i++) {
		if (state->priv->speed_data[i] > 0) {
			sum += state->priv->speed_data[i];
			sum_cnt++;
		}
	}
	if (sum_cnt > 0)
		sum /= sum_cnt;

	hif_state_set_speed_internal (state, sum);
}

/**
 * hif_state_release_locks:
 **/
static gboolean
hif_state_release_locks (HifState *state)
{
	gboolean ret = TRUE;
	guint i;
	guint lock_id;

	/* release each one */
	for (i = 0; i < state->priv->lock_ids->len; i++) {
		lock_id = GPOINTER_TO_UINT (g_ptr_array_index (state->priv->lock_ids, i));
		g_debug ("releasing lock %i", lock_id);
		ret = hif_lock_release (state->priv->lock,
					lock_id,
					NULL);
		if (!ret)
			goto out;
	}
	g_ptr_array_set_size (state->priv->lock_ids, 0);
out:
	return ret;
}

/**
 * hif_state_set_percentage:
 **/
gboolean
hif_state_set_percentage (HifState *state, guint percentage)
{
	gboolean ret = FALSE;

	/* do we care */
	if (!state->priv->report_progress) {
		ret = TRUE;
		goto out;
	}

	/* is it the same */
	if (percentage == state->priv->last_percentage)
		goto out;

	/* is it invalid */
	if (percentage > 100) {
		hif_state_print_parent_chain (state, 0);
		g_warning ("percentage %i%% is invalid on %p!",
			   percentage, state);
		goto out;
	}

	/* is it less */
	if (percentage < state->priv->last_percentage) {
		if (state->priv->enable_profile) {
			hif_state_print_parent_chain (state, 0);
			g_warning ("percentage should not go down from %i to %i on %p!",
				   state->priv->last_percentage, percentage, state);
		}
		goto out;
	}

	/* we're done, so we're not preventing cancellation anymore */
	if (percentage == 100 && !state->priv->allow_cancel) {
		g_debug ("done, so allow cancel 1 for %p", state);
		hif_state_set_allow_cancel (state, TRUE);
	}

	/* automatically cancel any action */
	if (percentage == 100 && state->priv->action != PK_STATUS_ENUM_UNKNOWN) {
		g_debug ("done, so cancelling action %s",
			 pk_status_enum_to_string (state->priv->action));
		hif_state_action_stop (state);
	}

	/* speed no longer valid */
	if (percentage == 100)
		hif_state_set_speed_internal (state, 0);

	/* release locks? */
	if (percentage == 100) {
		ret = hif_state_release_locks (state);
		if (!ret)
			goto out;
	}

	/* save */
	state->priv->last_percentage = percentage;

	/* are we so low we don't care */
	if (state->priv->global_share < 0.001)
		goto out;

	/* emit */
	g_signal_emit (state, signals [SIGNAL_PERCENTAGE_CHANGED], 0, percentage);

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * hif_state_get_percentage:
 **/
guint
hif_state_get_percentage (HifState *state)
{
	return state->priv->last_percentage;
}

/**
 * hif_state_set_subpercentage:
 **/
static gboolean
hif_state_set_subpercentage (HifState *state, guint percentage)
{
	/* are we so low we don't care */
	if (state->priv->global_share < 0.01)
		goto out;

	/* just emit */
	g_signal_emit (state, signals [SIGNAL_SUBPERCENTAGE_CHANGED], 0, percentage);
out:
	return TRUE;
}

/**
 * hif_state_action_start:
 **/
gboolean
hif_state_action_start (HifState *state, PkStatusEnum action, const gchar *action_hint)
{
	g_return_val_if_fail (HIF_IS_STATE (state), FALSE);

	/* ignore this */
	if (action == PK_STATUS_ENUM_UNKNOWN) {
		g_warning ("cannot set action PK_STATUS_ENUM_UNKNOWN");
		return FALSE;
	}

	/* is different? */
	if (state->priv->action == action &&
	    g_strcmp0 (action_hint, state->priv->action_hint) == 0) {
		g_debug ("same action as before, ignoring");
		return FALSE;
	}

	/* remember for stop */
	state->priv->last_action = state->priv->action;

	/* save hint */
	g_free (state->priv->action_hint);
	state->priv->action_hint = g_strdup (action_hint);

	/* save */
	state->priv->action = action;

	/* just emit */
	g_signal_emit (state, signals [SIGNAL_ACTION_CHANGED], 0, action, action_hint);
	return TRUE;
}

/**
 * hif_state_set_package_progress:
 **/
void
hif_state_set_package_progress (HifState *state,
				const gchar *package_id,
				PkStatusEnum action,
				guint percentage)
{
	g_return_if_fail (HIF_IS_STATE (state));
	g_return_if_fail (package_id != NULL);
	g_return_if_fail (action != PK_STATUS_ENUM_UNKNOWN);
	g_return_if_fail (percentage <= 100);

	/* just emit */
	g_signal_emit (state, signals [SIGNAL_PACKAGE_PROGRESS_CHANGED], 0,
		       package_id, action, percentage);
}

/**
 * hif_state_action_stop:
 **/
gboolean
hif_state_action_stop (HifState *state)
{
	g_return_val_if_fail (HIF_IS_STATE (state), FALSE);

	/* nothing ever set */
	if (state->priv->action == PK_STATUS_ENUM_UNKNOWN) {
		g_debug ("cannot unset action PK_STATUS_ENUM_UNKNOWN");
		return FALSE;
	}

	/* pop and reset */
	state->priv->action = state->priv->last_action;
	state->priv->last_action = PK_STATUS_ENUM_UNKNOWN;
	if (state->priv->action_hint != NULL) {
		g_free (state->priv->action_hint);
		state->priv->action_hint = NULL;
	}

	/* just emit */
	g_signal_emit (state, signals [SIGNAL_ACTION_CHANGED], 0, state->priv->action, NULL);
	return TRUE;
}

/**
 * hif_state_get_action_hint:
 **/
const gchar *
hif_state_get_action_hint (HifState *state)
{
	g_return_val_if_fail (HIF_IS_STATE (state), NULL);
	return state->priv->action_hint;
}

/**
 * hif_state_get_action:
 **/
PkStatusEnum
hif_state_get_action (HifState *state)
{
	g_return_val_if_fail (HIF_IS_STATE (state), PK_STATUS_ENUM_UNKNOWN);
	return state->priv->action;
}

/**
 * hif_state_child_percentage_changed_cb:
 **/
static void
hif_state_child_percentage_changed_cb (HifState *child, guint percentage, HifState *state)
{
	gfloat offset;
	gfloat range;
	gfloat extra;
	guint parent_percentage;

	/* propagate up the stack if HifState has only one step */
	if (state->priv->steps == 1) {
		hif_state_set_percentage (state, percentage);
		return;
	}

	/* did we call done on a state that did not have a size set? */
	if (state->priv->steps == 0)
		return;

	/* always provide two levels of signals */
	hif_state_set_subpercentage (state, percentage);

	/* already at >= 100% */
	if (state->priv->current >= state->priv->steps) {
		g_warning ("already at %i/%i steps on %p", state->priv->current, state->priv->steps, state);
		return;
	}

	/* we have to deal with non-linear steps */
	if (state->priv->step_data != NULL) {
		/* we don't store zero */
		if (state->priv->current == 0) {
			parent_percentage = percentage * state->priv->step_data[state->priv->current] / 100;
		} else {
			/* bilinearly interpolate for speed */
			parent_percentage = (((100 - percentage) * state->priv->step_data[state->priv->current-1]) +
					     (percentage * state->priv->step_data[state->priv->current])) / 100;
		}
		goto out;
	}

	/* get the offset */
	offset = hif_state_discrete_to_percent (state->priv->current, state->priv->steps);

	/* get the range between the parent step and the next parent step */
	range = hif_state_discrete_to_percent (state->priv->current+1, state->priv->steps) - offset;
	if (range < 0.01) {
		g_warning ("range=%f (from %i to %i), should be impossible", range, state->priv->current+1, state->priv->steps);
		return;
	}

	/* restore the pre-child action */
	if (percentage == 100) {
		state->priv->last_action = state->priv->child_action;
		g_debug ("restoring last action %s",
			 pk_status_enum_to_string (state->priv->child_action));
	}

	/* get the extra contributed by the child */
	extra = ((gfloat) percentage / 100.0f) * range;

	/* emit from the parent */
	parent_percentage = (guint) (offset + extra);
out:
	hif_state_set_percentage (state, parent_percentage);
}

/**
 * hif_state_child_subpercentage_changed_cb:
 **/
static void
hif_state_child_subpercentage_changed_cb (HifState *child, guint percentage, HifState *state)
{
	/* discard this, unless the HifState has only one step */
	if (state->priv->steps != 1)
		return;

	/* propagate up the stack as if the parent didn't exist */
	hif_state_set_subpercentage (state, percentage);
}

/**
 * hif_state_child_allow_cancel_changed_cb:
 **/
static void
hif_state_child_allow_cancel_changed_cb (HifState *child, gboolean allow_cancel, HifState *state)
{
	/* save */
	state->priv->allow_cancel_child = allow_cancel;

	/* just emit if both this and child is okay */
	g_signal_emit (state, signals [SIGNAL_ALLOW_CANCEL_CHANGED], 0,
		       state->priv->allow_cancel && state->priv->allow_cancel_child);
}

/**
 * hif_state_child_action_changed_cb:
 **/
static void
hif_state_child_action_changed_cb (HifState *child, PkStatusEnum action, const gchar *action_hint, HifState *state)
{
	/* save */
	state->priv->action = action;

	/* just emit */
	g_signal_emit (state, signals [SIGNAL_ACTION_CHANGED], 0, action, action_hint);
}

/**
 * hif_state_child_package_progress_changed_cb:
 **/
static void
hif_state_child_package_progress_changed_cb (HifState *child,
					     const gchar *package_id,
					     PkStatusEnum action,
					     guint progress,
					     HifState *state)
{
	/* just emit */
	g_signal_emit (state, signals [SIGNAL_PACKAGE_PROGRESS_CHANGED], 0,
		       package_id, action, progress);
}

/**
 * hif_state_reset:
 **/
gboolean
hif_state_reset (HifState *state)
{
	gboolean ret = TRUE;

	g_return_val_if_fail (HIF_IS_STATE (state), FALSE);

	/* do we care */
	if (!state->priv->report_progress) {
		ret = TRUE;
		goto out;
	}

	/* reset values */
	state->priv->steps = 0;
	state->priv->current = 0;
	state->priv->last_percentage = 0;

	/* only use the timer if profiling; it's expensive */
	if (state->priv->enable_profile)
		g_timer_start (state->priv->timer);

	/* disconnect client */
	if (state->priv->percentage_child_id != 0) {
		g_signal_handler_disconnect (state->priv->child,
					     state->priv->percentage_child_id);
		state->priv->percentage_child_id = 0;
	}
	if (state->priv->subpercentage_child_id != 0) {
		g_signal_handler_disconnect (state->priv->child,
					     state->priv->subpercentage_child_id);
		state->priv->subpercentage_child_id = 0;
	}
	if (state->priv->allow_cancel_child_id != 0) {
		g_signal_handler_disconnect (state->priv->child,
					     state->priv->allow_cancel_child_id);
		state->priv->allow_cancel_child_id = 0;
	}
	if (state->priv->action_child_id != 0) {
		g_signal_handler_disconnect (state->priv->child,
					     state->priv->action_child_id);
		state->priv->action_child_id = 0;
	}
	if (state->priv->package_progress_child_id != 0) {
		g_signal_handler_disconnect (state->priv->child,
					     state->priv->package_progress_child_id);
		state->priv->package_progress_child_id = 0;
	}
	if (state->priv->notify_speed_child_id != 0) {
		g_signal_handler_disconnect (state->priv->child,
					     state->priv->notify_speed_child_id);
		state->priv->notify_speed_child_id = 0;
	}

	/* unref child */
	if (state->priv->child != NULL) {
		g_object_unref (state->priv->child);
		state->priv->child = NULL;
	}

	/* no more locks */
	hif_state_release_locks (state);

	/* no more step data */
	g_free (state->priv->step_data);
	g_free (state->priv->step_profile);
	state->priv->step_data = NULL;
	state->priv->step_profile = NULL;
out:
	return ret;
}

/**
 * hif_state_set_global_share:
 **/
static void
hif_state_set_global_share (HifState *state, gdouble global_share)
{
	state->priv->global_share = global_share;
}

/**
 * hif_state_child_notify_speed_cb:
 **/
static void
hif_state_child_notify_speed_cb (HifState *child,
				 GParamSpec *pspec,
				 HifState *state)
{
	hif_state_set_speed_internal (state,
				      hif_state_get_speed (child));
}

/**
 * hif_state_get_child:
 **/
HifState *
hif_state_get_child (HifState *state)
{
	HifState *child = NULL;

	g_return_val_if_fail (HIF_IS_STATE (state), NULL);

	/* do we care */
	if (!state->priv->report_progress) {
		child = state;
		goto out;
	}

	/* already set child */
	if (state->priv->child != NULL) {
		g_signal_handler_disconnect (state->priv->child,
					     state->priv->percentage_child_id);
		g_signal_handler_disconnect (state->priv->child,
					     state->priv->subpercentage_child_id);
		g_signal_handler_disconnect (state->priv->child,
					     state->priv->allow_cancel_child_id);
		g_signal_handler_disconnect (state->priv->child,
					     state->priv->action_child_id);
		g_signal_handler_disconnect (state->priv->child,
					     state->priv->package_progress_child_id);
		g_signal_handler_disconnect (state->priv->child,
					     state->priv->notify_speed_child_id);
		g_object_unref (state->priv->child);
	}

	/* connect up signals */
	child = hif_state_new ();
	child->priv->parent = state; /* do not ref! */
	state->priv->child = child;
	state->priv->percentage_child_id =
		g_signal_connect (child, "percentage-changed",
				  G_CALLBACK (hif_state_child_percentage_changed_cb),
				  state);
	state->priv->subpercentage_child_id =
		g_signal_connect (child, "subpercentage-changed",
				  G_CALLBACK (hif_state_child_subpercentage_changed_cb),
				  state);
	state->priv->allow_cancel_child_id =
		g_signal_connect (child, "allow-cancel-changed",
				  G_CALLBACK (hif_state_child_allow_cancel_changed_cb),
				  state);
	state->priv->action_child_id =
		g_signal_connect (child, "action-changed",
				  G_CALLBACK (hif_state_child_action_changed_cb),
				  state);
	state->priv->package_progress_child_id =
		g_signal_connect (child, "package-progress-changed",
				  G_CALLBACK (hif_state_child_package_progress_changed_cb),
				  state);
	state->priv->notify_speed_child_id =
		g_signal_connect (child, "notify::speed",
				  G_CALLBACK (hif_state_child_notify_speed_cb),
				  state);

	/* reset child */
	child->priv->current = 0;
	child->priv->last_percentage = 0;

	/* save so we can recover after child has done */
	child->priv->action = state->priv->action;
	state->priv->child_action = state->priv->action;

	/* set the global share on the new child */
	hif_state_set_global_share (child, state->priv->global_share);

	/* set cancellable, creating if required */
	if (state->priv->cancellable == NULL)
		state->priv->cancellable = g_cancellable_new ();
	hif_state_set_cancellable (child, state->priv->cancellable);

	/* set the lock handler if one exists on the child */
	if (state->priv->lock_handler_cb != NULL) {
		hif_state_set_lock_handler (child,
					    state->priv->lock_handler_cb,
					    state->priv->lock_handler_user_data);
	}

	/* set the profile state */
	hif_state_set_enable_profile (child,
				      state->priv->enable_profile);
out:
	return child;
}

/**
 * hif_state_set_number_steps_real:
 **/
gboolean
hif_state_set_number_steps_real (HifState *state, guint steps, const gchar *strloc)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (state != NULL, FALSE);

	/* nothing to do for 0 steps */
	if (steps == 0) {
		ret = TRUE;
		goto out;
	}

	/* do we care */
	if (!state->priv->report_progress) {
		ret = TRUE;
		goto out;
	}

	/* did we call done on a state that did not have a size set? */
	if (state->priv->steps != 0) {
		g_warning ("steps already set to %i, can't set %i! [%s]",
			     state->priv->steps, steps, strloc);
		hif_state_print_parent_chain (state, 0);
		goto out;
	}

	/* set id */
	g_free (state->priv->id);
	state->priv->id = g_strdup_printf ("%s", strloc);

	/* only use the timer if profiling; it's expensive */
	if (state->priv->enable_profile)
		g_timer_start (state->priv->timer);

	/* imply reset */
	hif_state_reset (state);

	/* set steps */
	state->priv->steps = steps;

	/* global share just got smaller */
	state->priv->global_share /= steps;

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * hif_state_set_steps_real:
 **/
gboolean
hif_state_set_steps_real (HifState *state, GError **error, const gchar *strloc, gint value, ...)
{
	va_list args;
	guint i;
	gint value_temp;
	guint total;
	gboolean ret = FALSE;

	g_return_val_if_fail (state != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* do we care */
	if (!state->priv->report_progress) {
		ret = TRUE;
		goto out;
	}

	/* we must set at least one thing */
	total = value;

	/* process the valist */
	va_start (args, value);
	for (i = 0;; i++) {
		value_temp = va_arg (args, gint);
		if (value_temp == -1)
			break;
		total += (guint) value_temp;
	}
	va_end (args);

	/* does not sum to 100% */
	if (total != 100) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "percentage not 100: %i",
			     total);
		goto out;
	}

	/* set step number */
	ret = hif_state_set_number_steps_real (state, i+1, strloc);
	if (!ret) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "failed to set number steps: %i",
			     i+1);
		goto out;
	}

	/* save this data */
	total = value;
	state->priv->step_data = g_new0 (guint, i+2);
	state->priv->step_profile = g_new0 (gdouble, i+2);
	state->priv->step_data[0] = total;
	va_start (args, value);
	for (i = 0;; i++) {
		value_temp = va_arg (args, gint);
		if (value_temp == -1)
			break;

		/* we pre-add the data to make access simpler */
		total += (guint) value_temp;
		state->priv->step_data[i+1] = total;
	}
	va_end (args);

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * hif_state_show_profile:
 **/
static void
hif_state_show_profile (HifState *state)
{
	gdouble division;
	gdouble total_time = 0.0f;
	GString *result;
	guint i;
	guint uncumalitive = 0;

	/* get the total time so we can work out the divisor */
	result = g_string_new ("Raw timing data was { ");
	for (i = 0; i < state->priv->steps; i++) {
		g_string_append_printf (result, "%.3f, ",
					state->priv->step_profile[i]);
	}
	if (state->priv->steps > 0)
		g_string_set_size (result, result->len - 2);
	g_string_append (result, " }\n");

	/* get the total time so we can work out the divisor */
	for (i = 0; i < state->priv->steps; i++)
		total_time += state->priv->step_profile[i];
	division = total_time / 100.0f;

	/* what we set */
	g_string_append (result, "steps were set as [ ");
	for (i = 0; i < state->priv->steps; i++) {
		g_string_append_printf (result, "%i, ",
					state->priv->step_data[i] - uncumalitive);
		uncumalitive = state->priv->step_data[i];
	}

	/* what we _should_ have set */
	g_string_append_printf (result, "-1 ] but should have been: [ ");
	for (i = 0; i < state->priv->steps; i++) {
		g_string_append_printf (result, "%.0f, ",
					state->priv->step_profile[i] / division);
	}
	g_string_append (result, "-1 ]");
	g_printerr ("\n\n%s at %s\n\n", result->str, state->priv->id);
	g_string_free (result, TRUE);
}

/**
 * hif_state_check:
 **/
gboolean
hif_state_check (HifState *state, GError **error)
{
	gboolean ret = TRUE;

	g_return_val_if_fail (state != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* are we cancelled */
	if (g_cancellable_is_cancelled (state->priv->cancellable)) {
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				     "cancelled by user action");
		ret = FALSE;
		goto out;
	}
out:
	return ret;
}

/**
 * hif_state_done_real:
 **/
gboolean
hif_state_done_real (HifState *state, GError **error, const gchar *strloc)
{
	gboolean ret;
	gdouble elapsed;
	gfloat percentage;

	g_return_val_if_fail (state != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check */
	ret = hif_state_check (state, error);
	if (!ret)
		goto out;

	/* do we care */
	if (!state->priv->report_progress)
		goto out;

	/* did we call done on a state that did not have a size set? */
	if (state->priv->steps == 0) {
		g_set_error (error, HIF_ERROR, PK_ERROR_ENUM_INTERNAL_ERROR,
			     "done on a state %p that did not have a size set! [%s]",
			     state, strloc);
		hif_state_print_parent_chain (state, 0);
		ret = FALSE;
		goto out;
	}

	/* check the interval was too big in allow_cancel false mode */
	if (state->priv->enable_profile) {
		elapsed = g_timer_elapsed (state->priv->timer, NULL);
		if (!state->priv->allow_cancel_changed_state && state->priv->current > 0) {
			if (elapsed > 0.1f) {
				g_warning ("%.1fms between hif_state_done() and no hif_state_set_allow_cancel()", elapsed * 1000);
				hif_state_print_parent_chain (state, 0);
			}
		}

		/* save the duration in the array */
		if (state->priv->step_profile != NULL)
			state->priv->step_profile[state->priv->current] = elapsed;
		g_timer_start (state->priv->timer);
	}

	/* is already at 100%? */
	if (state->priv->current >= state->priv->steps) {
		g_set_error (error, HIF_ERROR, PK_ERROR_ENUM_INTERNAL_ERROR,
			     "already at 100%% state [%s]", strloc);
		hif_state_print_parent_chain (state, 0);
		ret = FALSE;
		goto out;
	}

	/* is child not at 100%? */
	if (state->priv->child != NULL) {
		HifStatePrivate *child_priv = state->priv->child->priv;
		if (child_priv->current != child_priv->steps) {
			g_print ("child is at %i/%i steps and parent done [%s]\n",
				 child_priv->current, child_priv->steps, strloc);
			hif_state_print_parent_chain (state->priv->child, 0);
			ret = TRUE;
			/* do not abort, as we want to clean this up */
		}
	}

	/* we just checked for cancel, so it's not true to say we're blocking */
	hif_state_set_allow_cancel (state, TRUE);

	/* another */
	state->priv->current++;

	/* find new percentage */
	if (state->priv->step_data == NULL) {
		percentage = hif_state_discrete_to_percent (state->priv->current,
							    state->priv->steps);
	} else {
		/* this is cumalative, for speedy access */
		percentage = state->priv->step_data[state->priv->current - 1];
	}
	hif_state_set_percentage (state, (guint) percentage);

	/* show any profiling stats */
	if (state->priv->enable_profile &&
	    state->priv->current == state->priv->steps &&
	    state->priv->step_profile != NULL) {
		hif_state_show_profile (state);
	}

	/* reset child if it exists */
	if (state->priv->child != NULL)
		hif_state_reset (state->priv->child);
out:
	return ret;
}

/**
 * hif_state_finished_real:
 **/
gboolean
hif_state_finished_real (HifState *state, GError **error, const gchar *strloc)
{
	gboolean ret;

	g_return_val_if_fail (state != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check */
	ret = hif_state_check (state, error);
	if (!ret)
		goto out;

	/* is already at 100%? */
	if (state->priv->current == state->priv->steps)
		goto out;

	/* all done */
	state->priv->current = state->priv->steps;

	/* set new percentage */
	hif_state_set_percentage (state, 100);
out:
	return ret;
}

/**
 * hif_state_finalize:
 **/
static void
hif_state_finalize (GObject *object)
{
	HifState *state;

	g_return_if_fail (object != NULL);
	g_return_if_fail (HIF_IS_STATE (object));
	state = HIF_STATE (object);

	/* no more locks */
	hif_state_release_locks (state);

	hif_state_reset (state);
	g_free (state->priv->id);
	g_free (state->priv->action_hint);
	g_free (state->priv->step_data);
	g_free (state->priv->step_profile);
	if (state->priv->cancellable != NULL)
		g_object_unref (state->priv->cancellable);
	g_timer_destroy (state->priv->timer);
	g_free (state->priv->speed_data);
	g_ptr_array_unref (state->priv->lock_ids);
	g_object_unref (state->priv->lock);

	G_OBJECT_CLASS (hif_state_parent_class)->finalize (object);
}

/**
 * hif_state_get_property:
 **/
static void
hif_state_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	HifState *state = HIF_STATE (object);
	HifStatePrivate *priv = state->priv;

	switch (prop_id) {
	case PROP_SPEED:
		g_value_set_uint64 (value, priv->speed);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * hif_state_set_property:
 **/
static void
hif_state_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	HifState *state = HIF_STATE (object);
	HifStatePrivate *priv = state->priv;

	switch (prop_id) {
	case PROP_SPEED:
		priv->speed = g_value_get_uint64 (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * hif_state_class_init:
 **/
static void
hif_state_class_init (HifStateClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = hif_state_finalize;
	object_class->get_property = hif_state_get_property;
	object_class->set_property = hif_state_set_property;

	/**
	 * HifState:speed:
	 */
	pspec = g_param_spec_uint64 ("speed", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_SPEED, pspec);

	signals [SIGNAL_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (HifStateClass, percentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	signals [SIGNAL_SUBPERCENTAGE_CHANGED] =
		g_signal_new ("subpercentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (HifStateClass, subpercentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	signals [SIGNAL_ALLOW_CANCEL_CHANGED] =
		g_signal_new ("allow-cancel-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (HifStateClass, allow_cancel_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	signals [SIGNAL_ACTION_CHANGED] =
		g_signal_new ("action-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (HifStateClass, action_changed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

	signals [SIGNAL_PACKAGE_PROGRESS_CHANGED] =
		g_signal_new ("package-progress-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (HifStateClass, package_progress_changed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (HifStatePrivate));
}

/**
 * hif_state_init:
 **/
static void
hif_state_init (HifState *state)
{
	state->priv = HIF_STATE_GET_PRIVATE (state);
	state->priv->allow_cancel = TRUE;
	state->priv->allow_cancel_child = TRUE;
	state->priv->global_share = 1.0f;
	state->priv->action = PK_STATUS_ENUM_UNKNOWN;
	state->priv->last_action = PK_STATUS_ENUM_UNKNOWN;
	state->priv->timer = g_timer_new ();
	state->priv->lock_ids = g_ptr_array_new ();
	state->priv->report_progress = TRUE;
	state->priv->lock = hif_lock_new ();
	state->priv->speed_data = g_new0 (guint64, HIF_STATE_SPEED_SMOOTHING_ITEMS);
}

/**
 * hif_state_new:
 **/
HifState *
hif_state_new (void)
{
	HifState *state;
	state = g_object_new (HIF_TYPE_STATE, NULL);
	return HIF_STATE (state);
}

