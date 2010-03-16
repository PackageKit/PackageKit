/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:pk-task
 * @short_description: An abstract package task GObject, dealing with unsigned
 * transactions, GPG keys and EULA requests.
 */

#include "config.h"

#include <gio/gio.h>

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
	gboolean			 simulate;
};

enum {
	PROP_0,
	PROP_SIMULATE,
	PROP_LAST
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
	gboolean			 simulate;
	gboolean			 only_trusted;
	gchar				**package_ids;
	gboolean			 allow_deps;
	gboolean			 autoremove;
	gchar				**files;
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

	g_return_val_if_fail (PK_IS_TASK (task), NULL);
	g_return_val_if_fail (request != 0, NULL);

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
	/* get result */
	if (state->ret) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_object_ref ((GObject*) state->results), g_object_unref);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* remove from list */
	egg_debug ("remove state %p", state);
	g_ptr_array_remove (state->task->priv->array, state);

	/* deallocate */
	if (state->cancellable != NULL)
		g_object_unref (state->cancellable);
	if (state->results != NULL)
		g_object_unref (state->results);
	g_strfreev (state->package_ids);
	g_strfreev (state->files);
	g_object_unref (state->res);
	g_object_unref (state->task);
	g_slice_free (PkTaskState, state);
}

/**
 * pk_task_do_async_action:
 **/
