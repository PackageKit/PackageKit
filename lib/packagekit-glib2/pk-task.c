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

#include <packagekit-glib2/pk-task.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>

static void     pk_task_finalize	(GObject     *object);

#define PK_TASK_TRANSACTION_CANCELLED_RETRY_TIMEOUT	2000 /* ms */

/**
 * PkTaskPrivate:
 *
 * Private #PkTask data
 **/
struct _PkTaskPrivate
{
	GHashTable			*gtasks; /* uint, GTask* */
	gboolean			 simulate;
	gboolean			 only_download;
	gboolean			 only_trusted;
	gboolean			 allow_reinstall;
	gboolean			 allow_downgrade;
};

enum {
	PROP_0,
	PROP_SIMULATE,
	PROP_ONLY_DOWNLOAD,
	PROP_ONLY_TRUSTED,
	PROP_ALLOW_REINSTALL,
	PROP_ALLOW_DOWNGRADE,
	PROP_LAST
};

static GParamSpec *obj_properties[PROP_LAST] = { NULL, };

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
	gboolean			 allow_reinstall;
	gboolean			 allow_downgrade;
	gboolean			 transaction_flags;
	gchar				**package_ids;
	gboolean			 allow_deps;
	gboolean			 autoremove;
	gchar				**files;
	PkResults			*results;
	PkProgressCallback		 progress_callback;
	gpointer			 progress_user_data;
	gboolean			 enabled;
	gboolean			 force;
	gboolean			 recursive;
	gchar				*directory;
	gchar				*distro_id;
	gchar				**packages;
	gchar				*repo_id;
	gchar				*transaction_id;
	gchar				**values;
	PkBitfield			 filters;
	PkUpgradeKindEnum		 upgrade_kind;
	guint				 retry_id;
} PkTaskState;

G_DEFINE_TYPE_WITH_PRIVATE (PkTask, pk_task, PK_TYPE_CLIENT)

static void pk_task_ready_cb (GObject *source_object, GAsyncResult *res, gpointer user_data);

/*
 * pk_task_generate_request_id:
 **/
static guint
pk_task_generate_request_id (void)
{
	static guint id = 0;
	return ++id;
}

/*
 * pk_task_find_by_request:
 **/
static inline GTask *
pk_task_find_by_request (PkTask *task, guint request)
{
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	g_return_val_if_fail (PK_IS_TASK (task), NULL);
	g_return_val_if_fail (request != 0, NULL);

	return g_hash_table_lookup (priv->gtasks, GUINT_TO_POINTER (request));
}

/*
 * pk_task_generic_state_finish:
 **/
static void
pk_task_state_free (gpointer task_state)
{
	PkTaskState *state = task_state;

	if (state->retry_id != 0)
		g_source_remove (state->retry_id);
	g_free (state->directory);
	g_free (state->distro_id);
	g_free (state->repo_id);
	g_free (state->transaction_id);
	g_strfreev (state->files);
	g_strfreev (state->package_ids);
	g_strfreev (state->packages);
	g_strfreev (state->values);
	g_slice_free (PkTaskState, state);
}

/*
 * pk_task_do_async_action:
 **/
