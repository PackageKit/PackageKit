/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>

#include <gmodule.h>
#include <libgbus.h>

#include <pk-common.h>
#include <pk-package-id.h>
#include <pk-enum.h>
#include <pk-network.h>

#include "pk-debug.h"
#include "pk-runner.h"
#include "pk-backend.h"
#include "pk-backend-internal.h"
#include "pk-marshal.h"
#include "pk-enum.h"
#include "pk-inhibit.h"
#include "pk-thread-list.h"
#include "pk-package-list.h"

#define PK_RUNNER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_RUNNER, PkRunnerPrivate))

struct PkRunnerPrivate
{
	GModule			*handle;
	gchar			*name;
	PkRoleEnum		 role;
	PkStatusEnum		 status;
	gboolean		 finished;
	gboolean		 allow_cancel;
	gboolean		 cached_force;
	gboolean		 cached_allow_deps;
	gboolean		 cached_autoremove;
	gboolean		 cached_enabled;
	gchar			*cached_package_id;
	gchar			*cached_transaction_id;
	gchar			*cached_full_path;
	gchar			*cached_filter;
	gchar			*cached_search;
	gchar			*cached_repo_id;
	gchar			*cached_parameter;
	gchar			*cached_value;
	LibGBus			*libgbus;
	PkNetwork		*network;
	PkBackend		*backend;
	PkInhibit		*inhibit;
	gulong			 signal_package;
	gulong			 signal_finished;
	gulong			 signal_status;
	gulong			 signal_allow_cancel;
	/* needed for gui coldplugging */
	gchar			*last_package;
	gchar			*dbus_name;
	gchar			*tid;
	PkThreadList		*thread_list;
	PkPackageList		*package_list;
};

G_DEFINE_TYPE (PkRunner, pk_runner, G_TYPE_OBJECT)

enum {
	PK_RUNNER_CALLER_ACTIVE_CHANGED,
	PK_RUNNER_LAST_SIGNAL
};

static guint signals [PK_RUNNER_LAST_SIGNAL] = { 0 };

/**
 * pk_runner_get_package_list:
 **/
PkPackageList *
pk_runner_get_package_list (PkRunner *runner)
{
	g_return_val_if_fail (runner != NULL, NULL);
	g_return_val_if_fail (PK_IS_RUNNER (runner), NULL);
	return runner->priv->package_list;
}

/**
 * pk_runner_set_role:
 * We should only set this when we are creating a manual cache
 **/
gboolean
pk_runner_set_role (PkRunner *runner, PkRoleEnum role)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	g_return_val_if_fail (PK_IS_RUNNER (runner), FALSE);

	/* save this */
	runner->priv->role = role;
	return TRUE;
}

//package_cb
//	/* save in case we need this from coldplug */
//	g_free (backend->priv->last_package);
//	backend->priv->last_package = g_strdup (package);

/**
 * pk_runner_get_package:
 **/
gboolean
pk_runner_get_package (PkRunner *runner, gchar **package_id)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	g_return_val_if_fail (PK_IS_RUNNER (runner), FALSE);

	if (runner->priv->last_package == NULL) {
		return FALSE;
	}
	*package_id = g_strdup (runner->priv->last_package);
	return TRUE;
}

/**
 * pk_runner_get_allow_cancel:
 **/
gboolean
pk_runner_get_allow_cancel (PkRunner *runner)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	g_return_val_if_fail (PK_IS_RUNNER (runner), FALSE);
	return runner->priv->allow_cancel;
}

/**
 * pk_runner_get_status:
 *
 * Even valid when the backend has moved on
 **/
PkStatusEnum
pk_runner_get_status (PkRunner *runner)
{
	g_return_val_if_fail (runner != NULL, PK_ROLE_ENUM_UNKNOWN);
	g_return_val_if_fail (PK_IS_RUNNER (runner), PK_ROLE_ENUM_UNKNOWN);
	return runner->priv->status;
}

/**
 * pk_runner_get_role:
 **/
PkRoleEnum
pk_runner_get_role (PkRunner *runner)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	g_return_val_if_fail (PK_IS_RUNNER (runner), FALSE);

	/* check to see if we have an action */
	return runner->priv->role;
}