static void
pk_task_do_async_action (PkTaskState *state)
{
	/* so the callback knows if we are serious or not */
	state->simulate = FALSE;

	/* do the correct action */
	if (state->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		/* start install async */
		egg_debug ("doing install");
		pk_client_install_packages_async (PK_CLIENT(state->task), state->only_trusted, state->package_ids,
						  state->cancellable, state->progress_callback, state->progress_user_data,
						  (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		/* start update async */
		egg_debug ("doing update");
		pk_client_update_packages_async (PK_CLIENT(state->task), state->only_trusted, state->package_ids,
						 state->cancellable, state->progress_callback, state->progress_user_data,
						 (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		/* start remove async */
		egg_debug ("doing remove");
		pk_client_remove_packages_async (PK_CLIENT(state->task), state->package_ids, state->allow_deps, state->autoremove,
						 state->cancellable, state->progress_callback, state->progress_user_data,
						 (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		/* start update async */
		egg_debug ("doing update system");
		pk_client_update_system_async (PK_CLIENT(state->task), state->only_trusted,
					       state->cancellable, state->progress_callback, state->progress_user_data,
					       (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_FILES) {
		/* start install async */
		egg_debug ("doing install files");
		pk_client_install_files_async (PK_CLIENT(state->task), state->only_trusted, state->files,
					       state->cancellable, state->progress_callback, state->progress_user_data,
					       (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else {
		g_assert_not_reached ();
	}
}

/**
 * pk_task_simulate_ready_cb:
 **/
static void
pk_task_simulate_ready_cb (GObject *source_object, GAsyncResult *res, PkTaskState *state)
{
	PkTaskClass *klass = PK_TASK_GET_CLASS (state->task);
	GError *error = NULL;
	PkResults *results;
	PkPackageSack *sack = NULL;
	guint length;
	PkError *error_code;
	guint idx = 0;
	guint i;
	GPtrArray *array = NULL;
	GPtrArray *array_messages = NULL;
	PkPackage *item;
	gboolean ret;
	PkInfoEnum info;
	PkMessage *message;
	PkMessageEnum message_type;
	const gchar *package_id;

	/* old results no longer valid */
	if (state->results != NULL)
		g_object_unref (state->results);

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(state->task), res, &error);
	if (results == NULL) {

		/* handle case where this is not implemented */
		if (error->code == PK_CLIENT_ERROR_NOT_SUPPORTED) {
			pk_task_do_async_action (state);
			g_error_free (error);
			goto out;
		}

		/* just abort */
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* we own a copy now */
	state->results = g_object_ref (G_OBJECT(results));

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);
	if (state->exit_enum != PK_EXIT_ENUM_SUCCESS) {
		error_code = pk_results_get_error_code (state->results);
		/* TODO: convert the PkErrorEnum to a PK_CLIENT_ERROR_* enum */
		error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED,
				     "could not do simulate: %s", pk_error_get_details (error_code));
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		g_object_unref (error_code);
		goto out;
	}

	/* if we did a simulate and we got a message that a package was untrusted,
	 * there's no point trying to do the action with only-trusted */
	if (state->simulate && state->only_trusted) {
		array_messages = pk_results_get_message_array (state->results);
		for (i = 0; i < array_messages->len; i++) {
			message = g_ptr_array_index (array_messages, i);
			g_object_get (message,
				      "type", &message_type,
				      NULL);
			if (message_type == PK_MESSAGE_ENUM_UNTRUSTED_PACKAGE) {
				egg_debug ("we got an untrusted message, so skipping only-trusted");
				state->only_trusted = FALSE;
				break;
			}
		}
	}

	/* get data */
	sack = pk_results_get_package_sack (results);

	/* remove all the original packages from the sack */
	if (state->package_ids != NULL) {
		length = g_strv_length (state->package_ids);
		for (i=0; i<length; i++)
			pk_package_sack_remove_package_by_id (sack, state->package_ids[i]);

		/* remove packages from the array that will not be useful */
		array = pk_results_get_package_array (results);
		while (idx < array->len) {
			item = g_ptr_array_index (array, idx);
			package_id = pk_package_get_id (item);
			g_object_get (item,
				      "info", &info,
				      NULL);

			/* remove all the cleanup and finished packages */
			if (info == PK_INFO_ENUM_CLEANUP ||
			    info == PK_INFO_ENUM_FINISHED) {
				egg_debug ("removing %s", package_id);
				g_ptr_array_remove (array, item);
				continue;
			}

			/* remove all the original packages */
			ret = FALSE;
			for (i=0; i<length; i++) {
				if (g_strcmp0 (package_id, state->package_ids[i]) == 0) {
					egg_debug ("removing %s", package_id);
					g_ptr_array_remove (array, item);
					ret = TRUE;
					break;
				}
			}
			if (ret)
				continue;

			/* no removal done */
			idx++;
		}
	}

	/* no results from simulate */
	if (pk_package_sack_get_size (sack) == 0) {
		pk_task_do_async_action (state);
		goto out;
	}

	/* sort the list, as clients will mostly want this */
	pk_package_sack_sort (sack, PK_PACKAGE_SACK_SORT_TYPE_INFO);

	/* run the callback */
	klass->simulate_question (state->task, state->request, state->results);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (array_messages != NULL)
		g_ptr_array_unref (array_messages);
	if (results != NULL)
		g_object_unref (results);
	if (sack != NULL)
		g_object_unref (sack);
	return;
}

/**
 * pk_task_do_async_simulate_action:
 **/
static void
pk_task_do_async_simulate_action (PkTaskState *state)
{
	/* so the callback knows if we are serious or not */
	state->simulate = TRUE;

	/* do the correct action */
	if (state->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		/* simulate install async */
		egg_debug ("doing install");
		pk_client_simulate_install_packages_async (PK_CLIENT(state->task), state->package_ids,
							   state->cancellable, state->progress_callback, state->progress_user_data,
							   (GAsyncReadyCallback) pk_task_simulate_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		/* simulate update async */
		egg_debug ("doing update");
		pk_client_simulate_update_packages_async (PK_CLIENT(state->task), state->package_ids,
							  state->cancellable, state->progress_callback, state->progress_user_data,
							  (GAsyncReadyCallback) pk_task_simulate_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		/* simulate remove async */
		egg_debug ("doing remove");
		pk_client_simulate_remove_packages_async (PK_CLIENT(state->task), state->package_ids, state->autoremove,
							  state->cancellable, state->progress_callback, state->progress_user_data,
							  (GAsyncReadyCallback) pk_task_simulate_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_FILES) {
		/* simulate install async */
		egg_debug ("doing install files");
		pk_client_simulate_install_files_async (PK_CLIENT(state->task), state->files,
						        state->cancellable, state->progress_callback, state->progress_user_data,
						        (GAsyncReadyCallback) pk_task_simulate_ready_cb, state);
	} else {
		g_assert_not_reached ();
	}
}

/**
 * pk_task_install_signatures_ready_cb:
 **/
static void
pk_task_install_signatures_ready_cb (GObject *source_object, GAsyncResult *res, PkTaskState *state)
{
	PkTask *task = PK_TASK (source_object);
	GError *error = NULL;
	PkResults *results;
	PkError *error_code;

	/* old results no longer valid */
	if (state->results != NULL)
		g_object_unref (state->results);

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (results == NULL) {
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* we own a copy now */
	state->results = g_object_ref (G_OBJECT(results));

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);

	/* need untrusted */
	if (state->exit_enum != PK_EXIT_ENUM_SUCCESS) {
		error_code = pk_results_get_error_code (state->results);
		/* TODO: convert the PkErrorEnum to a PK_CLIENT_ERROR_* enum */
		error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "failed to install signature: %s", pk_error_get_details (error_code));
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		g_object_unref (error_code);
		goto out;
	}

	/* now try the action again */
	pk_task_do_async_action (state);
out:
	if (results != NULL)
		g_object_unref (results);
	return;
}

/**
 * pk_task_install_signatures:
 **/
static void
pk_task_install_signatures (PkTaskState *state)
{
	GError *error = NULL;
	GPtrArray *array;
	PkRepoSignatureRequired *item;
	gchar *key_id = NULL;
	gchar *package_id = NULL;
	PkSigTypeEnum type;

	/* get results */
	array = pk_results_get_repo_signature_required_array (state->results);
	if (array == NULL || array->len == 0) {
		error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "no signatures to install");
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* did we get more than result? */
	if (array->len > 1) {
		/* TODO: support more than one signature */
		error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "more than one signature to install");
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* get first item of data */
	item = g_ptr_array_index (array, 0);
	g_object_get (item,
		      "type", &type,
		      "key-id", &key_id,
		      "package-id", &package_id,
		      NULL);

	/* do new async method */
	pk_client_install_signature_async (PK_CLIENT(state->task), type, key_id, package_id,
					   state->cancellable, state->progress_callback, state->progress_user_data,
					   (GAsyncReadyCallback) pk_task_install_signatures_ready_cb, state);
out:
	g_free (package_id);
	g_free (key_id);
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * pk_task_accept_eulas_ready_cb:
 **/
static void
pk_task_accept_eulas_ready_cb (GObject *source_object, GAsyncResult *res, PkTaskState *state)
{
	PkTask *task = PK_TASK (source_object);
	GError *error = NULL;
	PkResults *results;
	PkError *error_code;

	/* old results no longer valid */
	if (state->results != NULL)
		g_object_unref (state->results);

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (results == NULL) {
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* we own a copy now */
	state->results = g_object_ref (G_OBJECT(results));

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);

	/* need untrusted */
	if (state->exit_enum != PK_EXIT_ENUM_SUCCESS) {
		error_code = pk_results_get_error_code (state->results);
		/* TODO: convert the PkErrorEnum to a PK_CLIENT_ERROR_* enum */
		error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "failed to accept eula: %s", pk_error_get_details (error_code));
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		g_object_unref (error_code);
		goto out;
	}

	/* now try the action again */
	pk_task_do_async_action (state);
out:
	if (results != NULL)
		g_object_unref (results);
	return;
}

/**
 * pk_task_accept_eulas:
 **/
static void
pk_task_accept_eulas (PkTaskState *state)
{
	GError *error = NULL;
	GPtrArray *array;
	PkEulaRequired *item;
	gchar *eula_id = NULL;

	/* get results */
	array = pk_results_get_eula_required_array (state->results);
	if (array == NULL || array->len == 0) {
		error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "no eulas to accept");
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* did we get more than result? */
	if (array->len > 1) {
		/* TODO: support more than one eula */
		error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "more than one eula to accept");
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* get first item of data */
	item = g_ptr_array_index (array, 0);
	g_object_get (item,
		      "eula-id", &eula_id,
		      NULL);

	/* do new async method */
	pk_client_accept_eula_async (PK_CLIENT(state->task), eula_id,
				     state->cancellable, state->progress_callback, state->progress_user_data,
				     (GAsyncReadyCallback) pk_task_accept_eulas_ready_cb, state);
out:
	g_free (eula_id);
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * pk_task_user_accepted_idle_cb:
 **/
static gboolean
pk_task_user_accepted_idle_cb (PkTaskState *state)
{
	/* this needs another step in the dance */
	if (state->exit_enum == PK_EXIT_ENUM_KEY_REQUIRED) {
		egg_debug ("need to do install-sig");
		pk_task_install_signatures (state);
		goto out;
	}

	/* this needs another step in the dance */
	if (state->exit_enum == PK_EXIT_ENUM_EULA_REQUIRED) {
		egg_debug ("need to do accept-eula");
		pk_task_accept_eulas (state);
		goto out;
	}

	/* doing task */
	egg_debug ("continuing with request %i", state->request);
	pk_task_do_async_action (state);

out:
	/* never repeat */
	return FALSE;
}

/**
 * pk_task_user_accepted:
 *
 * Since: 0.5.2
 **/
gboolean
pk_task_user_accepted (PkTask *task, guint request)
{
	PkTaskState *state;

	/* get the not-yet-completed request */
	state = pk_task_find_by_request (task, request);
	if (state == NULL) {
		egg_warning ("request %i not found", request);
		return FALSE;
	}

	g_idle_add ((GSourceFunc) pk_task_user_accepted_idle_cb, state);
	return TRUE;
}

/**
 * pk_task_user_declined_idle_cb:
 **/
static gboolean
pk_task_user_declined_idle_cb (PkTaskState *state)
{
	GError *error;

	/* the introduction is finished */
	if (state->simulate) {
		error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_DECLINED_SIMULATION, "user declined simulation");
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* doing task */
	egg_debug ("declined request %i", state->request);
	error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "user declined interaction");
	pk_task_generic_state_finish (state, error);
	g_error_free (error);

out:
	/* never repeat */
	return FALSE;
}

/**
 * pk_task_user_declined:
 *
 * Since: 0.5.2
 **/
gboolean
pk_task_user_declined (PkTask *task, guint request)
{
	PkTaskState *state;

	/* get the not-yet-completed request */
	state = pk_task_find_by_request (task, request);
	if (state == NULL) {
		egg_warning ("request %i not found", request);
		return FALSE;
	}

	g_idle_add ((GSourceFunc) pk_task_user_declined_idle_cb, state);
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
	PkResults *results;

	/* old results no longer valid */
	if (state->results != NULL)
		g_object_unref (state->results);

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (results == NULL) {
		pk_task_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* we own a copy now */
	state->results = g_object_ref (G_OBJECT(results));

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);

	/* need untrusted */
	if (state->exit_enum == PK_EXIT_ENUM_NEED_UNTRUSTED) {
		state->only_trusted = FALSE;

		/* no support */
		if (klass->untrusted_question == NULL) {
			error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NOT_SUPPORTED,
					     "could not do untrusted question as no klass support");
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
			error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NOT_SUPPORTED,
					     "could not do key question as no klass support");
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
			error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NOT_SUPPORTED,
					     "could not do eula question as no klass support");
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
			error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NOT_SUPPORTED,
					     "could not do media change question as no klass support");
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
	if (results != NULL)
		g_object_unref (results);
	return;
}

/**
 * pk_task_install_packages_async:
 * @task: a valid #PkTask instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Merges in details about packages using resolve.
 *
 * Since: 0.5.2
 **/
void
pk_task_install_packages_async (PkTask *task, gchar **package_ids, GCancellable *cancellable,
				PkProgressCallback progress_callback, gpointer progress_user_data,
				GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkTaskState *state;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (task), callback, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_INSTALL_PACKAGES;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->only_trusted = TRUE;
	state->package_ids = g_strdupv (package_ids);
	state->request = pk_task_generate_request_id ();

	egg_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* start trusted install async */
	if (task->priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (state);
	else
		pk_task_do_async_action (state);

	g_object_unref (res);
}

/**
 * pk_task_update_packages_async:
 * @task: a valid #PkTask instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Update specific packages to the newest available versions.
 *
 * Since: 0.5.2
 **/
void
pk_task_update_packages_async (PkTask *task, gchar **package_ids, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkTaskState *state;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_update_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_UPDATE_PACKAGES;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->only_trusted = TRUE;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->request = pk_task_generate_request_id ();

	egg_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* start trusted install async */
	if (task->priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (state);
	else
		pk_task_do_async_action (state);

	g_object_unref (res);
}

/**
 * pk_task_remove_packages_async:
 * @task: a valid #PkTask instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @allow_deps: if other dependant packages are allowed to be removed from the computer
 * @autoremove: if other packages installed at the same time should be tried to remove
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Remove a package (optionally with dependancies) from the system.
 * If %allow_deps is set to %FALSE, and other packages would have to be removed,
 * then the transaction would fail.
 *
 * Since: 0.5.2
 **/
void
pk_task_remove_packages_async (PkTask *task, gchar **package_ids, gboolean allow_deps, gboolean autoremove, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkTaskState *state;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_remove_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_REMOVE_PACKAGES;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->allow_deps = allow_deps;
	state->autoremove = autoremove;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->request = pk_task_generate_request_id ();

	egg_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* start trusted install async */
	if (task->priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (state);
	else
		pk_task_do_async_action (state);

	g_object_unref (res);
}

/**
 * pk_task_install_files_async:
 * @task: a valid #PkTask instance
 * @files: a file such as "/home/hughsie/Desktop/hal-devel-0.10.0.rpm"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Install a file locally, and get the deps from the repositories.
 * This is useful for double clicking on a .rpm or .deb file.
 *
 * Since: 0.5.2
 **/
void
pk_task_install_files_async (PkTask *task, gchar **files, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkTaskState *state;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_files_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_INSTALL_FILES;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->only_trusted = TRUE;
	state->files = g_strdupv (files);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->request = pk_task_generate_request_id ();

	egg_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* start trusted install async */
	if (task->priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (state);
	else
		pk_task_do_async_action (state);

	g_object_unref (res);
}

/**
 * pk_task_update_system_async:
 * @task: a valid #PkTask instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Update all the packages on the system with the highest versions found in all
 * repositories.
 * NOTE: you can't choose what repositories to update from, but you can do:
 * - pk_task_repo_disable()
 * - pk_task_update_system()
 * - pk_task_repo_enable()
 *
 * Since: 0.5.2
 **/
void
pk_task_update_system_async (PkTask *task, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkTaskState *state;

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_update_system_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_UPDATE_SYSTEM;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->only_trusted = TRUE;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->request = pk_task_generate_request_id ();

	egg_debug ("adding state %p", state);
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
 *
 * Since: 0.5.2
 **/
PkResults *
pk_task_generic_finish (PkTask *task, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (PK_IS_TASK (task), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_object_ref (g_simple_async_result_get_op_res_gpointer (simple));
}


/**
 * pk_task_get_property:
 **/
static void
pk_task_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkTask *task = PK_TASK (object);
	PkTaskPrivate *priv = task->priv;

	switch (prop_id) {
	case PROP_SIMULATE:
		g_value_set_boolean (value, priv->simulate);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_task_set_property:
 **/
static void
pk_task_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkTask *task = PK_TASK (object);
	PkTaskPrivate *priv = task->priv;

	switch (prop_id) {
	case PROP_SIMULATE:
		priv->simulate = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_task_class_init:
 **/
static void
pk_task_class_init (PkTaskClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_task_finalize;
	object_class->get_property = pk_task_get_property;
	object_class->set_property = pk_task_set_property;

	/**
	 * PkTask:simulate:
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_boolean ("simulate", NULL, NULL,
				      TRUE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SIMULATE, pspec);

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
	task->priv->simulate = TRUE;
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
 *
 * Since: 0.5.2
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
#include <packagekit-glib2/pk-package-ids.h>
#include "egg-test.h"

static void
pk_task_test_install_packages_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkTask *task = PK_TASK (object);
	GError *error = NULL;
	PkResults *results;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results != NULL) {
		egg_test_failed (test, "finish should fail!");
		goto out;
	}

	/* check error */
	if (g_strcmp0 (error->message, "could not do untrusted question as no klass support") != 0) {
		egg_test_failed (test, "wrong message: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_error_free (error);
	if (results != NULL)
		g_object_unref (results);
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
		egg_debug ("now %s", pk_status_enum_to_string (status));
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
	package_ids = pk_package_ids_from_id ("glib2;2.14.0;i386;fedora");
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