static void
pk_task_do_async_action (GTask *gtask)
{
	PkTask *task = g_task_get_source_object (gtask);
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state = g_task_get_task_data (gtask);
	GCancellable *cancellable = g_task_get_cancellable (gtask);
	PkBitfield transaction_flags;

	/* so the callback knows if we are serious or not */
	state->simulate = FALSE;

	/* only prepare the transaction */
	transaction_flags = state->transaction_flags;
	if (priv->only_download) {
		pk_bitfield_add (transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD);
	}
	if (priv->allow_reinstall) {
		pk_bitfield_add (transaction_flags,
				PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL);
	}
	if (priv->allow_downgrade) {
		pk_bitfield_add (transaction_flags,
				PK_TRANSACTION_FLAG_ENUM_ALLOW_DOWNGRADE);
	}

	/* do the correct action */
	if (state->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		pk_client_install_packages_async (PK_CLIENT(task), transaction_flags, state->package_ids,
						  cancellable, state->progress_callback, state->progress_user_data,
						  pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		pk_client_update_packages_async (PK_CLIENT(task), transaction_flags, state->package_ids,
						 cancellable, state->progress_callback, state->progress_user_data,
						 pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		pk_client_remove_packages_async (PK_CLIENT(task),
						 transaction_flags,
						 state->package_ids,
						 state->allow_deps,
						 state->autoremove,
						 cancellable,
						 state->progress_callback,
						 state->progress_user_data,
						 pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_INSTALL_FILES) {
		pk_client_install_files_async (PK_CLIENT(task), transaction_flags, state->files,
					       cancellable, state->progress_callback, state->progress_user_data,
					       pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_RESOLVE) {
		pk_client_resolve_async (PK_CLIENT(task), state->filters, state->packages,
					 cancellable, state->progress_callback, state->progress_user_data,
					 pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_SEARCH_NAME) {
		pk_client_search_names_async (PK_CLIENT(task), state->filters, state->values,
					      cancellable, state->progress_callback, state->progress_user_data,
					      pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		pk_client_search_details_async (PK_CLIENT(task), state->filters, state->values,
						cancellable, state->progress_callback, state->progress_user_data,
						pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_SEARCH_GROUP) {
		pk_client_search_groups_async (PK_CLIENT(task), state->filters, state->values,
					       cancellable, state->progress_callback, state->progress_user_data,
					       pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_SEARCH_FILE) {
		pk_client_search_files_async (PK_CLIENT(task), state->filters, state->values,
					      cancellable, state->progress_callback, state->progress_user_data,
					      pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_GET_DETAILS) {
		pk_client_get_details_async (PK_CLIENT(task), state->package_ids,
					     cancellable, state->progress_callback, state->progress_user_data,
					     pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		pk_client_get_update_detail_async (PK_CLIENT(task), state->package_ids,
						   cancellable, state->progress_callback, state->progress_user_data,
						   pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		pk_client_download_packages_async (PK_CLIENT(task), state->package_ids, state->directory,
						   cancellable, state->progress_callback, state->progress_user_data,
						   pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATES) {
		pk_client_get_updates_async (PK_CLIENT(task), state->filters,
					     cancellable, state->progress_callback, state->progress_user_data,
					     pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_DEPENDS_ON) {
		pk_client_depends_on_async (PK_CLIENT(task), state->filters, state->package_ids, state->recursive,
					     cancellable, state->progress_callback, state->progress_user_data,
					     pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_GET_PACKAGES) {
		pk_client_get_packages_async (PK_CLIENT(task), state->filters,
					      cancellable, state->progress_callback, state->progress_user_data,
					      pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_REQUIRED_BY) {
		pk_client_required_by_async (PK_CLIENT(task), state->filters, state->package_ids, state->recursive,
					      cancellable, state->progress_callback, state->progress_user_data,
					      pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_WHAT_PROVIDES) {
		pk_client_what_provides_async (PK_CLIENT(task), state->filters, state->values,
					       cancellable, state->progress_callback, state->progress_user_data,
					       pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_GET_FILES) {
		pk_client_get_files_async (PK_CLIENT(task), state->package_ids,
					   cancellable, state->progress_callback, state->progress_user_data,
					   pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_GET_CATEGORIES) {
		pk_client_get_categories_async (PK_CLIENT(task),
						cancellable, state->progress_callback, state->progress_user_data,
						pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_REFRESH_CACHE) {
		pk_client_refresh_cache_async (PK_CLIENT(task), state->force,
					       cancellable, state->progress_callback, state->progress_user_data,
					       pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_GET_REPO_LIST) {
		pk_client_get_repo_list_async (PK_CLIENT(task), state->filters,
					       cancellable, state->progress_callback, state->progress_user_data,
					       pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_REPO_ENABLE) {
		pk_client_repo_enable_async (PK_CLIENT(task), state->repo_id, state->enabled,
					     cancellable, state->progress_callback, state->progress_user_data,
					     pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_UPGRADE_SYSTEM) {
		pk_client_upgrade_system_async (PK_CLIENT(task), transaction_flags, state->distro_id, state->upgrade_kind,
						cancellable, state->progress_callback, state->progress_user_data,
						pk_task_ready_cb, g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_REPAIR_SYSTEM) {
		pk_client_repair_system_async (PK_CLIENT(task), transaction_flags,
					       cancellable, state->progress_callback, state->progress_user_data,
					       pk_task_ready_cb, g_steal_pointer (&gtask));
	} else {
		g_assert_not_reached ();
	}
}

/*
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

static void pk_task_do_async_simulate_action (GTask *gtask);

/*
 * pk_task_simulate_ready_cb:
 **/
static void
pk_task_simulate_ready_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) gtask = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(PkPackageSack) sack = NULL;
	g_autoptr(PkPackageSack) untrusted_sack = NULL;
	PkTask *task = g_task_get_source_object (gtask);
	PkTaskState *state = g_task_get_task_data (gtask);

	/* old results no longer valid */
	g_clear_object (&state->results);

	/* get the results */
	state->results = pk_client_generic_finish (PK_CLIENT(source_object), res, &error);
	if (state->results == NULL) {

		/* handle case where this is not implemented */
		if (error->code == PK_CLIENT_ERROR_NOT_SUPPORTED) {
			pk_task_do_async_action (g_steal_pointer (&gtask));
			return;
		}

		/* just abort */
		g_task_return_error (gtask, g_steal_pointer (&error));
		return;
	}

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);
	if (state->exit_enum == PK_EXIT_ENUM_NEED_UNTRUSTED) {
		g_debug ("retrying with !only-trusted");
		pk_bitfield_remove (state->transaction_flags,
				    PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
		/* retry this */
		pk_task_do_async_simulate_action (g_steal_pointer (&gtask));
		return;
	}

	if (state->exit_enum != PK_EXIT_ENUM_SUCCESS) {
		/* we 'fail' with success so the application gets a
		 * chance to process the PackageKit-specific
		 * ErrorCode enumerated value and detail. */
		g_task_return_pointer (gtask, g_steal_pointer (&state->results), g_object_unref);
		return;
	}

	/* get data */
	sack = pk_results_get_package_sack (state->results);

	/* if we did a simulate and we got a message that a package was untrusted,
	 * there's no point trying to do the action with only-trusted */
	untrusted_sack = pk_package_sack_filter_by_info (sack, PK_INFO_ENUM_UNTRUSTED);
	if (pk_package_sack_get_size (untrusted_sack) > 0) {
		g_debug ("we got an untrusted message, so skipping only-trusted");
		pk_bitfield_remove (state->transaction_flags,
				    PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	}

	/* remove all the packages we want to ignore */
	pk_package_sack_remove_by_filter (sack, pk_task_package_filter_cb, NULL);

	/* no results from simulate */
	if (pk_package_sack_get_size (sack) == 0) {
		pk_task_do_async_action (g_steal_pointer (&gtask));
		return;
	}

	/* sort the list, as clients will mostly want this */
	pk_package_sack_sort (sack, PK_PACKAGE_SACK_SORT_TYPE_INFO);

	/* run the callback */
	PK_TASK_GET_CLASS (task)->simulate_question (task, state->request, state->results);
}

/*
 * pk_task_do_async_simulate_action:
 **/
static void
pk_task_do_async_simulate_action (GTask *gtask)
{
	PkTask *task = g_task_get_source_object (gtask);
	PkTaskState *state = g_task_get_task_data (gtask);
	GCancellable *cancellable = g_task_get_cancellable (gtask);
	PkBitfield transaction_flags = state->transaction_flags;

	/* so the callback knows if we are serious or not */
	pk_bitfield_add (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE);
	state->simulate = TRUE;

	/* do the correct action */
	if (state->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		/* simulate install async */
		g_debug ("doing install");
		pk_client_install_packages_async (PK_CLIENT(task),
						  transaction_flags,
						  state->package_ids,
						  cancellable,
						  state->progress_callback,
						  state->progress_user_data,
						  pk_task_simulate_ready_cb,
						  g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		/* simulate update async */
		g_debug ("doing update");
		pk_client_update_packages_async (PK_CLIENT(task),
						 transaction_flags,
						 state->package_ids,
						 cancellable,
						 state->progress_callback,
						 state->progress_user_data,
						 pk_task_simulate_ready_cb,
						 g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		/* simulate remove async */
		g_debug ("doing remove");
		pk_client_remove_packages_async (PK_CLIENT(task),
						 transaction_flags,
						 state->package_ids,
						 state->allow_deps,
						 state->autoremove,
						 cancellable,
						 state->progress_callback,
						 state->progress_user_data,
						 pk_task_simulate_ready_cb,
						 g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_INSTALL_FILES) {
		/* simulate install async */
		g_debug ("doing install files");
		pk_client_install_files_async (PK_CLIENT(task),
					       transaction_flags,
					       state->files,
					       cancellable,
					       state->progress_callback,
					       state->progress_user_data,
					       pk_task_simulate_ready_cb,
					       g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_UPGRADE_SYSTEM) {
		/* simulate upgrade system async */
		g_debug ("doing upgrade system");
		pk_client_upgrade_system_async (PK_CLIENT(task),
						transaction_flags,
						state->distro_id,
						state->upgrade_kind,
						cancellable,
						state->progress_callback,
						state->progress_user_data,
						pk_task_simulate_ready_cb,
						g_steal_pointer (&gtask));
	} else if (state->role == PK_ROLE_ENUM_REPAIR_SYSTEM) {
		/* simulate repair system async */
		g_debug ("doing repair system");
		pk_client_repair_system_async (PK_CLIENT(task),
					       transaction_flags,
					       cancellable,
					       state->progress_callback,
					       state->progress_user_data,
					       pk_task_simulate_ready_cb,
					       g_steal_pointer (&gtask));
	} else {
		g_assert_not_reached ();
	}
}

/*
 * pk_task_install_signatures_ready_cb:
 **/
static void
pk_task_install_signatures_ready_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) gtask = G_TASK (user_data);
	PkTask *task = PK_TASK (source_object);
	PkTaskState *state = g_task_get_task_data (gtask);
	g_autoptr(GError) error = NULL;

	/* old results no longer valid */
	g_clear_object (&state->results);

	/* get the results */
	state->results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (state->results == NULL) {
		g_task_return_error (gtask, g_steal_pointer (&error));
		return;
	}

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);

	/* need untrusted */
	if (state->exit_enum != PK_EXIT_ENUM_SUCCESS) {
		g_autoptr(PkError) error_code = NULL;
		error_code = pk_results_get_error_code (state->results);
		/* TODO: convert the PkErrorEnum to a PK_CLIENT_ERROR_* enum */
		g_task_return_new_error (gtask,
					 PK_CLIENT_ERROR,
					 PK_CLIENT_ERROR_FAILED,
					 "failed to install signature: %s",
					 pk_error_get_details (error_code));
		return;
	}

	/* now try the action again */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/*
 * pk_task_install_signatures:
 **/
static void
pk_task_install_signatures (GTask *given_gtask)
{
	GTask *gtask = given_gtask;
	PkTask *task = g_task_get_source_object (gtask);
	PkTaskState *state = g_task_get_task_data (gtask);
	PkRepoSignatureRequired *item;
	PkSigTypeEnum type;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *key_id = NULL;
	g_autofree gchar *package_id = NULL;
	g_autoptr(GPtrArray) array = NULL;
	GCancellable *cancellable;

	/* get results */
	array = pk_results_get_repo_signature_required_array (state->results);
	if (array == NULL || array->len == 0) {
		g_task_return_new_error (gtask,
					 PK_CLIENT_ERROR,
					 PK_CLIENT_ERROR_FAILED,
					 "no signatures to install");
		return;
	}

	/* did we get more than result? */
	if (array->len > 1) {
		/* TODO: support more than one signature */
		g_task_return_new_error (gtask,
					 PK_CLIENT_ERROR,
					 PK_CLIENT_ERROR_FAILED,
					 "more than one signature to install");
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
	cancellable = g_task_get_cancellable (gtask);
	pk_client_install_signature_async (PK_CLIENT(task), type, key_id, package_id,
					   cancellable, state->progress_callback, state->progress_user_data,
					   pk_task_install_signatures_ready_cb, g_object_ref (gtask));
}

/*
 * pk_task_accept_eulas_ready_cb:
 **/
static void
pk_task_accept_eulas_ready_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) gtask = G_TASK (user_data);
	PkTask *task = PK_TASK (source_object);
	PkTaskState *state = g_task_get_task_data (gtask);
	g_autoptr(GError) error = NULL;

	/* old results no longer valid */
	g_clear_object (&state->results);

	/* get the results */
	state->results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (state->results == NULL) {
		g_task_return_error (gtask, g_steal_pointer (&error));
		return;
	}

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);

	/* need untrusted */
	if (state->exit_enum != PK_EXIT_ENUM_SUCCESS) {
		g_autoptr(PkError) error_code = NULL;
		error_code = pk_results_get_error_code (state->results);
		/* TODO: convert the PkErrorEnum to a PK_CLIENT_ERROR_* enum */
		g_task_return_new_error (gtask,
					 PK_CLIENT_ERROR,
					 PK_CLIENT_ERROR_FAILED,
					 "failed to accept eula: %s",
					 pk_error_get_details (error_code));
		return;
	}

	/* now try the action again */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/*
 * pk_task_accept_eulas:
 **/
static void
pk_task_accept_eulas (GTask *given_gtask)
{
	GTask *gtask = given_gtask;
	PkTask *task = g_task_get_source_object (gtask);
	PkTaskState *state = g_task_get_task_data (gtask);
	PkEulaRequired *item;
	g_autoptr(GError) error = NULL;
	const gchar *eula_id;
	g_autoptr(GPtrArray) array = NULL;
	GCancellable *cancellable;

	/* get results */
	array = pk_results_get_eula_required_array (state->results);
	if (array == NULL || array->len == 0) {
		g_task_return_new_error (gtask,
					 PK_CLIENT_ERROR,
					 PK_CLIENT_ERROR_FAILED,
					 "no eulas to accept");
		return;
	}

	/* did we get more than result? */
	if (array->len > 1) {
		/* TODO: support more than one eula */
		g_task_return_new_error (gtask,
					 PK_CLIENT_ERROR,
					 PK_CLIENT_ERROR_FAILED,
					 "more than one eula to accept");
		return;
	}

	/* get first item of data */
	item = g_ptr_array_index (array, 0);
	eula_id = pk_eula_required_get_eula_id (item);

	/* do new async method */
	cancellable = g_task_get_cancellable (gtask);
	pk_client_accept_eula_async (PK_CLIENT(task), eula_id,
				     cancellable, state->progress_callback, state->progress_user_data,
				     pk_task_accept_eulas_ready_cb, g_object_ref (gtask));
}

/*
 * pk_task_repair_ready_cb:
 **/
static void
pk_task_repair_ready_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) gtask = G_TASK (user_data);
	PkTaskState *state = g_task_get_task_data (gtask);
	PkTask *task = PK_TASK (source_object);
	g_autoptr(GError) error = NULL;

	/* old results no longer valid */
	g_clear_object (&state->results);

	/* get the results */
	state->results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (state->results == NULL) {
		g_task_return_error (gtask, g_steal_pointer (&error));
		return;
	}

	/* get exit code */
	state->exit_enum = pk_results_get_exit_code (state->results);

	/* need untrusted */
	if (state->exit_enum != PK_EXIT_ENUM_SUCCESS) {
		g_autoptr(PkError) error_code = NULL;
		error_code = pk_results_get_error_code (state->results);
		/* TODO: convert the PkErrorEnum to a PK_CLIENT_ERROR_* enum */
		g_task_return_new_error (gtask,
					 PK_CLIENT_ERROR,
					 PK_CLIENT_ERROR_FAILED,
					 "failed to repair: %s",
					 pk_error_get_details (error_code));
		return;
	}

	/* now try the action again */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/*
 * pk_task_user_accepted_idle_cb:
 **/
static gboolean
pk_task_user_accepted_idle_cb (gpointer user_data)
{
	GTask *gtask = user_data;
	PkTaskState *state = g_task_get_task_data (gtask);

	/* this needs another step in the dance */
	if (state->exit_enum == PK_EXIT_ENUM_KEY_REQUIRED) {
		g_debug ("need to do install-sig");
		pk_task_install_signatures (gtask);
		return FALSE;
	}

	/* this needs another step in the dance */
	if (state->exit_enum == PK_EXIT_ENUM_EULA_REQUIRED) {
		g_debug ("need to do accept-eula");
		pk_task_accept_eulas (gtask);
		return FALSE;
	}

	/* this needs another step in the dance */
	if (state->exit_enum == PK_EXIT_ENUM_REPAIR_REQUIRED) {
		GCancellable *cancellable = g_task_get_cancellable (gtask);
		PkTask *task = g_task_get_source_object (gtask);
		g_debug ("need to do repair");
		pk_client_repair_system_async (PK_CLIENT(task),
					       pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_NONE),
					       cancellable,
					       state->progress_callback,
					       state->progress_user_data,
					       (GAsyncReadyCallback) pk_task_repair_ready_cb, g_object_ref (gtask));
		return FALSE;
	}

	/* doing task */
	g_debug ("continuing with request %i", state->request);
	pk_task_do_async_action (g_object_ref (gtask));
	return FALSE;
}

/**
 * pk_task_user_accepted:
 * @task: a valid #PkTask instance
 * @request: request ID for EULA.
 *
 * Mark a EULA as accepted by the user.
 *
 * Return value: %TRUE if @request is valid.
 *
 * Since: 0.5.2
 **/
gboolean
pk_task_user_accepted (PkTask *task, guint request)
{
	GTask *gtask;
	GSource *idle_source;

	/* get the not-yet-completed request */
	gtask = pk_task_find_by_request (task, request);
	if (gtask == NULL) {
		g_warning ("request %i not found", request);
		return FALSE;
	}

	idle_source = g_idle_source_new ();
	g_source_set_callback (idle_source, G_SOURCE_FUNC (pk_task_user_accepted_idle_cb), g_object_ref (gtask), g_object_unref);
	g_source_set_name (idle_source, "[PkTask] user-accept");
	g_source_attach (idle_source, g_main_context_get_thread_default ());
	return TRUE;
}

/*
 * pk_task_user_declined_idle_cb:
 **/
static gboolean
pk_task_user_declined_idle_cb (gpointer user_data)
{
	GTask *gtask = user_data;
	PkTaskState *state;

	state = g_task_get_task_data (gtask);

	/* the introduction is finished */
	if (state->simulate) {
		g_task_return_new_error (gtask,
					 PK_CLIENT_ERROR, PK_CLIENT_ERROR_DECLINED_SIMULATION,
					 "user declined simulation");
		return FALSE;
	}

	/* doing task */
	g_debug ("declined request %i", state->request);
	g_task_return_new_error (gtask,
				 PK_CLIENT_ERROR, PK_CLIENT_ERROR_DECLINED_INTERACTION,
				 "user declined interaction");
	return FALSE;
}

/**
 * pk_task_user_declined:
 * @task: a valid #PkTask instance
 * @request: request ID for EULA.
 *
 * Mark a EULA as declined by the user.
 *
 * Return value: %TRUE if @request is valid.
 *
 * Since: 0.5.2
 **/
gboolean
pk_task_user_declined (PkTask *task, guint request)
{
	GTask *gtask;
	GSource *idle_source;

	/* get the not-yet-completed request */
	gtask = pk_task_find_by_request (task, request);
	if (gtask == NULL) {
		g_warning ("request %i not found", request);
		return FALSE;
	}

	idle_source = g_idle_source_new ();
	g_source_set_callback (idle_source, G_SOURCE_FUNC (pk_task_user_declined_idle_cb), g_object_ref (gtask), g_object_unref);
	g_source_set_name (idle_source, "[PkTask] user-accept");
	g_source_attach (idle_source, g_main_context_get_thread_default ());
	return TRUE;
}

/*
 * pk_task_retry_cancelled_transaction_cb:
 **/
static gboolean
pk_task_retry_cancelled_transaction_cb (gpointer user_data)
{
	g_autoptr(GTask) gtask = G_TASK (user_data);
	PkTaskState *state = g_task_get_task_data (gtask);

	pk_task_do_async_action (g_steal_pointer (&gtask));
	state->retry_id = 0;
	return G_SOURCE_REMOVE;
}

/*
 * pk_task_ready_cb:
 **/
static void
pk_task_ready_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) gtask = G_TASK (user_data);
	PkTask *task = PK_TASK (source_object);
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);
	PkTaskState *state = g_task_get_task_data (gtask);
	gboolean interactive;
	g_autoptr(GError) error = NULL;

	/* old results no longer valid */
	g_clear_object (&state->results);

	/* get the results */
	state->results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (state->results == NULL) {
		g_task_return_error (gtask, g_steal_pointer (&error));
		return;
	}

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
			pk_task_user_accepted (task, state->request);
			return;
		}

		/* no support */
		if (klass->untrusted_question == NULL) {
			g_task_return_new_error (gtask,
						 PK_CLIENT_ERROR,
						 PK_CLIENT_ERROR_NOT_SUPPORTED,
						 "could not do untrusted question as no klass support");
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
			pk_task_user_accepted (task, state->request);
			return;
		}

		/* no support */
		if (klass->key_question == NULL) {
			g_task_return_new_error (gtask,
						 PK_CLIENT_ERROR,
						 PK_CLIENT_ERROR_NOT_SUPPORTED,
						 "could not do key question as no klass support");
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
			pk_task_user_accepted (task, state->request);
			return;
		}

		/* no support */
		if (klass->repair_question == NULL) {
			g_task_return_new_error (gtask,
						 PK_CLIENT_ERROR,
						 PK_CLIENT_ERROR_NOT_SUPPORTED,
						 "could not do repair question as no klass support");
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
			pk_task_user_accepted (task, state->request);
			return;
		}

		/* no support */
		if (klass->eula_question == NULL) {
			g_task_return_new_error (gtask,
						 PK_CLIENT_ERROR,
						 PK_CLIENT_ERROR_NOT_SUPPORTED,
						 "could not do eula question as no klass support");
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
			pk_task_user_accepted (task, state->request);
			g_task_return_pointer (gtask, g_steal_pointer (&state->results), g_object_unref);
			return;
		}

		/* no support */
		if (klass->media_change_question == NULL) {
			g_task_return_new_error (gtask,
						 PK_CLIENT_ERROR,
						 PK_CLIENT_ERROR_NOT_SUPPORTED,
						 "could not do media change question as no klass support");
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
						 g_steal_pointer (&gtask));
		return;
	}

	/* we're done */
	g_task_return_pointer (gtask, g_steal_pointer (&state->results), g_object_unref);
}

/**
 * pk_task_install_packages_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_INSTALL_PACKAGES;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	if (priv->allow_reinstall) {
		pk_bitfield_add (state->transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL);
	}
	if (priv->allow_downgrade) {
		pk_bitfield_add (state->transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ALLOW_DOWNGRADE);
	}
	state->package_ids = g_strdupv (package_ids);
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_packages_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* start trusted install async */
	if (priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (g_steal_pointer (&gtask));
	else
		pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_update_packages_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_UPDATE_PACKAGES;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_packages_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* start trusted install async */
	if (priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (g_steal_pointer (&gtask));
	else
		pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_upgrade_system_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @distro_id: a distro ID such as "fedora-14"
 * @upgrade_kind: a #PkUpgradeKindEnum such as %PK_UPGRADE_KIND_ENUM_COMPLETE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * This transaction will update the distro to the next version, which may
 * involve just downloading the installer and setting up the boot device,
 * or may involve doing an on-line upgrade.
 *
 * The backend will decide what is best to do.
 *
 * Since: 1.0.12
 **/
void
pk_task_upgrade_system_async (PkTask *task,
                              const gchar *distro_id,
                              PkUpgradeKindEnum upgrade_kind,
                              GCancellable *cancellable,
                              PkProgressCallback progress_callback, gpointer progress_user_data,
                              GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_UPGRADE_SYSTEM;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->distro_id = g_strdup (distro_id);
	state->upgrade_kind = upgrade_kind;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_upgrade_system_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* start trusted install async */
	if (priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (g_steal_pointer (&gtask));
	else
		pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_remove_packages_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @allow_deps: if other dependent packages are allowed to be removed from the computer
 * @autoremove: if other packages installed at the same time should be tried to remove
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Remove a package (optionally with dependencies) from the system.
 * If @allow_deps is set to %FALSE, and other packages would have to be removed,
 * then the transaction would fail.
 *
 * Since: 0.5.2
 **/
void
pk_task_remove_packages_async (PkTask *task, gchar **package_ids, gboolean allow_deps, gboolean autoremove, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_REMOVE_PACKAGES;
	state->allow_deps = allow_deps;
	state->autoremove = autoremove;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_packages_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* start trusted install async */
	if (priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (g_steal_pointer (&gtask));
	else
		pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_install_files_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @files: (array zero-terminated=1): a file such as "/home/hughsie/Desktop/hal-devel-0.10.0.rpm"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_INSTALL_FILES;
	if (priv->only_trusted)
		state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	else
		state->transaction_flags = 0;
	state->files = g_strdupv (files);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_files_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* start trusted install async */
	if (priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (g_steal_pointer (&gtask));
	else
		pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_resolve_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @packages: (array zero-terminated=1): package names to find
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_RESOLVE;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	
	if (priv->allow_downgrade)
		pk_bitfield_add (state->transaction_flags,
				PK_TRANSACTION_FLAG_ENUM_ALLOW_DOWNGRADE);
	if (priv->allow_reinstall)
		pk_bitfield_add (state->transaction_flags,
				PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL);
	state->filters = filters;
	state->packages = g_strdupv (packages);
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_resolve_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_search_names_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: (array zero-terminated=1): search values
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_SEARCH_NAME;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->values = g_strdupv (values);
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_search_names_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_search_details_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: (array zero-terminated=1): search values
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_SEARCH_DETAILS;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->values = g_strdupv (values);
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_search_details_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_search_groups_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: (array zero-terminated=1): search values
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_SEARCH_GROUP;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->values = g_strdupv (values);
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_search_groups_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_search_files_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: (array zero-terminated=1): search values
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_SEARCH_FILE;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->values = g_strdupv (values);
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_files_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_get_details_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_DETAILS;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->package_ids = g_strdupv (package_ids);
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_get_details_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_get_update_detail_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_UPDATE_DETAIL;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->package_ids = g_strdupv (package_ids);
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_get_update_detail_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_download_packages_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @directory: the destination directory
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_DOWNLOAD_PACKAGES;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->package_ids = g_strdupv (package_ids);
	state->directory = g_strdup (directory);
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_download_packages_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_get_updates_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_UPDATES;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_get_updates_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_depends_on_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: if we should recurse to packages that depend on other packages
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Get the list of dependent packages.
 *
 * Since: 0.6.5
 **/
void
pk_task_depends_on_async (PkTask *task, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
			   PkProgressCallback progress_callback, gpointer progress_user_data,
			   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_DEPENDS_ON;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->package_ids = g_strdupv (package_ids);
	state->recursive = recursive;
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_depends_on_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_get_packages_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_PACKAGES;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_packages_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_required_by_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: if we should return packages that depend on the ones we do
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_REQUIRED_BY;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->package_ids = g_strdupv (package_ids);
	state->recursive = recursive;
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_packages_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_what_provides_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: (array zero-terminated=1): values to search for
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_WHAT_PROVIDES;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->values = g_strdupv (values);
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_packages_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_get_files_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_FILES;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->package_ids = g_strdupv (package_ids);
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_packages_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_get_categories_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: (scope async): the function to run on completion
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_CATEGORIES;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_packages_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_refresh_cache_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @force: if the metadata should be deleted and re-downloaded even if it is correct
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_REFRESH_CACHE;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->force = force;
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_packages_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_get_repo_list_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_GET_REPO_LIST;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->filters = filters;
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_packages_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_repo_enable_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @repo_id: The software repository ID
 * @enabled: %TRUE or %FALSE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_TASK (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_REPO_ENABLE;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->repo_id = g_strdup (repo_id);
	state->enabled = enabled;
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_install_packages_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);

	/* run task with callbacks */
	pk_task_do_async_action (g_steal_pointer (&gtask));
}

/**
 * pk_task_repair_system_async: (finish-func pk_task_generic_finish):
 * @task: a valid #PkTask instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	PkTaskState *state;
	PkTaskClass *klass = PK_TASK_GET_CLASS (task);
	g_autoptr(GTask) gtask = NULL;

	g_return_if_fail (PK_IS_CLIENT (task));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (PkTaskState);
	state->role = PK_ROLE_ENUM_REPAIR_SYSTEM;
	state->transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->request = pk_task_generate_request_id ();

	gtask = g_task_new (task, cancellable, callback_ready, user_data);
	g_task_set_source_tag (gtask, pk_task_repair_system_async);
	g_debug ("adding state %p", state);
	g_hash_table_insert (priv->gtasks, GUINT_TO_POINTER (state->request), g_object_ref (gtask));
	g_task_set_task_data (gtask, g_steal_pointer (&state), pk_task_state_free);


	/* start trusted repair system async */
	if (priv->simulate && klass->simulate_question != NULL)
		pk_task_do_async_simulate_action (g_steal_pointer (&gtask));
	else
		pk_task_do_async_action (g_steal_pointer (&gtask));
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);
	GTask *gtask;
	PkTaskState *state;

	g_return_val_if_fail (PK_IS_TASK (task), NULL);
	g_return_val_if_fail (g_task_is_valid (res, task), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gtask = G_TASK (res);
	state = g_task_get_task_data (gtask);
	/* remove from table */
	g_debug ("remove state %p", state);
	g_hash_table_remove (priv->gtasks, GUINT_TO_POINTER (state->request));

	return g_task_propagate_pointer (gtask, error);
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	g_return_if_fail (PK_IS_TASK (task));

	if (priv->simulate == simulate)
		return;

	priv->simulate = simulate;
	g_object_notify_by_pspec (G_OBJECT(task), obj_properties[PROP_SIMULATE]);
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	return priv->simulate;
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	g_return_if_fail (PK_IS_TASK (task));

	if (priv->only_download == only_download)
		return;

	priv->only_download = only_download;
	g_object_notify_by_pspec (G_OBJECT(task), obj_properties[PROP_ONLY_DOWNLOAD]);
}

/**
 * pk_task_get_only_download:
 * @task: a valid #PkTask instance
 *
 * Gets if we are just preparing the transaction for later.
 *
 * Return value: %TRUE if only downloading
 *
 * Since: 0.8.1
 **/
gboolean
pk_task_get_only_download (PkTask *task)
{
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	return priv->only_download;
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	g_return_if_fail (PK_IS_TASK (task));

	if (priv->only_trusted == only_trusted)
		return;

	priv->only_trusted = only_trusted;
	g_object_notify_by_pspec (G_OBJECT(task), obj_properties[PROP_ONLY_TRUSTED]);
}

/**
 * pk_task_get_only_trusted:
 * @task: a valid #PkTask instance
 *
 * Gets if we allow only authenticated packages in the transaction.
 *
 * Return value: %TRUE if we allow only authenticated packages
 *
 * Since: 0.9.5
 **/
gboolean
pk_task_get_only_trusted (PkTask *task)
{
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	return priv->only_trusted;
}

/**
 * pk_task_set_allow_downgrade:
 * @task: a valid #PkTask instance
 * @allow_downgrade: %TRUE to allow packages to be downgraded.
 *
 * If package downgrades shall be allowed during transaction.
 *
 * Since: 1.0.2
 **/
void
pk_task_set_allow_downgrade (PkTask *task, gboolean allow_downgrade)
{
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	g_return_if_fail (PK_IS_TASK (task));

	if (priv->allow_downgrade == allow_downgrade)
		return;

	priv->allow_downgrade = allow_downgrade;
	g_object_notify_by_pspec (G_OBJECT(task), obj_properties[PROP_ALLOW_DOWNGRADE]);
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
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	return priv->allow_downgrade;
}

/**
 * pk_task_set_allow_reinstall:
 * @task: a valid #PkTask instance
 * @allow_reinstall: %TRUE to allow packages to be reinstalled.
 *
 * If package reinstallation shall be allowed during transaction.
 *
 * Since: 1.0.2
 **/
void
pk_task_set_allow_reinstall (PkTask *task, gboolean allow_reinstall)
{
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	g_return_if_fail (PK_IS_TASK (task));

	if (priv->allow_reinstall == allow_reinstall)
		return;

	priv->allow_reinstall = allow_reinstall;
	g_object_notify_by_pspec (G_OBJECT (task), obj_properties[PROP_ALLOW_REINSTALL]);
}

/**
 * pk_task_get_allow_reinstall:
 * @task: a valid #PkTask instance
 *
 * Gets if we allow packages to be reinstalled.
 *
 * Return value: %TRUE if package reinstallation is allowed
 *
 * Since: 1.0.2
 **/
gboolean
pk_task_get_allow_reinstall (PkTask *task)
{
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	return priv->allow_reinstall;
}

/*
 * pk_task_get_property:
 **/
static void
pk_task_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkTask *task = PK_TASK (object);
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	switch (prop_id) {
	case PROP_SIMULATE:
		g_value_set_boolean (value, priv->simulate);
		break;
	case PROP_ONLY_DOWNLOAD:
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

/*
 * pk_task_set_property:
 **/
static void
pk_task_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkTask *task = PK_TASK (object);
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	switch (prop_id) {
	case PROP_SIMULATE:
		priv->simulate = g_value_get_boolean (value);
		break;
	case PROP_ONLY_DOWNLOAD:
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

/*
 * pk_task_class_init:
 **/
static void
pk_task_class_init (PkTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_task_finalize;
	object_class->get_property = pk_task_get_property;
	object_class->set_property = pk_task_set_property;

	/**
	 * PkTask:simulate:
	 *
         * %TRUE if we are simulating.
         *
	 * Since: 0.5.2
	 */
	obj_properties[PROP_SIMULATE] =
		g_param_spec_boolean ("simulate", NULL, NULL,
				      TRUE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * PkTask:only-download:
	 *
         * %TRUE if we are just preparing the transaction for later.
         *
	 * Since: 0.8.1
	 */
	obj_properties[PROP_ONLY_DOWNLOAD] =
		g_param_spec_boolean ("only-download", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * PkTask:only-trusted:
	 *
         * %TRUE if only authenticated packages should be allowed in the transaction.
         *
	 * Since: 0.9.5
	 */
	obj_properties[PROP_ONLY_TRUSTED] =
		g_param_spec_boolean ("only-trusted", NULL, NULL,
				      TRUE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * PkTask:allow-reinstall:
	 *
         * %TRUE if package reinstallation shall be allowed during transaction.
         *
	 * Since: 1.0.2
	 */
	obj_properties[PROP_ALLOW_REINSTALL] =
		g_param_spec_boolean ("allow-reinstall", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * PkTask:allow-downgrade:
	 *
         * %TRUE if package downgrades are allowed.
         *
	 * Since: 1.0.2
	 */
	obj_properties[PROP_ALLOW_DOWNGRADE] =
		g_param_spec_boolean ("allow-downgrade", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, PROP_LAST, obj_properties);
}

/*
 * pk_task_init:
 **/
static void
pk_task_init (PkTask *task)
{
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	task->priv = priv;
	priv->gtasks = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
	priv->simulate = TRUE;
	priv->allow_reinstall = FALSE;
	priv->allow_downgrade = FALSE;
}

/*
 * pk_task_finalize:
 **/
static void
pk_task_finalize (GObject *object)
{
	PkTask *task = PK_TASK (object);
	PkTaskPrivate *priv = pk_task_get_instance_private (task);

	g_clear_pointer (&priv->gtasks, g_hash_table_unref);

	G_OBJECT_CLASS (pk_task_parent_class)->finalize (object);
}

/**
 * pk_task_new:
 *
 * Return value: a new #PkTask object.
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
