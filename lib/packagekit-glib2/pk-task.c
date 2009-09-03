/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offtask: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <packagekit-glib2/pk-task.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>

#include "egg-debug.h"

static void     pk_task_finalize	(GObject     *object);

#define PK_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK, PkTaskPrivate))

/**
 * PkTaskPrivate:
 *
 * Private #PkTask data
 **/
struct _PkTaskPrivate
{
	GPtrArray			*array;
};

/**
 * PkTaskState:
 *
 * For use in the async methods
 **/
typedef struct {
	guint				 request;
	PkRoleEnum			 role;
	PkExitEnum			 exit_enum;
	gboolean			 only_trusted;
	gchar				**package_ids;
	GSimpleAsyncResult		*res;
	PkResults			*results;
	gboolean			 ret;
	PkTask				*task;
	GCancellable			*cancellable;
	PkProgressCallback		 progress_callback;
	gpointer			 progress_user_data;
} PkTaskState;

G_DEFINE_TYPE (PkTask, pk_task, PK_TYPE_CLIENT)

static void pk_task_ready_cb (GObject *source_object, GAsyncResult *res, PkTaskState *state);

/**
 * pk_task_generate_request_id:
 **/
static guint
pk_task_generate_request_id (void)
{
	static guint id = 0;
	return ++id;
}

/**
 * pk_task_find_by_request:
 **/
static PkTaskState *
pk_task_find_by_request (PkTask *task, guint request)
{
	PkTaskState *item;
	guint i;
	GPtrArray *array;

	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	g_return_val_if_fail (request != 0, FALSE);

	array = task->priv->array;
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (item->request == request)
			goto out;
	}
	item = NULL;
out:
	return item;
}

/**
 * pk_task_generic_state_finish:
 **/
static void
pk_task_generic_state_finish (PkTaskState *state, const GError *error)
{
	/* remove weak ref */
	if (state->task != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->task), (gpointer) &state->task);

	/* cancel */
	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	/* get result */
	if (state->ret) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_object_ref (state->results), g_object_unref);
	} else {
		/* FIXME: change g_simple_async_result_set_from_error() to accept const GError */
		g_simple_async_result_set_from_error (state->res, (GError*) error);
	}

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* remove from list */
	egg_warning ("remove state");
	g_ptr_array_remove (state->task->priv->array, state);

	/* deallocate */
	g_strfreev (state->package_ids);
	g_object_unref (state->res);
	g_slice_free (PkTaskState, state);
}

/**
 * pk_task_do_async_action:
 **/