/**
 * pk_runner_get_text:
 **/
const gchar *
pk_runner_get_text (PkRunner *runner)
{
	g_return_val_if_fail (runner != NULL, NULL);
	g_return_val_if_fail (PK_IS_RUNNER (runner), NULL);

	if (runner->priv->cached_package_id != NULL) {
		return runner->priv->cached_package_id;
	} else if (runner->priv->cached_search != NULL) {
		return runner->priv->cached_search;
	}

	return NULL;
}

/**
 * pk_runner_cancel:
 */
gboolean
pk_runner_cancel (PkRunner *runner, gchar **error_text)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	g_return_val_if_fail (error_text != NULL, FALSE);

	/* not implemented yet */
	if (runner->priv->backend->desc->cancel == NULL) {
		*error_text = g_strdup ("Operation not yet supported by runner");
		return FALSE;
	}

	/* have we already been marked as finished? */
	if (runner->priv->finished == TRUE) {
		*error_text = g_strdup ("Already finished");
		return FALSE;
	}

	/* check to see if we have an action */
	if (runner->priv->role == PK_ROLE_ENUM_UNKNOWN) {
		*error_text = g_strdup ("No role");
		return FALSE;
	}

	/* check if it's safe to kill */
	if (runner->priv->allow_cancel == FALSE) {
		*error_text = g_strdup ("Tried to cancel a runner that is not safe to kill");
		return FALSE;
	}

	/* actually run the method */
	runner->priv->backend->desc->cancel (runner->priv->backend);
	return TRUE;
}

/**
 * pk_runner_set_running:
 */
