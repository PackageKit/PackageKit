/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-task
 * @short_description: An abstract package task GObject, dealing with unsigned
 * transactions, GPG keys and EULA requests.
 */

#include "config.h"

#include <gio/gio.h>

#include "src/pk-cleanup.h"

#include <packagekit-glib2/pk-task.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>

static void     pk_task_finalize	(GObject     *object);

#define PK_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK, PkTaskPrivate))

#define PK_TASK_TRANSACTION_CANCELLED_RETRY_TIMEOUT	2000 /* ms */

/**
 * PkTaskPrivate:
 *
 * Private #PkTask data
 **/
struct _PkTaskPrivate
{
	GPtrArray			*array;
	gboolean			 simulate;
	gboolean			 only_download;
	gboolean			 only_trusted;
	gboolean			 allow_reinstall;
	gboolean			 allow_downgrade;
};

enum {
	PROP_0,
	PROP_SIMULATE,
	PROP_ONLY_PREPARE,
	PROP_ONLY_TRUSTED,
	PROP_ALLOW_REINSTALL,
	PROP_ALLOW_DOWNGRADE,
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
	gboolean			 only_download;
	gboolean             allow_reinstall;
	gboolean             allow_downgrade;
	gboolean			 transaction_flags;
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
	gboolean			 enabled;
	gboolean			 force;
	gboolean			 recursive;
	gchar				*directory;
	gchar				**packages;
	gchar				*repo_id;
	gchar				*transaction_id;
	gchar				**values;
	PkBitfield			 filters;
	guint				 retry_id;
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
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (item->request == request)
			return item;
	}
	return NULL;
}

/**
 * pk_task_generic_state_finish:
 **/