static void
pk_task_do_async_action (PkTaskState *state)
{
	/* do the correct action */
	if (state->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		/* start install async */
		egg_debug ("doing install untrusted");
		pk_client_install_packages_async (PK_CLIENT(state->task), state->only_trusted, state->package_ids,
						  state->cancellable, state->progress_callback, state->progress_user_data,
						  (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else {
		g_assert_not_reached ();
	}
}

/**
 * pk_task_user_acceptance_idle_cb:
 **/
static gboolean
pk_task_user_acceptance_idle_cb (PkTaskState *state)
{
	if (state->exit_enum == PK_EXIT_ENUM_KEY_REQUIRED)
		egg_error ("need to do install-sig");
	if (state->exit_enum == PK_EXIT_ENUM_EULA_REQUIRED)
		egg_error ("need to do accept-eula");

	/* doing task */
	egg_debug ("continuing with request %i", state->request);
	pk_task_do_async_action (state);

	/* never repeat */
	return FALSE;
}

/**
 * pk_task_user_acceptance:
 **/
gboolean
pk_task_user_acceptance (PkTask *task, guint request)
{
	PkTaskState *state;

	/* get the not-yet-completed request */
	state = pk_task_find_by_request (task, request);
	if (state == NULL) {
		egg_warning ("request %i not found", request);
		return FALSE;
	}

	g_idle_add ((GSourceFunc) pk_task_user_acceptance_idle_cb, state);
	return TRUE;
}

/**
 * pk_task_ready_cb:
 **/
static void
pk_task_ready_cb (GObject *source_object, GAsyncResult *res, PkTaskState *state)
{
	PkTask *task = PK_TASK (source_object);
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);
	GError *error = NULL;

	/* get the results */
	state->results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (state->results == NULL) {
		egg_warning ("failed to resolve: %s", error->message);
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);

	/* need untrusted */
	if (state->exit_enum == PK_EXIT_ENUM_NEED_UNTRUSTED) {
		state->only_trusted = FALSE;

		/* no support */
		if (klass->untrusted_question == NULL) {
			error = g_error_new (1, 0, "could not do untrusted question as no klass support");
			pk_task_generic_state_finish (state, error);
			g_error_free (error);
			goto out;
		}

		/* run the callback */
		klass->untrusted_question (task, state->request, state->results);
		goto out;
	}

	/* need key */
	if (state->exit_enum == PK_EXIT_ENUM_KEY_REQUIRED) {
		/* no support */
		if (klass->key_question == NULL) {
			error = g_error_new (1, 0, "could not do key question as no klass support");
			pk_task_generic_state_finish (state, error);
			g_error_free (error);
			goto out;
		}

		/* run the callback */
		klass->key_question (task, state->request, state->results);
		goto out;
	}

	/* need EULA */
	if (state->exit_enum == PK_EXIT_ENUM_EULA_REQUIRED) {
		/* no support */
		if (klass->eula_question == NULL) {
			error = g_error_new (1, 0, "could not do eula question as no klass support");
			pk_task_generic_state_finish (state, error);
			g_error_free (error);
			goto out;
		}

		/* run the callback */
		klass->eula_question (task, state->request, state->results);
		goto out;
	}

	/* need media change */
	if (state->exit_enum == PK_EXIT_ENUM_MEDIA_CHANGE_REQUIRED) {
		/* no support */
		if (klass->media_change_question == NULL) {
			error = g_error_new (1, 0, "could not do media change question as no klass support");
			pk_task_generic_state_finish (state, error);
			g_error_free (error);
			goto out;
		}

		/* run the callback */
		klass->media_change_question (task, state->request, state->results);
		goto out;
	}

	/* we can't handle this, just finish the async method */
	state->ret = TRUE;

	/* we're done */
	pk_task_generic_state_finish (state, error);
out:
	return;
}

/**
 * pk_task_install_packages_async:
 * @task: a valid #PkTask instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Merges in details about packages using resolve.
 **/
void
pk_task_install_packages_async (PkTask *task, gchar **package_ids, GCancellable *cancellable,
				PkProgressCallback progress_callback, gpointer progress_user_data,
				GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkTaskState *state;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (task), callback, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_INSTALL_PACKAGES;
	state->res = g_object_ref (res);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->task = task;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->only_trusted = TRUE;
	state->package_ids = g_strdupv (package_ids);
	state->request = pk_task_generate_request_id ();
	g_object_add_weak_pointer (G_OBJECT (state->task), (gpointer) &state->task);

	egg_warning ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* start trusted install async */
	pk_task_do_async_action (state);

	g_object_unref (res);
}

/**
 * pk_task_generic_finish:
 * @task: a valid #PkTask instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: %TRUE for success
 **/
PkResults *
pk_task_generic_finish (PkTask *task, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (res);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/**
 * pk_task_class_init:
 **/
static void
pk_task_class_init (PkTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_task_finalize;

	g_type_class_add_private (klass, sizeof (PkTaskPrivate));
}

/**
 * pk_task_init:
 **/
static void
pk_task_init (PkTask *task)
{
	task->priv = PK_TASK_GET_PRIVATE (task);
	task->priv->array = g_ptr_array_new ();
}

/**
 * pk_task_finalize:
 **/
static void
pk_task_finalize (GObject *object)
{
	PkTask *task = PK_TASK (object);
	g_ptr_array_unref (task->priv->array);
	G_OBJECT_CLASS (pk_task_parent_class)->finalize (object);
}

/**
 * pk_task_new:
 *
 * Return value: a new PkTask object.
 **/
PkTask *
pk_task_new (void)
{
	PkTask *task;
	task = g_object_new (PK_TYPE_TASK, NULL);
	return PK_TASK (task);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static void
pk_task_test_install_packages_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkTask *task = PK_TASK (object);
	GError *error = NULL;
	PkResults *results = NULL;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results != NULL) {
		egg_test_failed (test, "finish should fail!");
		return;
	}

	/* check error */
	if (g_strcmp0 (error->message, "could not do untrusted question as no klass support") != 0) {
		egg_test_failed (test, "wrong message: %s", error->message);
		g_error_free (error);
		return;
	}

	g_error_free (error);
	egg_test_loop_quit (test);
}

static void
pk_task_test_progress_cb (PkProgress *progress, PkProgressType type, EggTest *test)
{
	PkStatusEnum status;
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
			      "status", &status,
			      NULL);
		egg_debug ("now %s", pk_status_enum_to_text (status));
	}
}

void
pk_task_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	PkTask *task;
	gchar **package_ids;

	if (!egg_test_start (test, "PkTask"))
		return;

	/************************************************************/
	egg_test_title (test, "get task");
	task = pk_task_new ();
	egg_test_assert (test, task != NULL);

	/************************************************************/
	egg_test_title (test, "install package");
	package_ids = g_strsplit ("glib2;2.14.0;i386;fedora", ",", -1);
	pk_task_install_packages_async (task, package_ids, NULL,
				        (PkProgressCallback) pk_task_test_progress_cb, test,
				        (GAsyncReadyCallback) pk_task_test_install_packages_cb, test);
	g_strfreev (package_ids);
	egg_test_loop_wait (test, 150000);
	egg_test_success (test, "installed in %i", egg_test_elapsed (test));

	g_object_unref (task);
	egg_test_end (test);
}
#endif