static gboolean
pk_runner_set_running (PkRunner *runner)
{
	PkBackendDesc *desc;

	g_return_val_if_fail (runner != NULL, FALSE);

	/* assign */
	pk_backend_set_current_tid (runner->priv->backend, runner->priv->tid);

	/* i don't think we actually need to do this */
	pk_backend_set_role (runner->priv->backend, runner->priv->role);

	/* we are no longer waiting, we are setting up */
	pk_backend_set_status (runner->priv->backend, PK_STATUS_ENUM_SETUP);

	/* lets reduce pointer dereferences... */
	desc = runner->priv->backend->desc;

	/* do the correct action with the cached parameters */
	if (runner->priv->role == PK_ROLE_ENUM_GET_DEPENDS) {
		desc->get_depends (runner->priv->backend,
					    runner->priv->cached_package_id,
					    runner->priv->cached_force);
	} else if (runner->priv->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		desc->get_update_detail (runner->priv->backend,
						  runner->priv->cached_package_id);
	} else if (runner->priv->role == PK_ROLE_ENUM_RESOLVE) {
		desc->resolve (runner->priv->backend, runner->priv->cached_filter,
					runner->priv->cached_package_id);
	} else if (runner->priv->role == PK_ROLE_ENUM_ROLLBACK) {
		desc->rollback (runner->priv->backend, runner->priv->cached_transaction_id);
	} else if (runner->priv->role == PK_ROLE_ENUM_GET_DESCRIPTION) {
		desc->get_description (runner->priv->backend,
						runner->priv->cached_package_id);
	} else if (runner->priv->role == PK_ROLE_ENUM_GET_FILES) {
		desc->get_files (runner->priv->backend,
					  runner->priv->cached_package_id);
	} else if (runner->priv->role == PK_ROLE_ENUM_GET_REQUIRES) {
		desc->get_requires (runner->priv->backend,
					     runner->priv->cached_package_id,
					     runner->priv->cached_force);
	} else if (runner->priv->role == PK_ROLE_ENUM_GET_UPDATES) {
		desc->get_updates (runner->priv->backend);
	} else if (runner->priv->role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		desc->search_details (runner->priv->backend,
					       runner->priv->cached_filter,
					       runner->priv->cached_search);
	} else if (runner->priv->role == PK_ROLE_ENUM_SEARCH_FILE) {
		desc->search_file (runner->priv->backend,
					    runner->priv->cached_filter,
					    runner->priv->cached_search);
	} else if (runner->priv->role == PK_ROLE_ENUM_SEARCH_GROUP) {
		desc->search_group (runner->priv->backend,
					     runner->priv->cached_filter,
					     runner->priv->cached_search);
	} else if (runner->priv->role == PK_ROLE_ENUM_SEARCH_NAME) {
		desc->search_name (runner->priv->backend,
					    runner->priv->cached_filter,
					    runner->priv->cached_search);
	} else if (runner->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGE) {
		desc->install_package (runner->priv->backend,
						runner->priv->cached_package_id);
	} else if (runner->priv->role == PK_ROLE_ENUM_INSTALL_FILE) {
		desc->install_file (runner->priv->backend,
					     runner->priv->cached_full_path);
	} else if (runner->priv->role == PK_ROLE_ENUM_SERVICE_PACK) {
		desc->service_pack (runner->priv->backend,
				    runner->priv->cached_full_path);
	} else if (runner->priv->role == PK_ROLE_ENUM_REFRESH_CACHE) {
		desc->refresh_cache (runner->priv->backend,
					      runner->priv->cached_force);
	} else if (runner->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGE) {
		desc->remove_package (runner->priv->backend,
					       runner->priv->cached_package_id,
					       runner->priv->cached_allow_deps,
					       runner->priv->cached_autoremove);
	} else if (runner->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGE) {
		desc->update_package (runner->priv->backend,
					       runner->priv->cached_package_id);
	} else if (runner->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		desc->update_system (runner->priv->backend);
	} else if (runner->priv->role == PK_ROLE_ENUM_GET_REPO_LIST) {
		desc->get_repo_list (runner->priv->backend);
	} else if (runner->priv->role == PK_ROLE_ENUM_REPO_ENABLE) {
		desc->repo_enable (runner->priv->backend, runner->priv->cached_repo_id,
					    runner->priv->cached_enabled);
	} else if (runner->priv->role == PK_ROLE_ENUM_REPO_SET_DATA) {
		desc->repo_set_data (runner->priv->backend, runner->priv->cached_repo_id,
					      runner->priv->cached_parameter,
					      runner->priv->cached_value);
	} else {
		pk_error ("failed to run as role not assigned");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_runner_run:
 */
gboolean
pk_runner_run (PkRunner *runner)
{
	gboolean ret;
	g_return_val_if_fail (runner != NULL, FALSE);

	ret = pk_runner_set_running (runner);
	if (ret == TRUE) {
		/* we start inhibited, it's up to the backed to
		 * release early if a shutdown is possible */
		pk_inhibit_add (runner->priv->inhibit, runner);
	}
	return ret;
}

/**
 * pk_runner_get_depends:
 */
gboolean
pk_runner_get_depends (PkRunner *runner, const gchar *package_id, gboolean recursive)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->get_depends == NULL) {
		pk_debug ("Not implemented yet: GetDepends");
		return FALSE;
	}
	runner->priv->cached_package_id = g_strdup (package_id);
	runner->priv->cached_force = recursive;
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_GET_DEPENDS);
	return TRUE;
}

/**
 * pk_runner_get_update_detail:
 */
gboolean
pk_runner_get_update_detail (PkRunner *runner, const gchar *package_id)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->get_update_detail == NULL) {
		pk_debug ("Not implemented yet: GetUpdateDetail");
		return FALSE;
	}
	runner->priv->cached_package_id = g_strdup (package_id);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_GET_UPDATE_DETAIL);
	return TRUE;
}

/**
 * pk_runner_get_description:
 */
gboolean
pk_runner_get_description (PkRunner *runner, const gchar *package_id)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->get_description == NULL) {
		pk_debug ("Not implemented yet: GetDescription");
		return FALSE;
	}
	runner->priv->cached_package_id = g_strdup (package_id);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_GET_DESCRIPTION);
	return TRUE;
}

/**
 * pk_runner_get_files:
 */
gboolean
pk_runner_get_files (PkRunner *runner, const gchar *package_id)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->get_files == NULL) {
		pk_debug ("Not implemented yet: GetFiles");
		return FALSE;
	}
	runner->priv->cached_package_id = g_strdup (package_id);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_GET_FILES);
	return TRUE;
}

/**
 * pk_runner_get_requires:
 */