static void
pk_task_generic_state_finish (PkTaskState *state, const GError *error)
{
	/* get result */
	if (state->ret) {
		g_simple_async_result_set_op_res_gpointer (state->res,
							   g_object_ref ((GObject*) state->results),
							   g_object_unref);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* remove from list */
	g_debug ("remove state %p", state);
	g_ptr_array_remove (state->task->priv->array, state);

	/* deallocate */
	if (state->cancellable != NULL)
		g_object_unref (state->cancellable);
	if (state->results != NULL) {
		g_object_unref (state->results);
		state->results = NULL;
	}
	if (state->retry_id != 0)
		g_source_remove (state->retry_id);
	g_free (state->directory);
	g_free (state->repo_id);
	g_free (state->transaction_id);
	g_strfreev (state->files);
	g_strfreev (state->package_ids);
	g_strfreev (state->packages);
	g_strfreev (state->values);
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
	PkBitfield transaction_flags;

	/* so the callback knows if we are serious or not */
	state->simulate = FALSE;

	/* only prepare the transaction */
	transaction_flags = state->transaction_flags;
	if (state->task->priv->only_download) {
		pk_bitfield_add (transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD);
	}
	if (state->task->priv->allow_reinstall) {
		pk_bitfield_add (transaction_flags,
				PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL);
	}
	if (state->task->priv->allow_downgrade) {
		pk_bitfield_add (transaction_flags,
				PK_TRANSACTION_FLAG_ENUM_ALLOW_DOWNGRADE);
	}

	/* do the correct action */
	if (state->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		pk_client_install_packages_async (PK_CLIENT(state->task), transaction_flags, state->package_ids,
						  state->cancellable, state->progress_callback, state->progress_user_data,
						  (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		pk_client_update_packages_async (PK_CLIENT(state->task), transaction_flags, state->package_ids,
						 state->cancellable, state->progress_callback, state->progress_user_data,
						 (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		pk_client_remove_packages_async (PK_CLIENT(state->task),
						 transaction_flags,
						 state->package_ids,
						 state->allow_deps,
						 state->autoremove,
						 state->cancellable,
						 state->progress_callback,
						 state->progress_user_data,
						 (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_FILES) {
		pk_client_install_files_async (PK_CLIENT(state->task), transaction_flags, state->files,
					       state->cancellable, state->progress_callback, state->progress_user_data,
					       (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_RESOLVE) {
		pk_client_resolve_async (PK_CLIENT(state->task), state->filters, state->packages,
					 state->cancellable, state->progress_callback, state->progress_user_data,
					 (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_NAME) {
		pk_client_search_names_async (PK_CLIENT(state->task), state->filters, state->values,
					      state->cancellable, state->progress_callback, state->progress_user_data,
					      (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		pk_client_search_details_async (PK_CLIENT(state->task), state->filters, state->values,
						state->cancellable, state->progress_callback, state->progress_user_data,
						(GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_GROUP) {
		pk_client_search_groups_async (PK_CLIENT(state->task), state->filters, state->values,
					       state->cancellable, state->progress_callback, state->progress_user_data,
					       (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_FILE) {
		pk_client_search_files_async (PK_CLIENT(state->task), state->filters, state->values,
					      state->cancellable, state->progress_callback, state->progress_user_data,
					      (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_GET_DETAILS) {
		pk_client_get_details_async (PK_CLIENT(state->task), state->package_ids,
					     state->cancellable, state->progress_callback, state->progress_user_data,
					     (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		pk_client_get_update_detail_async (PK_CLIENT(state->task), state->package_ids,
						   state->cancellable, state->progress_callback, state->progress_user_data,
						   (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		pk_client_download_packages_async (PK_CLIENT(state->task), state->package_ids, state->directory,
						   state->cancellable, state->progress_callback, state->progress_user_data,
						   (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATES) {
		pk_client_get_updates_async (PK_CLIENT(state->task), state->filters,
					     state->cancellable, state->progress_callback, state->progress_user_data,
					     (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_DEPENDS_ON) {
		pk_client_depends_on_async (PK_CLIENT(state->task), state->filters, state->package_ids, state->recursive,
					     state->cancellable, state->progress_callback, state->progress_user_data,
					     (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_GET_PACKAGES) {
		pk_client_get_packages_async (PK_CLIENT(state->task), state->filters,
					      state->cancellable, state->progress_callback, state->progress_user_data,
					      (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_REQUIRED_BY) {
		pk_client_required_by_async (PK_CLIENT(state->task), state->filters, state->package_ids, state->recursive,
					      state->cancellable, state->progress_callback, state->progress_user_data,
					      (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_WHAT_PROVIDES) {
		pk_client_what_provides_async (PK_CLIENT(state->task), state->filters, state->values,
					       state->cancellable, state->progress_callback, state->progress_user_data,
					       (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_GET_FILES) {
		pk_client_get_files_async (PK_CLIENT(state->task), state->package_ids,
					   state->cancellable, state->progress_callback, state->progress_user_data,
					   (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_GET_CATEGORIES) {
		pk_client_get_categories_async (PK_CLIENT(state->task),
						state->cancellable, state->progress_callback, state->progress_user_data,
						(GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_REFRESH_CACHE) {
		pk_client_refresh_cache_async (PK_CLIENT(state->task), state->force,
					       state->cancellable, state->progress_callback, state->progress_user_data,
					       (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_GET_REPO_LIST) {
		pk_client_get_repo_list_async (PK_CLIENT(state->task), state->filters,
					       state->cancellable, state->progress_callback, state->progress_user_data,
					       (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_REPO_ENABLE) {
		pk_client_repo_enable_async (PK_CLIENT(state->task), state->repo_id, state->enabled,
					     state->cancellable, state->progress_callback, state->progress_user_data,
					     (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else if (state->role == PK_ROLE_ENUM_REPAIR_SYSTEM) {
		pk_client_repair_system_async (PK_CLIENT(state->task), transaction_flags,
					       state->cancellable, state->progress_callback, state->progress_user_data,
					       (GAsyncReadyCallback) pk_task_ready_cb, state);
	} else {
		g_assert_not_reached ();
	}
}

/**
 * pk_task_package_filter_cb:
 **/
static gboolean
pk_task_package_filter_cb (PkPackage *package, gpointer user_data)
{
	PkInfoEnum info;
	info = pk_package_get_info (package);
	if (info == PK_INFO_ENUM_CLEANUP ||
	    info == PK_INFO_ENUM_UNTRUSTED ||
	    info == PK_INFO_ENUM_FINISHED)
		return FALSE;
	if (g_strcmp0 (pk_package_get_data (package), "local") == 0)
		return FALSE;
	return TRUE;
}

static void pk_task_do_async_simulate_action (PkTaskState *state);

/**
 * pk_task_simulate_ready_cb:
 **/
static void
pk_task_simulate_ready_cb (GObject *source_object, GAsyncResult *res, PkTaskState *state)
{
	PkTaskClass *klass = PK_TASK_GET_CLASS (state->task);
	guint i;
	guint length;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ PkPackageSack *sack = NULL;
	_cleanup_object_unref_ PkPackageSack *untrusted_sack = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;

	/* old results no longer valid */
	if (state->results != NULL) {
		g_object_unref (state->results);
		state->results = NULL;
	}

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(state->task), res, &error);
	if (results == NULL) {

		/* handle case where this is not implemented */
		if (error->code == PK_CLIENT_ERROR_NOT_SUPPORTED) {
			pk_task_do_async_action (state);
				return;
		}

		/* just abort */
		pk_task_generic_state_finish (state, error);
		return;
	}

	/* we own a copy now */
	state->results = g_object_ref (G_OBJECT(results));

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);
	if (state->exit_enum == PK_EXIT_ENUM_NEED_UNTRUSTED) {
		g_debug ("retrying with !only-trusted");
		pk_bitfield_remove (state->transaction_flags,
				    PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
		/* retry this */
		pk_task_do_async_simulate_action (state);
		return;
	}

	if (state->exit_enum != PK_EXIT_ENUM_SUCCESS) {
		/* we 'fail' with success so the application gets a
		 * chance to process the PackageKit-specific
		 * ErrorCode enumerated value and detail. */
		state->ret = TRUE;
		pk_task_generic_state_finish (state, NULL);
		return;
	}

	/* get data */
	sack = pk_results_get_package_sack (results);

	/* if we did a simulate and we got a message that a package was untrusted,
	 * there's no point trying to do the action with only-trusted */
	untrusted_sack = pk_package_sack_filter_by_info (sack, PK_INFO_ENUM_UNTRUSTED);
	if (pk_package_sack_get_size (untrusted_sack) > 0) {
		g_debug ("we got an untrusted message, so skipping only-trusted");
		pk_bitfield_remove (state->transaction_flags,
				    PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	}

	/* remove all the packages we want to ignore */
	pk_package_sack_remove_by_filter (sack, pk_task_package_filter_cb, state);

	/* remove all the original packages from the sack */
	if (state->package_ids != NULL) {
		length = g_strv_length (state->package_ids);
		for (i = 0; i < length; i++)
			pk_package_sack_remove_package_by_id (sack, state->package_ids[i]);
	}

	/* no results from simulate */
	if (pk_package_sack_get_size (sack) == 0) {
		pk_task_do_async_action (state);
		return;
	}

	/* sort the list, as clients will mostly want this */
	pk_package_sack_sort (sack, PK_PACKAGE_SACK_SORT_TYPE_INFO);

	/* run the callback */
	klass->simulate_question (state->task, state->request, state->results);
}

/**
 * pk_task_do_async_simulate_action:
 **/
static void
pk_task_do_async_simulate_action (PkTaskState *state)
{
	PkBitfield transaction_flags = state->transaction_flags;

	/* so the callback knows if we are serious or not */
	pk_bitfield_add (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE);
	state->simulate = TRUE;

	/* do the correct action */
	if (state->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		/* simulate install async */
		g_debug ("doing install");
		pk_client_install_packages_async (PK_CLIENT(state->task),
						  transaction_flags,
						  state->package_ids,
						  state->cancellable,
						  state->progress_callback,
						  state->progress_user_data,
						  (GAsyncReadyCallback) pk_task_simulate_ready_cb,
						  state);
	} else if (state->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		/* simulate update async */
		g_debug ("doing update");
		pk_client_update_packages_async (PK_CLIENT(state->task),
						 transaction_flags,
						 state->package_ids,
						 state->cancellable,
						 state->progress_callback,
						 state->progress_user_data,
						 (GAsyncReadyCallback) pk_task_simulate_ready_cb,
						 state);
	} else if (state->role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		/* simulate remove async */
		g_debug ("doing remove");
		pk_client_remove_packages_async (PK_CLIENT(state->task),
						 transaction_flags,
						 state->package_ids,
						 state->allow_deps,
						 state->autoremove,
						 state->cancellable,
						 state->progress_callback,
						 state->progress_user_data,
						 (GAsyncReadyCallback) pk_task_simulate_ready_cb,
						 state);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_FILES) {
		/* simulate install async */
		g_debug ("doing install files");
		pk_client_install_files_async (PK_CLIENT(state->task),
					       transaction_flags,
					       state->files,
					       state->cancellable,
					       state->progress_callback,
					       state->progress_user_data,
					       (GAsyncReadyCallback) pk_task_simulate_ready_cb,
					       state);
	} else if (state->role == PK_ROLE_ENUM_REPAIR_SYSTEM) {
		/* simulate repair system async */
		g_debug ("doing repair system");
		pk_client_repair_system_async (PK_CLIENT(state->task),
					       transaction_flags,
					       state->cancellable,
					       state->progress_callback,
					       state->progress_user_data,
					       (GAsyncReadyCallback) pk_task_simulate_ready_cb,
					       state);
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
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;

	/* old results no longer valid */
	if (state->results != NULL) {
		g_object_unref (state->results);
		state->results = NULL;
	}

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (results == NULL) {
		pk_task_generic_state_finish (state, error);
		return;
	}

	/* we own a copy now */
	state->results = g_object_ref (G_OBJECT(results));

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);

	/* need untrusted */
	if (state->exit_enum != PK_EXIT_ENUM_SUCCESS) {
		_cleanup_object_unref_ PkError *error_code = NULL;
		error_code = pk_results_get_error_code (state->results);
		/* TODO: convert the PkErrorEnum to a PK_CLIENT_ERROR_* enum */
		g_set_error (&error,
			     PK_CLIENT_ERROR,
			     PK_CLIENT_ERROR_FAILED, "failed to install signature: %s", pk_error_get_details (error_code));
		pk_task_generic_state_finish (state, error);
		return;
	}

	/* now try the action again */
	pk_task_do_async_action (state);
}

/**
 * pk_task_install_signatures:
 **/
static void
pk_task_install_signatures (PkTaskState *state)
{
	PkRepoSignatureRequired *item;
	PkSigTypeEnum type;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *key_id = NULL;
	_cleanup_free_ gchar *package_id = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;

	/* get results */
	array = pk_results_get_repo_signature_required_array (state->results);
	if (array == NULL || array->len == 0) {
		g_set_error (&error,
			     PK_CLIENT_ERROR,
			     PK_CLIENT_ERROR_FAILED, "no signatures to install");
		pk_task_generic_state_finish (state, error);
		return;
	}

	/* did we get more than result? */
	if (array->len > 1) {
		/* TODO: support more than one signature */
		g_set_error (&error,
			     PK_CLIENT_ERROR,
			     PK_CLIENT_ERROR_FAILED, "more than one signature to install");
		pk_task_generic_state_finish (state, error);
		return;
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
}

/**
 * pk_task_accept_eulas_ready_cb:
 **/
static void
pk_task_accept_eulas_ready_cb (GObject *source_object, GAsyncResult *res, PkTaskState *state)
{
	PkTask *task = PK_TASK (source_object);
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;

	/* old results no longer valid */
	if (state->results != NULL) {
		g_object_unref (state->results);
		state->results = NULL;
	}

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (results == NULL) {
		pk_task_generic_state_finish (state, error);
		return;
	}

	/* we own a copy now */
	state->results = g_object_ref (G_OBJECT(results));

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);

	/* need untrusted */
	if (state->exit_enum != PK_EXIT_ENUM_SUCCESS) {
		_cleanup_object_unref_ PkError *error_code = NULL;
		error_code = pk_results_get_error_code (state->results);
		/* TODO: convert the PkErrorEnum to a PK_CLIENT_ERROR_* enum */
		g_set_error (&error,
			     PK_CLIENT_ERROR,
			     PK_CLIENT_ERROR_FAILED, "failed to accept eula: %s", pk_error_get_details (error_code));
		pk_task_generic_state_finish (state, error);
		return;
	}

	/* now try the action again */
	pk_task_do_async_action (state);
}

/**
 * pk_task_accept_eulas:
 **/
static void
pk_task_accept_eulas (PkTaskState *state)
{
	PkEulaRequired *item;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *eula_id = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;

	/* get results */
	array = pk_results_get_eula_required_array (state->results);
	if (array == NULL || array->len == 0) {
		g_set_error (&error,
			     PK_CLIENT_ERROR,
			     PK_CLIENT_ERROR_FAILED, "no eulas to accept");
		pk_task_generic_state_finish (state, error);
		return;
	}

	/* did we get more than result? */
	if (array->len > 1) {
		/* TODO: support more than one eula */
		g_set_error (&error,
			     PK_CLIENT_ERROR,
			     PK_CLIENT_ERROR_FAILED, "more than one eula to accept");
		pk_task_generic_state_finish (state, error);
		return;
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
}

/**
 * pk_task_repair_ready_cb:
 **/
static void
pk_task_repair_ready_cb (GObject *source_object, GAsyncResult *res, PkTaskState *state)
{
	PkTask *task = PK_TASK (source_object);
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;

	/* old results no longer valid */
	if (state->results != NULL) {
		g_object_unref (state->results);
		state->results = NULL;
	}

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (results == NULL) {
		pk_task_generic_state_finish (state, error);
		return;
	}

	/* we own a copy now */
	state->results = g_object_ref (G_OBJECT(results));

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);

	/* need untrusted */
	if (state->exit_enum != PK_EXIT_ENUM_SUCCESS) {
		_cleanup_object_unref_ PkError *error_code = NULL;
		error_code = pk_results_get_error_code (state->results);
		/* TODO: convert the PkErrorEnum to a PK_CLIENT_ERROR_* enum */
		error = g_error_new (PK_CLIENT_ERROR,
				     PK_CLIENT_ERROR_FAILED,
				     "failed to repair: %s",
				     pk_error_get_details (error_code));
		pk_task_generic_state_finish (state, error);
		return;
	}

	/* now try the action again */
	pk_task_do_async_action (state);
}

/**
 * pk_task_user_accepted_idle_cb:
 **/
static gboolean
pk_task_user_accepted_idle_cb (PkTaskState *state)
{
	/* this needs another step in the dance */
	if (state->exit_enum == PK_EXIT_ENUM_KEY_REQUIRED) {
		g_debug ("need to do install-sig");
		pk_task_install_signatures (state);
		return FALSE;
	}

	/* this needs another step in the dance */
	if (state->exit_enum == PK_EXIT_ENUM_EULA_REQUIRED) {
		g_debug ("need to do accept-eula");
		pk_task_accept_eulas (state);
		return FALSE;
	}

	/* this needs another step in the dance */
	if (state->exit_enum == PK_EXIT_ENUM_REPAIR_REQUIRED) {
		g_debug ("need to do repair");
		pk_client_repair_system_async (PK_CLIENT(state->task),
					       pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_NONE),
					       state->cancellable,
					       state->progress_callback,
					       state->progress_user_data,
					       (GAsyncReadyCallback) pk_task_repair_ready_cb, state);
		return FALSE;
	}

	/* doing task */
	g_debug ("continuing with request %i", state->request);
	pk_task_do_async_action (state);
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
	GSource *idle_source;

	/* get the not-yet-completed request */
	state = pk_task_find_by_request (task, request);
	if (state == NULL) {
		g_warning ("request %i not found", request);
		return FALSE;
	}

	idle_source = g_idle_source_new ();
	g_source_set_callback (idle_source, (GSourceFunc) pk_task_user_accepted_idle_cb, state, NULL);
	g_source_set_name (idle_source, "[PkTask] user-accept");
	g_source_attach (idle_source, g_main_context_get_thread_default ());
	return TRUE;
}

/**
 * pk_task_user_declined_idle_cb:
 **/
static gboolean
pk_task_user_declined_idle_cb (PkTaskState *state)
{
	_cleanup_error_free_ GError *error = NULL;

	/* the introduction is finished */
	if (state->simulate) {
		g_set_error (&error,
			     PK_CLIENT_ERROR,
			     PK_CLIENT_ERROR_DECLINED_SIMULATION, "user declined simulation");
		pk_task_generic_state_finish (state, error);
		return FALSE;
	}

	/* doing task */
	g_debug ("declined request %i", state->request);
	g_set_error (&error,
			     PK_CLIENT_ERROR,
			     PK_CLIENT_ERROR_FAILED, "user declined interaction");
	pk_task_generic_state_finish (state, error);
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
	GSource *idle_source;

	/* get the not-yet-completed request */
	state = pk_task_find_by_request (task, request);
	if (state == NULL) {
		g_warning ("request %i not found", request);
		return FALSE;
	}

	idle_source = g_idle_source_new ();
	g_source_set_callback (idle_source, (GSourceFunc) pk_task_user_declined_idle_cb, state, NULL);
	g_source_set_name (idle_source, "[PkTask] user-accept");
	g_source_attach (idle_source, g_main_context_get_thread_default ());
	return TRUE;
}

/**
 * pk_task_retry_cancelled_transaction_cb:
 **/
static gboolean
pk_task_retry_cancelled_transaction_cb (gpointer user_data)
{
	PkTaskState *state = (PkTaskState *) user_data;
	pk_task_do_async_action (state);
	state->retry_id = 0;
	return FALSE;
}

/**
 * pk_task_ready_cb:
 **/
static void
pk_task_ready_cb (GObject *source_object, GAsyncResult *res, PkTaskState *state)
{
	PkTask *task = PK_TASK (source_object);
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);
	gboolean interactive;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;

	/* old results no longer valid */
	if (state->results != NULL) {
		g_object_unref (state->results);
		state->results = NULL;
	}

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (results == NULL) {
		pk_task_generic_state_finish (state, error);
		return;
	}

	/* we own a copy now */
	state->results = g_object_ref (G_OBJECT(results));

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);

	/* can we ask the user questions? */
	interactive = pk_client_get_interactive (PK_CLIENT (task));

	/* need untrusted */
	if (state->exit_enum == PK_EXIT_ENUM_NEED_UNTRUSTED) {
		pk_bitfield_remove (state->transaction_flags,
				    PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);

		/* running non-interactive */
		if (!interactive) {
			g_debug ("working non-interactive, so calling accept");
			pk_task_user_accepted (state->task, state->request);
			return;
		}

		/* no support */
		if (klass->untrusted_question == NULL) {
			g_set_error (&error,
				     PK_CLIENT_ERROR,
				     PK_CLIENT_ERROR_NOT_SUPPORTED,
				     "could not do untrusted question as no klass support");
			pk_task_generic_state_finish (state, error);
			return;
		}

		/* run the callback */
		klass->untrusted_question (task, state->request, state->results);
		return;
	}

	/* need key */
	if (state->exit_enum == PK_EXIT_ENUM_KEY_REQUIRED) {

		/* running non-interactive */
		if (!interactive) {
			g_debug ("working non-interactive, so calling accept");
			pk_task_user_accepted (state->task, state->request);
			return;
		}

		/* no support */
		if (klass->key_question == NULL) {
			g_set_error (&error,
				     PK_CLIENT_ERROR,
				     PK_CLIENT_ERROR_NOT_SUPPORTED,
				     "could not do key question as no klass support");
			pk_task_generic_state_finish (state, error);
			return;
		}

		/* run the callback */
		klass->key_question (task, state->request, state->results);
		return;
	}

	/* need repair */
	if (state->exit_enum == PK_EXIT_ENUM_REPAIR_REQUIRED) {

		/* running non-interactive */
		if (!interactive) {
			g_debug ("working non-interactive, so calling accept");
			pk_task_user_accepted (state->task, state->request);
			return;
		}

		/* no support */
		if (klass->repair_question == NULL) {
			error = g_error_new (PK_CLIENT_ERROR,
					     PK_CLIENT_ERROR_NOT_SUPPORTED,
					     "could not do repair question as no klass support");
			pk_task_generic_state_finish (state, error);
			return;
		}

		/* run the callback */
		klass->repair_question (task, state->request, state->results);
		return;
	}

	/* need EULA */
	if (state->exit_enum == PK_EXIT_ENUM_EULA_REQUIRED) {

		/* running non-interactive */
		if (!interactive) {
			g_debug ("working non-interactive, so calling accept");
			pk_task_user_accepted (state->task, state->request);
			return;
		}

		/* no support */
		if (klass->eula_question == NULL) {
			g_set_error (&error,
				     PK_CLIENT_ERROR,
				     PK_CLIENT_ERROR_NOT_SUPPORTED,
				     "could not do eula question as no klass support");
			pk_task_generic_state_finish (state, error);
			return;
		}

		/* run the callback */
		klass->eula_question (task, state->request, state->results);
		return;
	}

	/* need media change */
	if (state->exit_enum == PK_EXIT_ENUM_MEDIA_CHANGE_REQUIRED) {

		/* running non-interactive */
		if (!interactive) {
			g_debug ("working non-interactive, so calling accept");
			pk_task_user_accepted (state->task, state->request);
			return;
		}

		/* no support */
		if (klass->media_change_question == NULL) {
			g_set_error (&error,
				     PK_CLIENT_ERROR,
				     PK_CLIENT_ERROR_NOT_SUPPORTED,
				     "could not do media change question as no klass support");
			pk_task_generic_state_finish (state, error);
			return;
		}

		/* run the callback */
		klass->media_change_question (task, state->request, state->results);
		return;
	}

	/* just re-run the transaction after a small delay */
	if (state->exit_enum == PK_EXIT_ENUM_CANCELLED_PRIORITY) {
		state->retry_id = g_timeout_add (PK_TASK_TRANSACTION_CANCELLED_RETRY_TIMEOUT,
						 pk_task_retry_cancelled_transaction_cb,
						 state);
		return;
	}

	/* we can't handle this, just finish the async method */
	state->ret = TRUE;

	/* we're done */
	pk_task_generic_state_finish (state, NULL);
}

/**
 * pk_task_install_packages_async:
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Merges in details about packages using resolve.
 *
 * Since: 0.5.2
 **/
void
pk_task_install_packages_async (PkTask *task, gchar **package_ids, GCancellable *cancellable,
				PkProgressCallback progress_callback, gpointer progress_user_data,
				GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

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
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	if (task->priv->allow_reinstall) {
		pk_bitfield_add(state->transaction_flags,
			   	PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL);
	}
	if (task->priv->allow_downgrade) {
		pk_bitfield_add(state->transaction_flags,
			   	PK_TRANSACTION_FLAG_ENUM_ALLOW_DOWNGRADE);
	}
	state->package_ids = g_strdupv (package_ids);
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* start trusted install async */
	if (task->priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (state);
	else
		pk_task_do_async_action (state);
}

/**
 * pk_task_update_packages_async:
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_update_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_UPDATE_PACKAGES;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* start trusted install async */
	if (task->priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (state);
	else
		pk_task_do_async_action (state);
}

/**
 * pk_task_remove_packages_async:
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @allow_deps: if other dependent packages are allowed to be removed from the computer
 * @autoremove: if other packages installed at the same time should be tried to remove
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

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

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* start trusted install async */
	if (task->priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (state);
	else
		pk_task_do_async_action (state);
}

/**
 * pk_task_install_files_async:
 * @task: a valid #PkTask instance
 * @files: (array zero-terminated=1): a file such as "/home/hughsie/Desktop/hal-devel-0.10.0.rpm"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_files_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_INSTALL_FILES;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	if (task->priv->only_trusted)
		state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	else
		state->transaction_flags = 0;
	state->files = g_strdupv (files);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* start trusted install async */
	if (task->priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (state);
	else
		pk_task_do_async_action (state);
}

/**
 * pk_task_resolve_async:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @packages: (array zero-terminated=1): package names to find
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Resolves a package name to a package-id.
 *
 * Since: 0.6.5
 **/
void
pk_task_resolve_async (PkTask *task, PkBitfield filters, gchar **packages, GCancellable *cancellable,
		       PkProgressCallback progress_callback, gpointer progress_user_data,
		       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_RESOLVE;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	
	if (state->task->priv->allow_downgrade)
		pk_bitfield_add (state->transaction_flags,
				PK_TRANSACTION_FLAG_ENUM_ALLOW_DOWNGRADE);
	if (state->task->priv->allow_reinstall)
		pk_bitfield_add (state->transaction_flags,
				PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL);
	state->filters = filters;
	state->packages = g_strdupv (packages);
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_search_names_async:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: (array zero-terminated=1): search values
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Searches for a package name.
 *
 * Since: 0.6.5
 **/
void
pk_task_search_names_async (PkTask *task, PkBitfield filters, gchar **values, GCancellable *cancellable,
			    PkProgressCallback progress_callback, gpointer progress_user_data,
			    GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_SEARCH_NAME;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->values = g_strdupv (values);
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_search_details_async:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: (array zero-terminated=1): search values
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Searches for some package details.
 *
 * Since: 0.6.5
 **/
void
pk_task_search_details_async (PkTask *task, PkBitfield filters, gchar **values, GCancellable *cancellable,
			      PkProgressCallback progress_callback, gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_SEARCH_DETAILS;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->values = g_strdupv (values);
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_search_groups_async:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: (array zero-terminated=1): search values
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Searches the group lists.
 *
 * Since: 0.6.5
 **/
void
pk_task_search_groups_async (PkTask *task, PkBitfield filters, gchar **values, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_SEARCH_GROUP;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->values = g_strdupv (values);
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_search_files_async:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: (array zero-terminated=1): search values
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Searches for specific files.
 *
 * Since: 0.6.5
 **/
void
pk_task_search_files_async (PkTask *task, PkBitfield filters, gchar **values, GCancellable *cancellable,
			    PkProgressCallback progress_callback, gpointer progress_user_data,
			    GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_SEARCH_FILE;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->values = g_strdupv (values);
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_get_details_async:
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Gets details about packages.
 *
 * Since: 0.6.5
 **/
void
pk_task_get_details_async (PkTask *task, gchar **package_ids, GCancellable *cancellable,
			   PkProgressCallback progress_callback, gpointer progress_user_data,
			   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_DETAILS;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->package_ids = g_strdupv (package_ids);
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_get_update_detail_async:
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Gets details about updates.
 *
 * Since: 0.6.5
 **/
void
pk_task_get_update_detail_async (PkTask *task, gchar **package_ids, GCancellable *cancellable,
				 PkProgressCallback progress_callback, gpointer progress_user_data,
				 GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_UPDATE_DETAIL;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->package_ids = g_strdupv (package_ids);
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_download_packages_async:
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @directory: the destination directory
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Downloads packages
 *
 * Since: 0.6.5
 **/
void
pk_task_download_packages_async (PkTask *task, gchar **package_ids, const gchar *directory, GCancellable *cancellable,
				 PkProgressCallback progress_callback, gpointer progress_user_data,
				 GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_DOWNLOAD_PACKAGES;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->package_ids = g_strdupv (package_ids);
	state->directory = g_strdup (directory);
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_get_updates_async:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Gets the update lists.
 *
 * Since: 0.6.5
 **/
void
pk_task_get_updates_async (PkTask *task, PkBitfield filters, GCancellable *cancellable,
			   PkProgressCallback progress_callback, gpointer progress_user_data,
			   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_UPDATES;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_depends_on_async:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: if we should recurse to packages that depend on other packages
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Get the list of dependant packages.
 *
 * Since: 0.6.5
 **/
void
pk_task_depends_on_async (PkTask *task, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
			   PkProgressCallback progress_callback, gpointer progress_user_data,
			   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_DEPENDS_ON;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->package_ids = g_strdupv (package_ids);
	state->recursive = recursive;
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_get_packages_async:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Gets the list of packages.
 *
 * Since: 0.6.5
 **/
void
pk_task_get_packages_async (PkTask *task, PkBitfield filters, GCancellable *cancellable,
			    PkProgressCallback progress_callback, gpointer progress_user_data,
			    GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_PACKAGES;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_required_by_async:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: if we should return packages that depend on the ones we do
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Get the packages this package requires.
 *
 * Since: 0.6.5
 **/
void
pk_task_required_by_async (PkTask *task, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
			    PkProgressCallback progress_callback, gpointer progress_user_data,
			    GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_REQUIRED_BY;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->package_ids = g_strdupv (package_ids);
	state->recursive = recursive;
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_what_provides_async:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: (array zero-terminated=1): values to search for
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Find the package that provides some resource.
 *
 * Since: 0.6.5
 **/
void
pk_task_what_provides_async (PkTask *task, PkBitfield filters,
			     gchar **values, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_WHAT_PROVIDES;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->values = g_strdupv (values);
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_get_files_async:
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Get the files in a package.
 *
 * Since: 0.6.5
 **/
void
pk_task_get_files_async (PkTask *task, gchar **package_ids, GCancellable *cancellable,
			 PkProgressCallback progress_callback, gpointer progress_user_data,
			 GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_FILES;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->package_ids = g_strdupv (package_ids);
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_get_categories_async:
 * @task: a valid #PkTask instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Get the categories available.
 *
 * Since: 0.6.5
 **/
void
pk_task_get_categories_async (PkTask *task, GCancellable *cancellable,
			      PkProgressCallback progress_callback, gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_CATEGORIES;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_refresh_cache_async:
 * @task: a valid #PkTask instance
 * @force: if the metadata should be deleted and re-downloaded even if it is correct
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Refresh the package cache.
 *
 * Since: 0.6.5
 **/
void
pk_task_refresh_cache_async (PkTask *task, gboolean force, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_REFRESH_CACHE;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->force = force;
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_get_repo_list_async:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Get the list of available repositories.
 *
 * Since: 0.6.5
 **/
void
pk_task_get_repo_list_async (PkTask *task, PkBitfield filters, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_REPO_LIST;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_repo_enable_async:
 * @task: a valid #PkTask instance
 * @repo_id: The software source ID
 * @enabled: %TRUE or %FALSE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Enable or disable a specific repo.
 *
 * Since: 0.6.5
 **/
void
pk_task_repo_enable_async (PkTask *task, const gchar *repo_id, gboolean enabled, GCancellable *cancellable,
			   PkProgressCallback progress_callback, gpointer progress_user_data,
			   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskState *state;
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_REPO_ENABLE;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->ret = FALSE;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->repo_id = g_strdup (repo_id);
	state->enabled = enabled;
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* run task with callbacks */
	pk_task_do_async_action (state);
}

/**
 * pk_task_repair_system_async:
 * @task: a valid #PkTask instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Recover the system from broken dependencies and aborted installations.
 *
 * Since: 0.7.2
 **/
void
pk_task_repair_system_async (PkTask *task,
			     GCancellable *cancellable,
			     PkProgressCallback progress_callback,
			     gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready,
			     gpointer user_data)
{
	PkTaskState *state;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);
	_cleanup_object_unref_ GSimpleAsyncResult *res = NULL;

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (task), callback_ready, user_data, pk_task_repair_system_async);

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_REPAIR_SYSTEM;
	state->res = g_object_ref (res);
	state->task = g_object_ref (task);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->request = pk_task_generate_request_id ();

	g_debug ("adding state %p", state);
	g_ptr_array_add (task->priv->array, state);

	/* start trusted repair system async */
	if (task->priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (state);
	else
		pk_task_do_async_action (state);
}

/**
 * pk_task_generic_finish:
 * @task: a valid #PkTask instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: (transfer full): The #PkResults of the transaction.
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
 * pk_task_set_simulate:
 * @task: a valid #PkTask instance
 * @simulate: the simulate mode
 *
 * If the simulate step should be run without the actual transaction.
 *
 * Since: 0.6.10
 **/
void
pk_task_set_simulate (PkTask *task, gboolean simulate)
{
	g_return_if_fail (PK_IS_TASK (task));
	task->priv->simulate = simulate;
	g_object_notify (G_OBJECT (task), "simulate");
}

/**
 * pk_task_get_simulate:
 * @task: a valid #PkTask instance
 *
 * Gets if we are simulating.
 *
 * Return value: %TRUE if we are simulating
 *
 * Since: 0.6.10
 **/
gboolean
pk_task_get_simulate (PkTask *task)
{
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	return task->priv->simulate;
}

/**
 * pk_task_set_only_download:
 * @task: a valid #PkTask instance
 * @only_download: %FALSE to actually commit the transaction
 *
 * If the transaction should be prepared (depsolved, packages
 * downloaded, etc) but not committed.
 *
 * Since: 0.8.1
 **/
void
pk_task_set_only_download (PkTask *task, gboolean only_download)
{
	g_return_if_fail (PK_IS_TASK (task));
	task->priv->only_download = only_download;
	g_object_notify (G_OBJECT (task), "only-download");
}

/**
 * pk_task_get_only_download:
 * @task: a valid #PkTask instance
 *
 * Gets if we are just preparing the transaction for later.
 *
 * Return value: %TRUE if we are simulating
 *
 * Since: 0.8.1
 **/
gboolean
pk_task_get_only_download (PkTask *task)
{
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	return task->priv->only_download;
}


/**
 * pk_task_set_only_trusted:
 * @task: a valid #PkTask instance
 * @only_trusted: %TRUE to allow only authenticated packages
 *
 * If only authenticated packages should be allowed in the
 * transaction.
 *
 * Since: 0.9.5
 **/
void
pk_task_set_only_trusted (PkTask *task, gboolean only_trusted)
{
	g_return_if_fail (PK_IS_TASK (task));
	task->priv->only_trusted = only_trusted;
	g_object_notify (G_OBJECT (task), "only-download");
}

/**
 * pk_task_get_only_trusted:
 * @task: a valid #PkTask instance
 *
 * Gets if we allow only authenticated packages in the transactoin.
 *
 * Return value: %TRUE if we allow only authenticated packages
 *
 * Since: 0.9.5
 **/
gboolean
pk_task_get_only_trusted (PkTask *task)
{
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	return task->priv->only_trusted;
}

/**
 * pk_task_set_allow_downgrade:
 * @task: a valid #PkTask instance
 * @allow_downgrade: %TRUE to allow packages to be downgraded.
 *
 * If package downgrades shall be allowed during transaction.
 *
 * Return value: %TRUE if we allow downgrades
 *
 * Since: 1.0.2
 **/
void
pk_task_set_allow_downgrade (PkTask *task, gboolean allow_downgrade)
{
	g_return_if_fail (PK_IS_TASK (task));
	task->priv->allow_downgrade = allow_downgrade;
	g_object_notify (G_OBJECT (task), "allow-downgrade");
}

/**
 * pk_task_get_allow_downgrade:
 * @task: a valid #PkTask instance
 *
 * Gets if we are allow packages to be downgraded.
 *
 * Return value: %TRUE if package downgrades are allowed
 *
 * Since: 1.0.2
 **/
gboolean
pk_task_get_allow_downgrade (PkTask *task)
{
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	return task->priv->allow_downgrade;
}

/**
 * pk_task_set_allow_reinstall:
 * @task: a valid #PkTask instance
 * @allow_reinstall: %TRUE to allow packages to be reinstalled.
 *
 * If package reinstallation shall be allowed during transaction.
 *
 * Return value: %TRUE if we allow reinstallations
 *
 * Since: 1.0.2
 **/
void
pk_task_set_allow_reinstall (PkTask *task, gboolean allow_reinstall)
{
	g_return_if_fail (PK_IS_TASK (task));
	task->priv->allow_reinstall = allow_reinstall;
	g_object_notify (G_OBJECT (task), "allow-reinstall");
}

/**
 * pk_task_get_allow_reinstall:
 * @task: a valid #PkTask instance
 *
 * Gets if we allow packages to be reinstalled.
 *
 * Return value: %TRUE if package downgrades are allowed
 *
 * Since: 1.0.2
 **/
gboolean
pk_task_get_allow_reinstall (PkTask *task)
{
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	return task->priv->allow_reinstall;
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
	case PROP_ONLY_PREPARE:
		g_value_set_boolean (value, priv->only_download);
		break;
	case PROP_ONLY_TRUSTED:
		g_value_set_boolean (value, priv->only_trusted);
		break;
	case PROP_ALLOW_REINSTALL:
		g_value_set_boolean (value, priv->allow_reinstall);
		break;
	case PROP_ALLOW_DOWNGRADE:
		g_value_set_boolean (value, priv->allow_downgrade);
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
	case PROP_ONLY_PREPARE:
		priv->only_download = g_value_get_boolean (value);
		break;
	case PROP_ONLY_TRUSTED:
		priv->only_trusted = g_value_get_boolean (value);
		break;
	case PROP_ALLOW_REINSTALL:
		priv->allow_reinstall = g_value_get_boolean (value);
		break;
	case PROP_ALLOW_DOWNGRADE:
		priv->allow_downgrade = g_value_get_boolean (value);
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

	/**
	 * PkTask:only-download:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_boolean ("only-download", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ONLY_PREPARE, pspec);

	/**
	 * PkTask:only-trusted:
	 *
	 * Since: 0.9.5
	 */
	pspec = g_param_spec_boolean ("only-trusted", NULL, NULL,
				      TRUE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ONLY_TRUSTED, pspec);

	/**
	 * PkTask:allow-reinstall:
	 *
	 * Since: 1.0.2
	 */
	pspec = g_param_spec_boolean ("allow-reinstall", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ALLOW_REINSTALL, pspec);

	/**
	 * PkTask:allow-downgrade:
	 *
	 * Since: 1.0.2
	 */
	pspec = g_param_spec_boolean ("allow-downgrade", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ALLOW_DOWNGRADE, pspec);

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
	task->priv->allow_reinstall = FALSE;
	task->priv->allow_downgrade = FALSE;
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
