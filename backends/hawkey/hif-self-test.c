/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2013 Richard Hughes <richard@hughsie.com>
 *
 * Most of this code was taken from Hif, libzif/zif-self-test.c
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

#include "config.h"

#include <glib-object.h>

#include "hif-state.h"
#include "hif-utils.h"

static guint _hif_lock_state_changed = 0;

static void
hif_lock_state_changed_cb (HifLock *lock, guint bitfield, gpointer user_data)
{
	g_debug ("lock state now %i", bitfield);
	_hif_lock_state_changed++;
}

static void
hif_lock_func (void)
{
	HifLock *lock;
	gboolean ret;
	guint lock_id1;
	guint lock_id2;
	GError *error = NULL;

	lock = hif_lock_new ();
	g_assert (lock != NULL);
	g_signal_connect (lock, "state-changed",
			  G_CALLBACK (hif_lock_state_changed_cb), NULL);

	/* nothing yet! */
	g_assert_cmpint (hif_lock_get_state (lock), ==, 0);
	ret = hif_lock_release (lock, 999, &error);
	g_assert_error (error, HIF_ERROR, PK_ERROR_ENUM_INTERNAL_ERROR);
	g_assert (!ret);
	g_clear_error (&error);

	/* take one */
	lock_id1 = hif_lock_take (lock,
				  HIF_LOCK_TYPE_RPMDB,
				  HIF_LOCK_MODE_PROCESS,
				  &error);
	g_assert_no_error (error);
	g_assert (lock_id1 != 0);
	g_assert_cmpint (hif_lock_get_state (lock), ==, 1 << HIF_LOCK_TYPE_RPMDB);
	g_assert_cmpint (_hif_lock_state_changed, ==, 1);

	/* take a different one */
	lock_id2 = hif_lock_take (lock,
				  HIF_LOCK_TYPE_REPO,
				  HIF_LOCK_MODE_PROCESS,
				  &error);
	g_assert_no_error (error);
	g_assert (lock_id2 != 0);
	g_assert (lock_id2 != lock_id1);
	g_assert_cmpint (hif_lock_get_state (lock), ==, 1 << HIF_LOCK_TYPE_RPMDB | 1 << HIF_LOCK_TYPE_REPO);
	g_assert_cmpint (_hif_lock_state_changed, ==, 2);

	/* take two */
	lock_id1 = hif_lock_take (lock,
				  HIF_LOCK_TYPE_RPMDB,
				  HIF_LOCK_MODE_PROCESS,
				  &error);
	g_assert_no_error (error);
	g_assert (lock_id1 != 0);
	g_assert_cmpint (hif_lock_get_state (lock), ==, 1 << HIF_LOCK_TYPE_RPMDB | 1 << HIF_LOCK_TYPE_REPO);

	/* release one */
	ret = hif_lock_release (lock, lock_id1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* release different one */
	ret = hif_lock_release (lock, lock_id2, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* release two */
	ret = hif_lock_release (lock, lock_id1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* no more! */
	ret = hif_lock_release (lock, lock_id1, &error);
	g_assert_error (error, HIF_ERROR, PK_ERROR_ENUM_INTERNAL_ERROR);
	g_assert (!ret);
	g_clear_error (&error);
	g_assert_cmpint (hif_lock_get_state (lock), ==, 0);
	g_assert_cmpint (_hif_lock_state_changed, ==, 6);

	g_object_unref (lock);
}

static gpointer
hif_self_test_lock_thread_one (gpointer data)
{
	GError *error = NULL;
	guint lock_id;
	HifLock *lock = HIF_LOCK (data);

	g_usleep (G_USEC_PER_SEC / 100);
	lock_id = hif_lock_take (lock,
				 HIF_LOCK_TYPE_REPO,
				 HIF_LOCK_MODE_PROCESS,
				 &error);
	g_assert_error (error, HIF_ERROR, PK_ERROR_ENUM_CANNOT_GET_LOCK);
	g_assert_cmpint (lock_id, ==, 0);
	g_error_free (error);
	return NULL;
}

static void
hif_lock_threads_func (void)
{
	gboolean ret;
	GError *error = NULL;
	GThread *one;
	guint lock_id;
	HifLock *lock;

	/* take in master thread */
	lock = hif_lock_new ();
	lock_id = hif_lock_take (lock,
				 HIF_LOCK_TYPE_REPO,
				 HIF_LOCK_MODE_PROCESS,
				 &error);
	g_assert_no_error (error);
	g_assert_cmpint (lock_id, >, 0);

	/* attempt to take in slave thread (should fail) */
	one = g_thread_new ("hif-lock-one",
			    hif_self_test_lock_thread_one,
			    lock);

	/* block, waiting for thread */
	g_usleep (G_USEC_PER_SEC);

	/* release lock */
	ret = hif_lock_release (lock, lock_id, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_thread_unref (one);
	g_object_unref (lock);
}

static guint _allow_cancel_updates = 0;
static guint _action_updates = 0;
static guint _package_progress_updates = 0;
static guint _last_percent = 0;
static guint _updates = 0;

static void
hif_state_test_percentage_changed_cb (HifState *state, guint value, gpointer data)
{
	_last_percent = value;
	_updates++;
}

static void
hif_state_test_allow_cancel_changed_cb (HifState *state, gboolean allow_cancel, gpointer data)
{
	_allow_cancel_updates++;
}

static void
hif_state_test_action_changed_cb (HifState *state, PkStatusEnum action, gpointer data)
{
	_action_updates++;
}

static void
hif_state_test_package_progress_changed_cb (HifState *state,
					    const gchar *package_id,
					    PkStatusEnum action,
					    guint percentage,
					    gpointer data)
{
	g_assert (data == NULL);
	g_debug ("%s now %s at %u",
		 package_id,
		 pk_status_enum_to_string (action),
		 percentage);
	_package_progress_updates++;
}

static void
hif_state_func (void)
{
	gboolean ret;
	guint i;
	HifState *state;

	for (i = 0; i < PK_STATUS_ENUM_LAST ; i++)
		g_assert (pk_status_enum_to_string (i) != NULL);

	_updates = 0;

	state = hif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	g_assert (state != NULL);
	g_signal_connect (state, "percentage-changed", G_CALLBACK (hif_state_test_percentage_changed_cb), NULL);
	g_signal_connect (state, "allow-cancel-changed", G_CALLBACK (hif_state_test_allow_cancel_changed_cb), NULL);
	g_signal_connect (state, "action-changed", G_CALLBACK (hif_state_test_action_changed_cb), NULL);
	g_signal_connect (state, "package-progress-changed", G_CALLBACK (hif_state_test_package_progress_changed_cb), NULL);

	g_assert (hif_state_get_allow_cancel (state));
	g_assert_cmpint (hif_state_get_action (state), ==, PK_STATUS_ENUM_UNKNOWN);

	hif_state_set_allow_cancel (state, TRUE);
	g_assert (hif_state_get_allow_cancel (state));

	hif_state_set_allow_cancel (state, FALSE);
	g_assert (!hif_state_get_allow_cancel (state));
	g_assert_cmpint (_allow_cancel_updates, ==, 1);

	/* stop never started */
	g_assert (!hif_state_action_stop (state));

	/* repeated */
	g_assert (hif_state_action_start (state, PK_STATUS_ENUM_DOWNLOAD, NULL));
	g_assert (!hif_state_action_start (state, PK_STATUS_ENUM_DOWNLOAD, NULL));
	g_assert_cmpint (hif_state_get_action (state), ==, PK_STATUS_ENUM_DOWNLOAD);
	g_assert (hif_state_action_stop (state));
	g_assert_cmpint (hif_state_get_action (state), ==, PK_STATUS_ENUM_UNKNOWN);
	g_assert_cmpint (_action_updates, ==, 2);

	ret = hif_state_set_number_steps (state, 5);
	g_assert (ret);

	ret = hif_state_done (state, NULL);
	g_assert (ret);

	g_assert_cmpint (_updates, ==, 1);

	g_assert_cmpint (_last_percent, ==, 20);

	ret = hif_state_done (state, NULL);
	ret = hif_state_done (state, NULL);
	ret = hif_state_done (state, NULL);
	hif_state_set_package_progress (state,
					"hal;0.0.1;i386;fedora",
					PK_STATUS_ENUM_DOWNLOAD,
					50);
	g_assert (hif_state_done (state, NULL));

	g_assert (!hif_state_done (state, NULL));
	g_assert_cmpint (_updates, ==, 5);
	g_assert_cmpint (_package_progress_updates, ==, 1);
	g_assert_cmpint (_last_percent, ==, 100);

	/* ensure allow cancel as we're done */
	g_assert (hif_state_get_allow_cancel (state));

	g_object_unref (state);
	g_assert (state == NULL);
}

static void
hif_state_child_func (void)
{
	gboolean ret;
	HifState *state;
	HifState *child;

	/* reset */
	_updates = 0;
	_allow_cancel_updates = 0;
	_action_updates = 0;
	_package_progress_updates = 0;
	state = hif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	hif_state_set_allow_cancel (state, TRUE);
	hif_state_set_number_steps (state, 2);
	g_signal_connect (state, "percentage-changed", G_CALLBACK (hif_state_test_percentage_changed_cb), NULL);
	g_signal_connect (state, "allow-cancel-changed", G_CALLBACK (hif_state_test_allow_cancel_changed_cb), NULL);
	g_signal_connect (state, "action-changed", G_CALLBACK (hif_state_test_action_changed_cb), NULL);
	g_signal_connect (state, "package-progress-changed", G_CALLBACK (hif_state_test_package_progress_changed_cb), NULL);

	// state: |-----------------------|-----------------------|
	// step1: |-----------------------|
	// child:                         |-------------|---------|

	/* PARENT UPDATE */
	g_debug ("parent update #1");
	ret = hif_state_done (state, NULL);

	g_assert ((_updates == 1));
	g_assert ((_last_percent == 50));

	/* set parent state */
	g_debug ("setting: depsolving-conflicts");
	hif_state_action_start (state,
				PK_STATUS_ENUM_DEP_RESOLVE,
				"hal;0.1.0-1;i386;fedora");

	/* now test with a child */
	child = hif_state_get_child (state);
	hif_state_set_number_steps (child, 2);

	/* check child inherits parents action */
	g_assert_cmpint (hif_state_get_action (child), ==,
			 PK_STATUS_ENUM_DEP_RESOLVE);

	/* set child non-cancellable */
	hif_state_set_allow_cancel (child, FALSE);

	/* ensure both are disallow-cancel */
	g_assert (!hif_state_get_allow_cancel (child));
	g_assert (!hif_state_get_allow_cancel (state));

	/* CHILD UPDATE */
	g_debug ("setting: loading-rpmdb");
	g_assert (hif_state_action_start (child, PK_STATUS_ENUM_LOADING_CACHE, NULL));
	g_assert_cmpint (hif_state_get_action (child), ==,
			 PK_STATUS_ENUM_LOADING_CACHE);

	g_debug ("child update #1");
	ret = hif_state_done (child, NULL);
	hif_state_set_package_progress (child,
					"hal;0.0.1;i386;fedora",
					PK_STATUS_ENUM_DOWNLOAD,
					50);

	g_assert_cmpint (_updates, ==, 2);
	g_assert_cmpint (_last_percent, ==, 75);
	g_assert_cmpint (_package_progress_updates, ==, 1);

	/* child action */
	g_debug ("setting: downloading");
	g_assert (hif_state_action_start (child,
					  PK_STATUS_ENUM_DOWNLOAD,
					  NULL));
	g_assert_cmpint (hif_state_get_action (child), ==,
			 PK_STATUS_ENUM_DOWNLOAD);

	/* CHILD UPDATE */
	g_debug ("child update #2");
	ret = hif_state_done (child, NULL);

	g_assert_cmpint (hif_state_get_action (state), ==,
			 PK_STATUS_ENUM_DEP_RESOLVE);
	g_assert (hif_state_action_stop (state));
	g_assert (!hif_state_action_stop (state));
	g_assert_cmpint (hif_state_get_action (state), ==,
			 PK_STATUS_ENUM_UNKNOWN);
	g_assert_cmpint (_action_updates, ==, 6);

	g_assert_cmpint (_updates, ==, 3);
	g_assert_cmpint (_last_percent, ==, 100);

	/* ensure the child finishing cleared the allow cancel on the parent */
	ret = hif_state_get_allow_cancel (state);
	g_assert (ret);

	/* PARENT UPDATE */
	g_debug ("parent update #2");
	ret = hif_state_done (state, NULL);
	g_assert (ret);

	/* ensure we ignored the duplicate */
	g_assert_cmpint (_updates, ==, 3);
	g_assert_cmpint (_last_percent, ==, 100);

	g_object_unref (state);
	g_assert (state == NULL);
}

static void
hif_state_parent_one_step_proxy_func (void)
{
	HifState *state;
	HifState *child;

	/* reset */
	_updates = 0;
	state = hif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	hif_state_set_number_steps (state, 1);
	g_signal_connect (state, "percentage-changed", G_CALLBACK (hif_state_test_percentage_changed_cb), NULL);
	g_signal_connect (state, "allow-cancel-changed", G_CALLBACK (hif_state_test_allow_cancel_changed_cb), NULL);

	/* now test with a child */
	child = hif_state_get_child (state);
	hif_state_set_number_steps (child, 2);

	/* CHILD SET VALUE */
	hif_state_set_percentage (child, 33);

	/* ensure 1 updates for state with one step and ensure using child value as parent */
	g_assert (_updates == 1);
	g_assert (_last_percent == 33);

	g_object_unref (state);
	g_assert (state == NULL);
}

static void
hif_state_non_equal_steps_func (void)
{
	gboolean ret;
	GError *error = NULL;
	HifState *state;
	HifState *child;
	HifState *child_child;

	/* test non-equal steps */
	state = hif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	hif_state_set_enable_profile (state, TRUE);
	ret = hif_state_set_steps (state,
				   &error,
				   20, /* prepare */
				   60, /* download */
				   10, /* install */
				   -1);
	g_assert_error (error, HIF_ERROR, PK_ERROR_ENUM_INTERNAL_ERROR);
	g_assert (!ret);
	g_clear_error (&error);

	/* okay this time */
	ret = hif_state_set_steps (state, &error, 20, 60, 20, -1);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify nothing */
	g_assert_cmpint (hif_state_get_percentage (state), ==, 0);

	/* child step should increment according to the custom steps */
	child = hif_state_get_child (state);
	hif_state_set_number_steps (child, 2);

	/* start child */
	g_usleep (9 * 10 * 1000);
	ret = hif_state_done (child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify 10% */
	g_assert_cmpint (hif_state_get_percentage (state), ==, 10);

	/* finish child */
	g_usleep (9 * 10 * 1000);
	ret = hif_state_done (child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	ret = hif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify 20% */
	g_assert_cmpint (hif_state_get_percentage (state), ==, 20);

	/* child step should increment according to the custom steps */
	child = hif_state_get_child (state);
	ret = hif_state_set_steps (child,
				   &error,
				   25,
				   75,
				   -1);

	/* start child */
	g_usleep (25 * 10 * 1000);
	ret = hif_state_done (child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify bilinear interpolation is working */
	g_assert_cmpint (hif_state_get_percentage (state), ==, 35);

	/*
	 * 0        20                             80         100
	 * |---------||----------------------------||---------|
	 *            |       35                   |
	 *            |-------||-------------------| (25%)
	 *                     |              75.5 |
	 *                     |---------------||--| (90%)
	 */
	child_child = hif_state_get_child (child);
	ret = hif_state_set_steps (child_child,
				   &error,
				   90,
				   10,
				   -1);
	g_assert_no_error (error);
	g_assert (ret);

	ret = hif_state_done (child_child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify bilinear interpolation (twice) is working for subpercentage */
	g_assert_cmpint (hif_state_get_percentage (state), ==, 75);

	ret = hif_state_done (child_child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* finish child */
	g_usleep (25 * 10 * 1000);
	ret = hif_state_done (child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	ret = hif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify 80% */
	g_assert_cmpint (hif_state_get_percentage (state), ==, 80);

	g_usleep (19 * 10 * 1000);

	ret = hif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify 100% */
	g_assert_cmpint (hif_state_get_percentage (state), ==, 100);

	g_object_unref (state);
	g_assert (state == NULL);
}

static void
hif_state_no_progress_func (void)
{
	gboolean ret;
	GError *error = NULL;
	HifState *state;
	HifState *child;

	/* test a state where we don't care about progress */
	state = hif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	hif_state_set_report_progress (state, FALSE);

	hif_state_set_number_steps (state, 3);
	g_assert_cmpint (hif_state_get_percentage (state), ==, 0);

	ret = hif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (hif_state_get_percentage (state), ==, 0);

	ret = hif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	child = hif_state_get_child (state);
	g_assert (child != NULL);
	hif_state_set_number_steps (child, 2);
	ret = hif_state_done (child, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = hif_state_done (child, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (hif_state_get_percentage (state), ==, 0);

	g_object_unref (state);
	g_assert (state == NULL);
}

static void
hif_state_finish_func (void)
{
	gboolean ret;
	GError *error = NULL;
	HifState *state;
	HifState *child;

	/* check straight finish */
	state = hif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	hif_state_set_number_steps (state, 3);

	child = hif_state_get_child (state);
	hif_state_set_number_steps (child, 3);
	ret = hif_state_finished (child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* parent step done after child finish */
	ret = hif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_object_unref (state);
	g_assert (state == NULL);
}

static void
hif_state_speed_func (void)
{
	HifState *state;

	/* speed averaging test */
	state = hif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	g_assert_cmpint (hif_state_get_speed (state), ==, 0);
	hif_state_set_speed (state, 100);
	g_assert_cmpint (hif_state_get_speed (state), ==, 100);
	hif_state_set_speed (state, 200);
	g_assert_cmpint (hif_state_get_speed (state), ==, 150);
	hif_state_set_speed (state, 300);
	g_assert_cmpint (hif_state_get_speed (state), ==, 200);
	hif_state_set_speed (state, 400);
	g_assert_cmpint (hif_state_get_speed (state), ==, 250);
	hif_state_set_speed (state, 500);
	g_assert_cmpint (hif_state_get_speed (state), ==, 300);
	hif_state_set_speed (state, 600);
	g_assert_cmpint (hif_state_get_speed (state), ==, 400);
	g_object_unref (state);
	g_assert (state == NULL);
}

static void
hif_state_finished_func (void)
{
	HifState *state_local;
	HifState *state;
	gboolean ret;
	GError *error = NULL;
	guint i;

	state = hif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	ret = hif_state_set_steps (state,
				   &error,
				   90,
				   10,
				   -1);
	g_assert_no_error (error);
	g_assert (ret);

	hif_state_set_allow_cancel (state, FALSE);
	hif_state_action_start (state,
				PK_STATUS_ENUM_LOADING_CACHE, "/");

	state_local = hif_state_get_child (state);
	hif_state_set_report_progress (state_local, FALSE);

	for (i = 0; i < 10; i++) {
		/* check cancelled (okay to reuse as we called
		 * hif_state_set_report_progress before)*/
		ret = hif_state_done (state_local, &error);
		g_assert_no_error (error);
		g_assert (ret);
	}

	/* turn checks back on */
	hif_state_set_report_progress (state_local, TRUE);
	ret = hif_state_finished (state_local, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* this section done */
	ret = hif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* this section done */
	ret = hif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_object_unref (state);
	g_assert (state == NULL);
}

static void
hif_state_locking_func (void)
{
	gboolean ret;
	GError *error = NULL;
	HifState *state;

	state = hif_state_new ();

	/* lock once */
	ret = hif_state_take_lock (state,
				   HIF_LOCK_TYPE_RPMDB,
				   HIF_LOCK_MODE_PROCESS,
				   &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* succeeded, even again */
	ret = hif_state_take_lock (state,
				   HIF_LOCK_TYPE_RPMDB,
				   HIF_LOCK_MODE_PROCESS,
				   &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_object_unref (state);
}

static void
hif_state_small_step_func (void)
{
	HifState *state;
	gboolean ret;
	GError *error = NULL;
	guint i;

	_updates = 0;

	state = hif_state_new ();
	g_signal_connect (state, "percentage-changed",
			G_CALLBACK (hif_state_test_percentage_changed_cb), NULL);
	hif_state_set_number_steps (state, 100000);

	/* process all steps, we should get 100 callbacks */
	for (i = 0; i < 100000; i++) {
		ret = hif_state_done (state, &error);
		g_assert_no_error (error);
		g_assert (ret);
	}
	g_assert_cmpint (_updates, ==, 100);

	g_object_unref (state);
}

/* avoid linking into the daemon */
void pk_backend_job_package (PkBackendJob *job, PkInfoEnum info,
			     const gchar *package_id, const gchar *summary);
void pk_backend_job_package (PkBackendJob *job, PkInfoEnum info,
			     const gchar *package_id, const gchar *summary) {}
gboolean pk_directory_remove_contents (const gchar *directory);
gboolean pk_directory_remove_contents (const gchar *directory) {return TRUE;}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* don't run when using make distcheck */
	if (g_strcmp0 (DEFAULT_BACKEND, "dummy") == 0)
		return 0;

	g_test_add_func ("/hif/lock", hif_lock_func);
	g_test_add_func ("/hif/lock[threads]", hif_lock_threads_func);
	g_test_add_func ("/hif/state", hif_state_func);
	g_test_add_func ("/hif/state[child]", hif_state_child_func);
	g_test_add_func ("/hif/state[parent-1-step]", hif_state_parent_one_step_proxy_func);
	g_test_add_func ("/hif/state[no-equal]", hif_state_non_equal_steps_func);
	g_test_add_func ("/hif/state[no-progress]", hif_state_no_progress_func);
	g_test_add_func ("/hif/state[finish]", hif_state_finish_func);
	g_test_add_func ("/hif/state[speed]", hif_state_speed_func);
	g_test_add_func ("/hif/state[locking]", hif_state_locking_func);
	g_test_add_func ("/hif/state[finished]", hif_state_finished_func);
	g_test_add_func ("/hif/state[small-step]", hif_state_small_step_func);

	return g_test_run ();
}