gboolean
pk_runner_get_requires (PkRunner *runner, const gchar *package_id, gboolean recursive)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->get_requires == NULL) {
		pk_debug ("Not implemented yet: GetRequires");
		return FALSE;
	}
	runner->priv->cached_package_id = g_strdup (package_id);
	runner->priv->cached_force = recursive;
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_GET_REQUIRES);
	return TRUE;
}

/**
 * pk_runner_get_updates:
 */
gboolean
pk_runner_get_updates (PkRunner *runner)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->get_updates == NULL) {
		pk_debug ("Not implemented yet: GetUpdates");
		return FALSE;
	}
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_GET_UPDATES);
	return TRUE;
}

/**
 * pk_runner_install_package:
 */
gboolean
pk_runner_install_package (PkRunner *runner, const gchar *package_id)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->install_package == NULL) {
		pk_debug ("Not implemented yet: InstallPackage");
		return FALSE;
	}
	runner->priv->cached_package_id = g_strdup (package_id);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_INSTALL_PACKAGE);
	return TRUE;
}

/**
 * pk_runner_install_file:
 */
gboolean
pk_runner_install_file (PkRunner *runner, const gchar *full_path)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->install_file == NULL) {
		pk_debug ("Not implemented yet: InstallFile");
		return FALSE;
	}
	runner->priv->cached_full_path = g_strdup (full_path);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_INSTALL_FILE);
	return TRUE;
}

/**
 * pk_runner_service_pack:
 */
gboolean
pk_runner_service_pack (PkRunner *runner, const gchar *location)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->service_pack == NULL) {
		pk_debug ("Not implemented yet: ServicePack");
		return FALSE;
	}
	runner->priv->cached_full_path = g_strdup (location);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_SERVICE_PACK);
	return TRUE;
}

/**
 * pk_runner_refresh_cache:
 */
gboolean
pk_runner_refresh_cache (PkRunner *runner, gboolean force)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->refresh_cache == NULL) {
		pk_debug ("Not implemented yet: RefreshCache");
		return FALSE;
	}
	runner->priv->cached_force = force;
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_REFRESH_CACHE);
	return TRUE;
}

/**
 * pk_runner_remove_package:
 */
gboolean
pk_runner_remove_package (PkRunner *runner, const gchar *package_id, gboolean allow_deps, gboolean autoremove)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->remove_package == NULL) {
		pk_debug ("Not implemented yet: RemovePackage");
		return FALSE;
	}
	runner->priv->cached_allow_deps = allow_deps;
	runner->priv->cached_package_id = g_strdup (package_id);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_REMOVE_PACKAGE);
	return TRUE;
}

/**
 * pk_runner_resolve:
 */
gboolean
pk_runner_resolve (PkRunner *runner, const gchar *filter, const gchar *package)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->resolve == NULL) {
		pk_debug ("Not implemented yet: Resolve");
		return FALSE;
	}
	runner->priv->cached_package_id = g_strdup (package);
	runner->priv->cached_filter = g_strdup (filter);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_RESOLVE);
	return TRUE;
}

/**
 * pk_runner_rollback:
 */
gboolean
pk_runner_rollback (PkRunner *runner, const gchar *transaction_id)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->rollback == NULL) {
		pk_debug ("Not implemented yet: Rollback");
		return FALSE;
	}
	runner->priv->cached_transaction_id = g_strdup (transaction_id);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_ROLLBACK);
	return TRUE;
}

/**
 * pk_runner_search_details:
 */
gboolean
pk_runner_search_details (PkRunner *runner, const gchar *filter, const gchar *search)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->search_details == NULL) {
		pk_debug ("Not implemented yet: SearchDetails");
		return FALSE;
	}
	runner->priv->cached_filter = g_strdup (filter);
	runner->priv->cached_search = g_strdup (search);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_SEARCH_DETAILS);
	return TRUE;
}

/**
 * pk_runner_search_file:
 */
gboolean
pk_runner_search_file (PkRunner *runner, const gchar *filter, const gchar *search)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->search_file == NULL) {
		pk_debug ("Not implemented yet: SearchFile");
		return FALSE;
	}
	runner->priv->cached_filter = g_strdup (filter);
	runner->priv->cached_search = g_strdup (search);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_SEARCH_FILE);
	return TRUE;
}

/**
 * pk_runner_search_group:
 */
gboolean
pk_runner_search_group (PkRunner *runner, const gchar *filter, const gchar *search)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->search_group == NULL) {
		pk_debug ("Not implemented yet: SearchGroup");
		return FALSE;
	}
	runner->priv->cached_filter = g_strdup (filter);
	runner->priv->cached_search = g_strdup (search);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_SEARCH_GROUP);
	return TRUE;
}

/**
 * pk_runner_search_name:
 */
gboolean
pk_runner_search_name (PkRunner *runner, const gchar *filter, const gchar *search)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->search_name == NULL) {
		pk_debug ("Not implemented yet: SearchName");
		return FALSE;
	}
	runner->priv->cached_filter = g_strdup (filter);
	runner->priv->cached_search = g_strdup (search);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_SEARCH_NAME);
	return TRUE;
}

/**
 * pk_runner_update_package:
 */
gboolean
pk_runner_update_package (PkRunner *runner, const gchar *package_id)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->update_package == NULL) {
		pk_debug ("Not implemented yet: UpdatePackage");
		return FALSE;
	}
	runner->priv->cached_package_id = g_strdup (package_id);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_UPDATE_PACKAGE);
	return TRUE;
}

/**
 * pk_runner_update_system:
 */
gboolean
pk_runner_update_system (PkRunner *runner)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->update_system == NULL) {
		pk_debug ("Not implemented yet: UpdateSystem");
		return FALSE;
	}
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_UPDATE_SYSTEM);
	return TRUE;
}

/**
 * pk_runner_get_repo_list:
 */
gboolean
pk_runner_get_repo_list (PkRunner *runner)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->get_repo_list == NULL) {
		pk_debug ("Not implemented yet: GetRepoList");
		return FALSE;
	}
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_GET_REPO_LIST);
	return TRUE;
}

/**
 * pk_runner_repo_enable:
 */
gboolean
pk_runner_repo_enable (PkRunner *runner, const gchar	*repo_id, gboolean enabled)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->repo_enable == NULL) {
		pk_debug ("Not implemented yet: RepoEnable");
		return FALSE;
	}
	runner->priv->cached_repo_id = g_strdup (repo_id);
	runner->priv->cached_enabled = enabled;
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_REPO_ENABLE);
	return TRUE;
}

/**
 * pk_runner_repo_set_data:
 */
gboolean
pk_runner_repo_set_data (PkRunner *runner, const gchar *repo_id, const gchar *parameter, const gchar *value)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->backend->desc->repo_set_data == NULL) {
		pk_debug ("Not implemented yet: RepoSetData");
		return FALSE;
	}
	runner->priv->cached_repo_id = g_strdup (repo_id);
	runner->priv->cached_parameter = g_strdup (parameter);
	runner->priv->cached_value = g_strdup (value);
	runner->priv->status = PK_STATUS_ENUM_WAIT;
	pk_runner_set_role (runner, PK_ROLE_ENUM_REPO_SET_DATA);
	return TRUE;
}

/**
 * pk_runner_get_actions:
 *
 * You need to g_object_unref the returned object
 */
PkEnumList *
pk_runner_get_actions (PkRunner *runner)
{
	PkEnumList *elist;
	PkBackendDesc *desc;

	g_return_val_if_fail (runner != NULL, NULL);

	/* lets reduce pointer dereferences... */
	desc = runner->priv->backend->desc;

	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_ROLE);
	if (desc->cancel != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_CANCEL);
	}
	if (desc->get_depends != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_DEPENDS);
	}
	if (desc->get_description != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_DESCRIPTION);
	}
	if (desc->get_files != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_FILES);
	}
	if (desc->get_requires != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_REQUIRES);
	}
	if (desc->get_updates != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_UPDATES);
	}
	if (desc->get_update_detail != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_UPDATE_DETAIL);
	}
	if (desc->install_package != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_INSTALL_PACKAGE);
	}
	if (desc->install_file != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_INSTALL_FILE);
	}
	if (desc->refresh_cache != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_REFRESH_CACHE);
	}
	if (desc->remove_package != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_REMOVE_PACKAGE);
	}
	if (desc->resolve != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_RESOLVE);
	}
	if (desc->rollback != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_ROLLBACK);
	}
	if (desc->search_details != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_DETAILS);
	}
	if (desc->search_file != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_FILE);
	}
	if (desc->search_group != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_GROUP);
	}
	if (desc->search_name != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_NAME);
	}
	if (desc->update_package != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_UPDATE_PACKAGE);
	}
	if (desc->update_system != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_UPDATE_SYSTEM);
	}
	if (desc->get_repo_list != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_REPO_LIST);
	}
	if (desc->repo_enable != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_REPO_ENABLE);
	}
	if (desc->repo_set_data != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_REPO_SET_DATA);
	}
	return elist;
}

/**
 * pk_runner_get_groups:
 *
 * You need to g_object_unref the returned object
 */
PkEnumList *
pk_runner_get_groups (PkRunner *runner)
{
	PkEnumList *elist;

	g_return_val_if_fail (runner != NULL, NULL);

	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_GROUP);
	if (runner->priv->backend->desc->get_groups != NULL) {
		runner->priv->backend->desc->get_groups (runner->priv->backend, elist);
	}
	return elist;
}

/**
 * pk_runner_get_filters:
 *
 * You need to g_object_unref the returned object
 */
PkEnumList *
pk_runner_get_filters (PkRunner *runner)
{
	PkEnumList *elist;

	g_return_val_if_fail (runner != NULL, NULL);

	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_FILTER);
	if (runner->priv->backend->desc->get_filters != NULL) {
		runner->priv->backend->desc->get_filters (runner->priv->backend, elist);
	}
	return elist;
}

/**
 * pk_runner_get_runtime:
 *
 * Returns time running in ms
 */
guint
pk_runner_get_runtime (PkRunner *runner)
{
	g_return_val_if_fail (runner != NULL, 0);
	return pk_backend_get_runtime (runner->priv->backend);
}

/**
 * pk_runner_network_is_online:
 */
gboolean
pk_runner_network_is_online (PkRunner *runner)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	return pk_network_is_online (runner->priv->network);
}

/**
 * pk_runner_set_dbus_name:
 */
gboolean
pk_runner_set_dbus_name (PkRunner *runner, const gchar *dbus_name)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	if (runner->priv->dbus_name != NULL) {
		pk_warning ("you can't assign more than once!");
		return FALSE;
	}
	runner->priv->dbus_name = g_strdup (dbus_name);
	pk_debug ("assiging %s to %p", dbus_name, runner);
	libgbus_assign (runner->priv->libgbus, LIBGBUS_SYSTEM, dbus_name);
	return TRUE;
}

/**
 * pk_runner_is_caller_active:
 */
gboolean
pk_runner_is_caller_active (PkRunner *runner, gboolean *is_active)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	*is_active = libgbus_is_connected (runner->priv->libgbus);
	return TRUE;
}

/**
 * pk_runner_connection_changed_cb:
 **/
static void
pk_runner_connection_changed_cb (LibGBus *libgbus, gboolean connected, PkRunner *runner)
{
	g_return_if_fail (runner != NULL);
	g_return_if_fail (PK_IS_RUNNER (runner));
	if (connected == FALSE) {
		pk_debug ("client disconnected....");
		g_signal_emit (runner, signals [PK_RUNNER_CALLER_ACTIVE_CHANGED], 0, FALSE);
	}
}

/**
 * pk_runner_tid_valid:
 **/
static gboolean
pk_runner_tid_valid (PkRunner *runner)
{
	const gchar *c_tid;
	gboolean valid;

	g_return_val_if_fail (runner != NULL, FALSE);
	g_return_val_if_fail (PK_IS_RUNNER (runner), FALSE);

	/* get currently running */
	c_tid = pk_backend_get_current_tid (runner->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid");
		return FALSE;
	}

	/* have we already been marked as finished? */
	if (runner->priv->finished == TRUE) {
		pk_debug ("Already finished, so it can't be us");
		return FALSE;
	}

	/* the same? */
	valid = pk_strequal (runner->priv->tid, c_tid);
	if (valid == FALSE) {
		pk_debug ("ignoring %s as %s", runner->priv->tid, c_tid);
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_runner_package_cb:
 **/
static void
pk_runner_package_cb (PkBackend *backend, PkInfoEnum info, const gchar *package_id, const gchar *summary, PkRunner *runner)
{
	PkRoleEnum role;
	const gchar *info_text;
	gboolean valid;

	g_return_if_fail (runner != NULL);
	g_return_if_fail (PK_IS_RUNNER (runner));

	/* are we still talking about the same backend instance */
	valid = pk_runner_tid_valid (runner);
	if (valid == FALSE) {
		return;
	}

	/* check the backend is doing the right thing */
	role = pk_runner_get_role (runner);
	if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_INSTALL_PACKAGE ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGE) {
		if (info == PK_INFO_ENUM_INSTALLED) {
			pk_backend_message (runner->priv->backend, PK_MESSAGE_ENUM_DAEMON,
					    "backend emitted 'installed' rather than 'installing' "
					    "- you need to do the package *before* you do the action");
			return;
		}
	}

	/* add to package cache even if we already got a result */
	pk_package_list_add (runner->priv->package_list, info, package_id, summary);

	info_text = pk_info_enum_to_text (info);
	pk_debug ("caching package info=%s %s, %s", info_text, package_id, summary);
}

/**
 * pk_runner_finished_cb:
 **/
static void
pk_runner_finished_cb (PkBackend *backend, PkExitEnum exit, PkRunner *runner)
{
	gboolean valid;

	g_return_if_fail (runner != NULL);
	g_return_if_fail (PK_IS_RUNNER (runner));

	/* are we still talking about the same backend instance */
	valid = pk_runner_tid_valid (runner);
	if (valid == FALSE) {
		return;
	}

	/* we should get no more from the backend with this tid */
	runner->priv->finished = TRUE;
}

/**
 * pk_runner_status_changed_cb:
 **/
static void
pk_runner_status_changed_cb (PkBackend *backend, PkStatusEnum status, PkRunner *runner)
{
	gboolean valid;

	g_return_if_fail (runner != NULL);
	g_return_if_fail (PK_IS_RUNNER (runner));

	/* are we still talking about the same backend instance */
	valid = pk_runner_tid_valid (runner);
	if (valid == FALSE) {
		return;
	}

	/* what we are interested in */
	runner->priv->status = status;
}

/**
 * pk_runner_allow_cancel_cb:
 **/
static void
pk_runner_allow_cancel_cb (PkBackend *backend, gboolean allow_cancel, PkRunner *runner)
{
	g_return_if_fail (runner != NULL);
	g_return_if_fail (PK_IS_RUNNER (runner));
	g_return_if_fail (runner->priv->backend->desc->cancel != NULL);

	pk_debug ("AllowCancel now %i", allow_cancel);
	runner->priv->allow_cancel = allow_cancel;
}

/**
 * pk_runner_get_tid:
 */
const gchar *
pk_runner_get_tid (PkRunner *runner)
{
	g_return_val_if_fail (runner != NULL, NULL);
	g_return_val_if_fail (PK_IS_RUNNER (runner), NULL);
	return runner->priv->tid;
}

/**
 * pk_runner_set_tid:
 */
gboolean
pk_runner_set_tid (PkRunner *runner, const gchar *tid)
{
	g_return_val_if_fail (runner != NULL, FALSE);
	g_return_val_if_fail (PK_IS_RUNNER (runner), FALSE);

	if (runner->priv->tid != NULL) {
		pk_warning ("changing a tid -- why?");
	}
	g_free (runner->priv->tid);
	runner->priv->tid = g_strdup (tid);
	return TRUE;
}

/**
 * pk_runner_finalize:
 **/
static void
pk_runner_finalize (GObject *object)
{
	PkRunner *runner;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_RUNNER (object));

	runner = PK_RUNNER (object);

	/* housekeeping */
	g_signal_handler_disconnect (runner->priv->backend, runner->priv->signal_package);
	g_signal_handler_disconnect (runner->priv->backend, runner->priv->signal_finished);
	g_signal_handler_disconnect (runner->priv->backend, runner->priv->signal_status);
	g_signal_handler_disconnect (runner->priv->backend, runner->priv->signal_allow_cancel);

	g_free (runner->priv->last_package);
	g_free (runner->priv->dbus_name);
	g_free (runner->priv->cached_package_id);
	g_free (runner->priv->cached_transaction_id);
	g_free (runner->priv->cached_filter);
	g_free (runner->priv->cached_search);
	g_free (runner->priv->cached_repo_id);
	g_free (runner->priv->cached_parameter);
	g_free (runner->priv->cached_value);
	g_free (runner->priv->tid);

	/* remove any inhibit, it's okay to call this function when it's not needed */
	pk_inhibit_remove (runner->priv->inhibit, runner);
	g_object_unref (runner->priv->inhibit);
	g_object_unref (runner->priv->backend);
	g_object_unref (runner->priv->libgbus);

	g_object_unref (runner->priv->network);
	g_object_unref (runner->priv->thread_list);
	g_object_unref (runner->priv->package_list);

	G_OBJECT_CLASS (pk_runner_parent_class)->finalize (object);
}

/**
 * pk_runner_class_init:
 **/
static void
pk_runner_class_init (PkRunnerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_runner_finalize;

	signals [PK_RUNNER_CALLER_ACTIVE_CHANGED] =
		g_signal_new ("caller-active-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (PkRunnerPrivate));
}

/**
 * pk_runner_init:
 **/
static void
pk_runner_init (PkRunner *runner)
{
	runner->priv = PK_RUNNER_GET_PRIVATE (runner);
	runner->priv->finished = FALSE;
	runner->priv->allow_cancel = FALSE;
	runner->priv->dbus_name = NULL;
	runner->priv->cached_enabled = FALSE;
	runner->priv->cached_package_id = NULL;
	runner->priv->cached_transaction_id = NULL;
	runner->priv->cached_full_path = NULL;
	runner->priv->cached_filter = NULL;
	runner->priv->cached_search = NULL;
	runner->priv->cached_repo_id = NULL;
	runner->priv->cached_parameter = NULL;
	runner->priv->cached_value = NULL;
	runner->priv->last_package = NULL;
	runner->priv->tid = NULL;
	runner->priv->role = PK_ROLE_ENUM_UNKNOWN;

	runner->priv->backend = pk_backend_new ();
	runner->priv->signal_package =
		g_signal_connect (runner->priv->backend, "package",
			  G_CALLBACK (pk_runner_package_cb), runner);
	runner->priv->signal_finished =
		g_signal_connect (runner->priv->backend, "finished",
			  G_CALLBACK (pk_runner_finished_cb), runner);
	runner->priv->signal_status =
		g_signal_connect (runner->priv->backend, "status-changed",
			  G_CALLBACK (pk_runner_status_changed_cb), runner);
	runner->priv->signal_allow_cancel =
		g_signal_connect (runner->priv->backend, "allow-cancel",
			  G_CALLBACK (pk_runner_allow_cancel_cb), runner);

	runner->priv->inhibit = pk_inhibit_new ();
	runner->priv->network = pk_network_new ();
	runner->priv->thread_list = pk_thread_list_new ();
	runner->priv->package_list = pk_package_list_new ();

	runner->priv->libgbus = libgbus_new ();
	g_signal_connect (runner->priv->libgbus, "connection-changed",
			  G_CALLBACK (pk_runner_connection_changed_cb), runner);
}

/**
 * pk_runner_new:
 **/
PkRunner *
pk_runner_new (void)
{
	PkRunner *runner;
	runner = g_object_new (PK_TYPE_RUNNER, NULL);
	return PK_RUNNER (runner);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_runner (LibSelfTest *test)
{
	PkRunner *runner;
	GTimer *timer;

	timer = g_timer_new ();

	if (libst_start (test, "PkRunner", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get a runner");
	runner = pk_runner_new ();
	if (runner != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	g_timer_destroy (timer);
	g_object_unref (runner);

	libst_end (test);
}
#endif

