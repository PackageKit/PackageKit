/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
#include <string.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <gmodule.h>
#include <glib/gprintf.h>
#include <pk-network.h>

#include "pk-package-item.h"
#include "pk-debug.h"
#include "pk-common.h"
#include "pk-marshal.h"
#include "pk-backend-internal.h"
#include "pk-backend.h"
#include "pk-time.h"
#include "pk-file-monitor.h"

#define PK_BACKEND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND, PkBackendPrivate))

/**
 * PK_BACKEND_PERCENTAGE_DEFAULT:
 *
 * The default percentage value, should never be emitted, but should be
 * used so we can work out if a backend just calls NoPercentageUpdates
 */
#define PK_BACKEND_PERCENTAGE_DEFAULT		102

/**
 * PK_BACKEND_FINISHED_ERROR_TIMEOUT:
 *
 * The time in ms the backend has to call Finished() after ErrorCode()
 * If backends do not do this, they will be Finished() manually,
 * and a Message() will be sent to warn the developer
 */
#define PK_BACKEND_FINISHED_ERROR_TIMEOUT	500 /* ms */

/**
 * PK_BACKEND_FINISHED_TIMEOUT_GRACE:
 *
 * The time in ms the backend waits after receiving Finished() before
 * propagating the signal to the other components.
 * This delay is required as some threads may take some time to cancel or a
 * spawned executable to disappear of the system DBUS.
 */
#define PK_BACKEND_FINISHED_TIMEOUT_GRACE	50 /* ms */

struct _PkBackendPrivate
{
	GModule			*handle;
	PkTime			*time;
	GHashTable		*eulas;
	gchar			*name;
	gchar			*c_tid;
	gchar			*proxy_http;
	gchar			*proxy_ftp;
	gboolean		 locked;
	gboolean		 set_error;
	gboolean		 set_signature;
	gboolean		 set_eula;
	gboolean		 has_sent_package;
	PkNetwork		*network;
	PkPackageItem		*last_package;
	PkRoleEnum		 role; /* this never changes for the lifetime of a transaction */
	PkStatusEnum		 status; /* this changes */
	PkExitEnum		 exit;
	PkFileMonitor		*file_monitor;
	PkBackendFileChanged	 file_changed_func;
	gpointer		 file_changed_data;
	gboolean		 during_initialize;
	gboolean		 allow_cancel;
	gboolean		 finished;
	guint			 last_percentage;
	guint			 last_subpercentage;
	guint			 last_remaining;
	guint			 signal_finished;
	guint			 signal_error_timeout;
	GThread			*thread;
	GHashTable		*hash_string;
	GHashTable		*hash_strv;
	GHashTable		*hash_pointer;
	GHashTable		*hash_array;
};

G_DEFINE_TYPE (PkBackend, pk_backend, G_TYPE_OBJECT)
static gpointer pk_backend_object = NULL;

enum {
	PK_BACKEND_STATUS_CHANGED,
	PK_BACKEND_PROGRESS_CHANGED,
	PK_BACKEND_DETAILS,
	PK_BACKEND_FILES,
	PK_BACKEND_PACKAGE,
	PK_BACKEND_UPDATE_DETAIL,
	PK_BACKEND_ERROR_CODE,
	PK_BACKEND_REPO_SIGNATURE_REQUIRED,
	PK_BACKEND_EULA_REQUIRED,
	PK_BACKEND_REQUIRE_RESTART,
	PK_BACKEND_MESSAGE,
	PK_BACKEND_CHANGE_TRANSACTION_DATA,
	PK_BACKEND_FINISHED,
	PK_BACKEND_ALLOW_CANCEL,
	PK_BACKEND_REPO_DETAIL,
	PK_BACKEND_LAST_SIGNAL
};

static guint signals [PK_BACKEND_LAST_SIGNAL] = { 0 };

/**
 * pk_backend_get_groups:
 **/
PkGroupEnum
pk_backend_get_groups (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_GROUP_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->locked != FALSE, PK_GROUP_ENUM_UNKNOWN);

	/* not compulsory */
	if (backend->desc->get_groups == NULL) {
		return PK_GROUP_ENUM_UNKNOWN;
	}
	return backend->desc->get_groups (backend);
}

/**
 * pk_backend_get_filters:
 **/
PkFilterEnum
pk_backend_get_filters (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_FILTER_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->locked != FALSE, PK_FILTER_ENUM_UNKNOWN);

	/* not compulsory */
	if (backend->desc->get_filters == NULL) {
		return PK_FILTER_ENUM_UNKNOWN;
	}
	return backend->desc->get_filters (backend);
}

/**
 * pk_backend_get_actions:
 **/
PkRoleEnum
pk_backend_get_actions (PkBackend *backend)
{
	PkRoleEnum roles = 0;
	PkBackendDesc *desc;

	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_ROLE_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->locked != FALSE, PK_ROLE_ENUM_UNKNOWN);

	/* lets reduce pointer dereferences... */
	desc = backend->desc;
	if (desc->cancel != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_CANCEL);
	}
	if (desc->get_depends != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_GET_DEPENDS);
	}
	if (desc->get_details != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_GET_DETAILS);
	}
	if (desc->get_files != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_GET_FILES);
	}
	if (desc->get_requires != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_GET_REQUIRES);
	}
	if (desc->get_packages != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_GET_PACKAGES);
	}
	if (desc->what_provides != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_WHAT_PROVIDES);
	}
	if (desc->get_updates != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_GET_UPDATES);
	}
	if (desc->get_update_detail != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_GET_UPDATE_DETAIL);
	}
	if (desc->install_packages != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_INSTALL_PACKAGES);
	}
	if (desc->install_files != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_INSTALL_FILES);
	}
	if (desc->refresh_cache != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_REFRESH_CACHE);
	}
	if (desc->remove_packages != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_REMOVE_PACKAGES);
	}
	if (desc->resolve != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_RESOLVE);
	}
	if (desc->rollback != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_ROLLBACK);
	}
	if (desc->search_details != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_SEARCH_DETAILS);
	}
	if (desc->search_file != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_SEARCH_FILE);
	}
	if (desc->search_group != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_SEARCH_GROUP);
	}
	if (desc->search_name != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_SEARCH_NAME);
	}
	if (desc->update_packages != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_UPDATE_PACKAGES);
	}
	if (desc->update_system != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_UPDATE_SYSTEM);
	}
	if (desc->get_repo_list != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_GET_REPO_LIST);
	}
	if (desc->repo_enable != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_REPO_ENABLE);
	}
	if (desc->repo_set_data != NULL) {
		pk_enums_add (roles, PK_ROLE_ENUM_REPO_SET_DATA);
	}
	return roles;
}

/**
 * pk_backend_set_string:
 **/
gboolean
pk_backend_set_string (PkBackend *backend, const gchar *key, const gchar *data)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* valid, but do nothing */
	if (data == NULL) {
		return FALSE;
	}

	/* does already exist? */
	value = g_hash_table_lookup (backend->priv->hash_string, (gpointer) key);
	if (value != NULL) {
		pk_warning ("already set data for %s", key);
		return FALSE;
	}
	pk_debug ("saving '%s' for %s", data, key);
	g_hash_table_insert (backend->priv->hash_string, g_strdup (key), (gpointer) g_strdup (data));
	return TRUE;
}

/**
 * pk_backend_set_strv:
 **/
gboolean
pk_backend_set_strv (PkBackend *backend, const gchar *key, gchar **data)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* valid, but do nothing */
	if (data == NULL) {
		return FALSE;
	}

	/* does already exist? */
	value = g_hash_table_lookup (backend->priv->hash_strv, (gpointer) key);
	if (value != NULL) {
		pk_warning ("already set data for %s", key);
		return FALSE;
	}
	pk_debug ("saving %p for %s", data, key);
	g_hash_table_insert (backend->priv->hash_strv, g_strdup (key), (gpointer) g_strdupv (data));
	return TRUE;
}

/**
 * pk_backend_set_array:
 **/
gboolean
pk_backend_set_array (PkBackend *backend, const gchar *key, GPtrArray *data)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* valid, but do nothing */
	if (data == NULL) {
		return FALSE;
	}

	/* does already exist? */
	value = g_hash_table_lookup (backend->priv->hash_array, (gpointer) key);
	if (value != NULL) {
		pk_warning ("already set data for %s", key);
		return FALSE;
	}
	pk_debug ("saving %p for %s", data, key);
	g_hash_table_insert (backend->priv->hash_array, g_strdup (key), (gpointer) data);
	return TRUE;
}

/**
 * pk_backend_set_uint:
 **/
gboolean
pk_backend_set_uint (PkBackend *backend, const gchar *key, guint data)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* does already exist? */
	value = g_hash_table_lookup (backend->priv->hash_pointer, (gpointer) key);
	if (value != NULL) {
		pk_warning ("already set data for %s", key);
		return FALSE;
	}
	pk_debug ("saving %i for %s", data, key);
	g_hash_table_insert (backend->priv->hash_pointer, g_strdup (key), GINT_TO_POINTER (data+1));
	return TRUE;
}

/**
 * pk_backend_set_bool:
 **/
gboolean
pk_backend_set_bool (PkBackend *backend, const gchar *key, gboolean data)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* does already exist? */
	value = g_hash_table_lookup (backend->priv->hash_pointer, (gpointer) key);
	if (value != NULL) {
		pk_warning ("already set data for %s", key);
		return FALSE;
	}
	pk_debug ("saving %i for %s", data, key);
	g_hash_table_insert (backend->priv->hash_pointer, g_strdup (key), GINT_TO_POINTER (data+1));
	return TRUE;
}

/**
 * pk_backend_set_pointer:
 **/
gboolean
pk_backend_set_pointer (PkBackend *backend, const gchar *key, gpointer data)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	/* does already exist? */
	value = g_hash_table_lookup (backend->priv->hash_pointer, (gpointer) key);
	if (value != NULL) {
		pk_warning ("already set data for %s", key);
		return FALSE;
	}
	pk_debug ("saving %p for %s", data, key);
	g_hash_table_insert (backend->priv->hash_pointer, g_strdup (key), data+1);
	return TRUE;
}

/**
 * pk_backend_get_string:
 **/
const gchar *
pk_backend_get_string (PkBackend *backend, const gchar *key)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* does already exist? */
	value = g_hash_table_lookup (backend->priv->hash_string, (gpointer) key);
	if (value == NULL) {
		pk_warning ("not set data for %s", key);
		return NULL;
	}
	return (const gchar *) value;
}

/**
 * pk_backend_get_strv:
 **/
gchar **
pk_backend_get_strv (PkBackend *backend, const gchar *key)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* does already exist? */
	value = g_hash_table_lookup (backend->priv->hash_strv, (gpointer) key);
	if (value == NULL) {
		pk_warning ("not set data for %s", key);
		return NULL;
	}
	return (gchar **) value;
}

/**
 * pk_backend_get_array:
 **/
GPtrArray *
pk_backend_get_array (PkBackend *backend, const gchar *key)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* does already exist? */
	value = g_hash_table_lookup (backend->priv->hash_array, (gpointer) key);
	if (value == NULL) {
		pk_warning ("not set data for %s", key);
		return NULL;
	}
	return (GPtrArray *) value;
}

/**
 * pk_backend_get_uint:
 **/
uint
pk_backend_get_uint (PkBackend *backend, const gchar *key)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_BACKEND (backend), 0);
	g_return_val_if_fail (key != NULL, 0);

	/* does already exist? */
	value = g_hash_table_lookup (backend->priv->hash_pointer, (gpointer) key);
	if (value == NULL) {
		pk_warning ("not set data for %s", key);
		return FALSE;
	}
	/* we do the +1/-1 as NULL also means missing in the hash table */
	return GPOINTER_TO_INT (value)-1;
}

/**
 * pk_backend_get_bool:
 **/
gboolean
pk_backend_get_bool (PkBackend *backend, const gchar *key)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* does already exist? */
	value = g_hash_table_lookup (backend->priv->hash_pointer, (gpointer) key);
	if (value == NULL) {
		pk_warning ("not set data for %s", key);
		return FALSE;
	}
	return GPOINTER_TO_INT (value)-1;
}

/**
 * pk_backend_get_pointer:
 **/
gpointer
pk_backend_get_pointer (PkBackend *backend, const gchar *key)
{
	gpointer value;
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* does already exist? */
	value = g_hash_table_lookup (backend->priv->hash_pointer, (gpointer) key);
	if (value == NULL) {
		pk_warning ("not set data for %s", key);
		return FALSE;
	}
	return value-1;
}

/**
 * pk_backend_build_library_path:
 **/
static gchar *
pk_backend_build_library_path (PkBackend *backend, const gchar *name)
{
	gchar *path;
	gchar *filename;

	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	filename = g_strdup_printf ("libpk_backend_%s.so", name);
#if PK_BUILD_LOCAL
	/* prefer the local version */
	path = g_build_filename ("..", "backends", name, ".libs", filename, NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS) == FALSE) {
		pk_debug ("local backend not found '%s'", path);
		g_free (path);
		path = g_build_filename (LIBDIR, "packagekit-backend", filename, NULL);
	}
#else
	path = g_build_filename (LIBDIR, "packagekit-backend", filename, NULL);
#endif
	g_free (filename);
	pk_debug ("dlopening '%s'", path);

	return path;
}

/**
 * pk_backend_set_name:
 **/
gboolean
pk_backend_set_name (PkBackend *backend, const gchar *backend_name)
{
	GModule *handle;
	gchar *path = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend_name != NULL, FALSE);

	/* have we already been set? */
	if (backend->priv->name != NULL) {
		pk_warning ("pk_backend_set_name called multiple times");
		ret = FALSE;
		goto out;
	}

	/* can we load it? */
	pk_debug ("Trying to load : %s", backend_name);
	path = pk_backend_build_library_path (backend, backend_name);
	handle = g_module_open (path, 0);
	if (handle == NULL) {
		pk_warning ("opening module %s failed : %s", backend_name, g_module_error ());
		ret = FALSE;
		goto out;
	}

	/* is is correctly formed? */
	if (!g_module_symbol (handle, "pk_backend_desc", (gpointer) &backend->desc)) {
		g_module_close (handle);
		pk_warning ("could not find description in plugin %s, not loading", backend_name);
		ret = FALSE;
		goto out;
	}

	/* save the backend name and handle */
	g_free (backend->priv->name);
	backend->priv->name = g_strdup (backend_name);
	backend->priv->handle = handle;

out:
	g_free (path);
	return ret;
}

/**
 * pk_backend_set_proxy:
 **/
gboolean
pk_backend_set_proxy (PkBackend	*backend, const gchar *proxy_http, const gchar *proxy_ftp)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_free (backend->priv->proxy_http);
	g_free (backend->priv->proxy_ftp);
	backend->priv->proxy_http = g_strdup (proxy_http);
	backend->priv->proxy_ftp = g_strdup (proxy_ftp);
	return TRUE;
}

/**
 * pk_backend_get_proxy_http:
 *
 * Return value: proxy string in the form username:password@server:port
 **/
gchar *
pk_backend_get_proxy_http (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return g_strdup (backend->priv->proxy_http);
}

/**
 * pk_backend_get_proxy_ftp:
 *
 * Return value: proxy string in the form username:password@server:port
 **/
gchar *
pk_backend_get_proxy_ftp (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return g_strdup (backend->priv->proxy_ftp);
}

/**
 * pk_backend_lock:
 *
 * Responsible for initialising the external backend object.
 *
 * Typically this will involve taking database locks for exclusive package access.
 * This method should only be called from the engine, unless the backend object
 * is used in self-check code, in which case the lock and unlock will have to
 * be done manually.
 **/
gboolean
pk_backend_lock (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->desc != NULL, FALSE);

	if (backend->priv->locked) {
		pk_warning ("already locked");
		/* we don't return FALSE here, as the action didn't fail */
		return TRUE;
	}
	if (backend->desc->initialize != NULL) {
		backend->priv->during_initialize = TRUE;
		backend->desc->initialize (backend);
		backend->priv->during_initialize = FALSE;
	}
	backend->priv->locked = TRUE;
	return TRUE;
}

/**
 * pk_backend_unlock:
 *
 * Responsible for finalising the external backend object.
 *
 * Typically this will involve releasing database locks for any other access.
 * This method should only be called from the engine, unless the backend object
 * is used in self-check code, in which case it will have to be done manually.
 **/
gboolean
pk_backend_unlock (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	if (backend->priv->locked == FALSE) {
		pk_warning ("already unlocked");
		/* we don't return FALSE here, as the action didn't fail */
		return TRUE;
	}
	if (backend->desc == NULL) {
		pk_warning ("not yet loaded backend, try pk_backend_lock()");
		return FALSE;
	}
	if (backend->desc->destroy != NULL) {
		backend->desc->destroy (backend);
	}
	backend->priv->locked = FALSE;
	return TRUE;
}

/**
 * pk_backend_get_name:
 **/
gchar *
pk_backend_get_name (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return g_strdup (backend->priv->name);
}

/**
 * pk_backend_emit_progress_changed:
 **/
static gboolean
pk_backend_emit_progress_changed (PkBackend *backend)
{
	guint percentage;
	guint subpercentage;
	guint elapsed;
	guint remaining;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	percentage = backend->priv->last_percentage;

	/* have not ever set any value? */
	if (percentage == PK_BACKEND_PERCENTAGE_DEFAULT) {
		percentage = PK_BACKEND_PERCENTAGE_INVALID;
	}
	subpercentage = backend->priv->last_subpercentage;
	elapsed = pk_time_get_elapsed (backend->priv->time);
	remaining = backend->priv->last_remaining;

	pk_debug ("emit progress %i, %i, %i, %i",
		  percentage, subpercentage, elapsed, remaining);
	g_signal_emit (backend, signals [PK_BACKEND_PROGRESS_CHANGED], 0,
		       percentage, subpercentage, elapsed, remaining);
	return TRUE;
}

/**
 * pk_backend_set_percentage:
 **/
gboolean
pk_backend_set_percentage (PkBackend *backend, guint percentage)
{
	guint remaining;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	/* set the same twice? */
	if (backend->priv->last_percentage == percentage) {
		pk_debug ("duplicate set of %i", percentage);
		return FALSE;
	}

	/* check over */
	if (percentage > PK_BACKEND_PERCENTAGE_INVALID) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_DAEMON,
				    "percentage value is invalid: %i", percentage);
		return FALSE;
	}

	/* check under */
	if (percentage < 100 &&
	    backend->priv->last_percentage < 100 &&
	    percentage < backend->priv->last_percentage) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_DAEMON,
				    "percentage value is going down to %i from %i",
				    percentage, backend->priv->last_percentage);
		return FALSE;
	}

	/* save in case we need this from coldplug */
	backend->priv->last_percentage = percentage;

	/* only compute time if we have data */
	if (percentage != PK_BACKEND_PERCENTAGE_INVALID) {
		/* needed for time remaining calculation */
		pk_time_add_data (backend->priv->time, percentage);

		/* lets try this and print as debug */
		remaining = pk_time_get_remaining (backend->priv->time);
		pk_debug ("this will now take ~%i seconds", remaining);

#ifdef PK_IS_DEVELOPER
		/* Until the predicted time is more sane... */
		backend->priv->last_remaining = remaining;
#endif
	}

	/* emit the progress changed signal */
	pk_backend_emit_progress_changed (backend);
	return TRUE;
}

/**
 * pk_backend_get_runtime:
 *
 * Returns time running in ms
 */
guint
pk_backend_get_runtime (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), 0);
	g_return_val_if_fail (backend->priv->locked != FALSE, 0);
	return pk_time_get_elapsed (backend->priv->time);
}

/**
 * pk_backend_set_sub_percentage:
 **/
gboolean
pk_backend_set_sub_percentage (PkBackend *backend, guint percentage)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	/* set the same twice? */
	if (backend->priv->last_subpercentage == percentage) {
		pk_debug ("duplicate set of %i", percentage);
		return FALSE;
	}

	/* invalid number? */
	if (percentage > 100 && percentage != PK_BACKEND_PERCENTAGE_INVALID) {
		pk_debug ("invalid number %i", percentage);
		return FALSE;
	}

	/* save in case we need this from coldplug */
	backend->priv->last_subpercentage = percentage;

	/* emit the progress changed signal */
	pk_backend_emit_progress_changed (backend);
	return TRUE;
}

/**
 * pk_backend_set_status:
 **/
gboolean
pk_backend_set_status (PkBackend *backend, PkStatusEnum status)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* already this? */
	if (backend->priv->status == status) {
		pk_debug ("already set same status");
		return TRUE;
	}

	/* have we already set an error? */
	if (backend->priv->set_error && status != PK_STATUS_ENUM_FINISHED) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	/* backends don't do this */
	if (status == PK_STATUS_ENUM_WAIT) {
		pk_warning ("backend tried to WAIT, only the runner should set this value");
		pk_backend_message (backend, PK_MESSAGE_ENUM_DAEMON,
				    "backends shouldn't use STATUS_WAIT");
		return FALSE;
	}

	/* sanity check */
	if (status == PK_STATUS_ENUM_SETUP && backend->priv->status != PK_STATUS_ENUM_WAIT) {
		pk_warning ("backend tried to SETUP, but should be in WAIT");
		pk_backend_message (backend, PK_MESSAGE_ENUM_DAEMON,
				    "Tried to SETUP when not in WAIT");
		return FALSE;
	}

	/* do we have to enumate a running call? */
	if (status != PK_STATUS_ENUM_RUNNING && status != PK_STATUS_ENUM_SETUP) {
		if (backend->priv->status == PK_STATUS_ENUM_SETUP) {
			pk_debug ("emiting status-changed running");
			g_signal_emit (backend, signals [PK_BACKEND_STATUS_CHANGED], 0, PK_STATUS_ENUM_RUNNING);
		}
	}

	backend->priv->status = status;

	pk_debug ("emiting status-changed %s", pk_status_enum_to_text (status));
	g_signal_emit (backend, signals [PK_BACKEND_STATUS_CHANGED], 0, status);
	return TRUE;
}

/**
 * pk_backend_get_status:
 **/
PkStatusEnum
pk_backend_get_status (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_STATUS_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->locked != FALSE, PK_STATUS_ENUM_UNKNOWN);
	return backend->priv->status;
}

/**
 * pk_backend_package:
 **/
gboolean
pk_backend_package (PkBackend *backend, PkInfoEnum info, const gchar *package_id, const gchar *summary)
{
	gchar *summary_safe;
	PkPackageItem *item;
	gboolean ret;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* check against the old one */
	item = pk_package_item_new (info, package_id, summary);
	ret = pk_package_item_equal (item, backend->priv->last_package);
	if (ret) {
		pk_package_item_free (item);
		pk_debug ("skipping duplicate %s", package_id);
		return FALSE;
	}
	/* update the 'last' package */
	pk_package_item_free (backend->priv->last_package);
	backend->priv->last_package = pk_package_item_copy (item);
	pk_package_item_free (item);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	/* we automatically set the transaction status for some infos */
	if (info == PK_INFO_ENUM_DOWNLOADING) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	} else if (info == PK_INFO_ENUM_UPDATING) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
	} else if (info == PK_INFO_ENUM_INSTALLING) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	} else if (info == PK_INFO_ENUM_REMOVING) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
	} else if (info == PK_INFO_ENUM_CLEANUP) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_CLEANUP);
	} else if (info == PK_INFO_ENUM_OBSOLETING) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_OBSOLETE);
	}

	/* we've sent a package for this transaction */
	backend->priv->has_sent_package = TRUE;

	/* replace unsafe chars */
	summary_safe = pk_strsafe (summary);

	pk_debug ("emit package %s, %s, %s", pk_info_enum_to_text (info), package_id, summary_safe);
	g_signal_emit (backend, signals [PK_BACKEND_PACKAGE], 0, info, package_id, summary_safe);
	g_free (summary_safe);
	return TRUE;
}

/**
 * pk_backend_update_detail:
 **/
gboolean
pk_backend_update_detail (PkBackend *backend, const gchar *package_id,
			  const gchar *updates, const gchar *obsoletes,
			  const gchar *vendor_url, const gchar *bugzilla_url,
			  const gchar *cve_url, PkRestartEnum restart,
			  const gchar *update_text)
{
	gchar *update_text_safe;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	/* replace unsafe chars */
	update_text_safe = pk_strsafe (update_text);

	pk_debug ("emit update-detail %s, %s, %s, %s, %s, %s, %i, %s",
		  package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text_safe);
	g_signal_emit (backend, signals [PK_BACKEND_UPDATE_DETAIL], 0,
		       package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text_safe);
	g_free (update_text_safe);
	return TRUE;
}

/**
 * pk_backend_get_progress:
 **/
gboolean
pk_backend_get_progress (PkBackend *backend,
			 guint *percentage, guint *subpercentage,
			 guint *elapsed, guint *remaining)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	*percentage = backend->priv->last_percentage;
	/* have not ever set any value? */
	if (*percentage == PK_BACKEND_PERCENTAGE_DEFAULT) {
		*percentage = PK_BACKEND_PERCENTAGE_INVALID;
	}
	*subpercentage = backend->priv->last_subpercentage;
	*elapsed = pk_time_get_elapsed (backend->priv->time);
	*remaining = backend->priv->last_remaining;
	return TRUE;
}

/**
 * pk_backend_require_restart:
 **/
gboolean
pk_backend_require_restart (PkBackend *backend, PkRestartEnum restart, const gchar *details)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	pk_debug ("emit require-restart %i, %s", restart, details);
	g_signal_emit (backend, signals [PK_BACKEND_REQUIRE_RESTART], 0, restart, details);

	return TRUE;
}

/**
 * pk_backend_message:
 **/
gboolean
pk_backend_message (PkBackend *backend, PkMessageEnum message, const gchar *format, ...)
{
	va_list args;
	gchar *buffer;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	pk_debug ("emit message %i, %s", message, buffer);
	g_signal_emit (backend, signals [PK_BACKEND_MESSAGE], 0, message, buffer);
	g_free (buffer);

	return TRUE;
}

/**
 * pk_backend_set_transaction_data:
 **/
gboolean
pk_backend_set_transaction_data (PkBackend *backend, const gchar *data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	pk_debug ("emit change-transaction-data %s", data);
	g_signal_emit (backend, signals [PK_BACKEND_CHANGE_TRANSACTION_DATA], 0, data);
	return TRUE;
}

/**
 * pk_backend_details:
 **/
gboolean
pk_backend_details (PkBackend *backend, const gchar *package_id,
		    const gchar *license, PkGroupEnum group,
		    const gchar *description, const gchar *url, gulong size)
{
	gchar *description_safe;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	/* replace unsafe chars */
	description_safe = pk_strsafe (description);

	pk_debug ("emit details %s, %s, %i, %s, %s, %ld",
		  package_id, license, group, description_safe, url,
		  size);
	g_signal_emit (backend, signals [PK_BACKEND_DETAILS], 0,
		       package_id, license, group, description_safe, url,
		       size);
	g_free (description_safe);
	return TRUE;
}

/**
 * pk_backend_files:
 **/
gboolean
pk_backend_files (PkBackend *backend, const gchar *package_id, const gchar *filelist)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (filelist != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	pk_debug ("emit files %s, %s", package_id, filelist);
	g_signal_emit (backend, signals [PK_BACKEND_FILES], 0,
		       package_id, filelist);

	return TRUE;
}

/**
 * pk_backend_repo_signature_required:
 **/
gboolean
pk_backend_repo_signature_required (PkBackend *backend, const gchar *package_id,
				    const gchar *repository_name, const gchar *key_url,
				    const gchar *key_userid, const gchar *key_id, const gchar *key_fingerprint,
				    const gchar *key_timestamp, PkSigTypeEnum type)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (repository_name != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	/* check we don't do this more than once */
	if (backend->priv->set_signature) {
		pk_warning ("already asked for a signature, cannot process");
		return FALSE;
	}
	backend->priv->set_signature = TRUE;

	pk_debug ("emit repo-signature-required %s, %s, %s, %s, %s, %s, %s, %i",
		  package_id, repository_name, key_url, key_userid, key_id,
		  key_fingerprint, key_timestamp, type);
	g_signal_emit (backend, signals [PK_BACKEND_REPO_SIGNATURE_REQUIRED], 0,
		       package_id, repository_name, key_url, key_userid, key_id,
		       key_fingerprint, key_timestamp, type);
	return TRUE;
}

/**
 * pk_backend_eula_required:
 **/
gboolean
pk_backend_eula_required (PkBackend *backend, const gchar *eula_id, const gchar *package_id,
			  const gchar *vendor_name, const gchar *license_agreement)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (eula_id != NULL, FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (vendor_name != NULL, FALSE);
	g_return_val_if_fail (license_agreement != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	/* check we don't do this more than once */
	if (backend->priv->set_eula) {
		pk_warning ("already asked for a signature, cannot process");
		return FALSE;
	}
	backend->priv->set_eula = TRUE;

	pk_debug ("emit eula-required %s, %s, %s, %s",
		  eula_id, package_id, vendor_name, license_agreement);

	g_signal_emit (backend, signals [PK_BACKEND_EULA_REQUIRED], 0,
		       eula_id, package_id, vendor_name, license_agreement);

	return TRUE;
}

/**
 * pk_backend_repo_detail:
 **/
gboolean
pk_backend_repo_detail (PkBackend *backend, const gchar *repo_id,
			const gchar *description, gboolean enabled)
{
	gchar *description_safe;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (repo_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}

	/* replace unsafe chars */
	description_safe = pk_strsafe (description);

	pk_debug ("emit repo-detail %s, %s, %i", repo_id, description_safe, enabled);
	g_signal_emit (backend, signals [PK_BACKEND_REPO_DETAIL], 0, repo_id, description, enabled);
	g_free (description_safe);
	return TRUE;
}

/**
 * pk_backend_error_timeout_delay_cb:
 *
 * We have to call Finished() within PK_BACKEND_FINISHED_ERROR_TIMEOUT of ErrorCode(), enforce this.
 **/
static gboolean
pk_backend_error_timeout_delay_cb (gpointer data)
{
	PkBackend *backend = PK_BACKEND (data);
	PkMessageEnum message;
	const gchar *buffer;

	/* check we have not already finished */
	if (backend->priv->finished) {
		pk_warning ("consistency error");
		return FALSE;
	}

	/* warn the backend developer that they've done something worng
	 * - we can't use pk_backend_message here as we have already set
	 * backend->priv->set_error to TRUE and hence the message would be ignored */
	message = PK_MESSAGE_ENUM_DAEMON;
	buffer = "ErrorCode() has to be followed with Finished()!";
	pk_warning ("emit message %i, %s", message, buffer);
	g_signal_emit (backend, signals [PK_BACKEND_MESSAGE], 0, message, buffer);

	pk_backend_finished (backend);
	return FALSE;
}

/**
 * pk_backend_error_code:
 **/
gboolean
pk_backend_error_code (PkBackend *backend, PkErrorCodeEnum code, const gchar *format, ...)
{
	va_list args;
	gchar *buffer;
	gboolean ret = TRUE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	va_start (args, format);
	buffer = g_strdup_vprintf (format, args);
	va_end (args);

	/* check we are not doing Init() */
	if (backend->priv->during_initialize) {
		pk_warning ("set during init: %s", buffer);
		ret = FALSE;
		goto out;
	}

	/* did we set a duplicate error? */
	if (backend->priv->set_error) {
		pk_warning ("More than one error emitted! You tried to set '%s'", buffer);
		ret = FALSE;
		goto out;
	}
	backend->priv->set_error = TRUE;

	/* we only allow a short time to send finished after error_code */
	backend->priv->signal_error_timeout = g_timeout_add (PK_BACKEND_FINISHED_ERROR_TIMEOUT,
							     pk_backend_error_timeout_delay_cb, backend);

	/* we mark any transaction with errors as failed */
	pk_backend_set_exit_code (backend, PK_EXIT_ENUM_FAILED);

	pk_debug ("emit error-code %s, %s", pk_error_enum_to_text (code), buffer);
	g_signal_emit (backend, signals [PK_BACKEND_ERROR_CODE], 0, code, buffer);

out:
	g_free (buffer);
	return ret;
}

/**
 * pk_backend_set_allow_cancel:
 **/
gboolean
pk_backend_set_allow_cancel (PkBackend *backend, gboolean allow_cancel)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->desc != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		pk_warning ("already set error, cannot process");
		return FALSE;
	}


	/* can we do the action? */
	if (backend->desc->cancel != NULL) {
		backend->priv->allow_cancel = allow_cancel;
		pk_debug ("emit allow-cancel %i", allow_cancel);
		g_signal_emit (backend, signals [PK_BACKEND_ALLOW_CANCEL], 0, allow_cancel);
	}
	return TRUE;
}

/**
 * pk_backend_get_allow_cancel:
 **/
gboolean
pk_backend_get_allow_cancel (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);
	return backend->priv->allow_cancel;
}

/**
 * pk_backend_set_role:
 **/
gboolean
pk_backend_set_role (PkBackend *backend, PkRoleEnum role)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* Should only be called once... */
	if (backend->priv->role != PK_ROLE_ENUM_UNKNOWN) {
		pk_warning ("cannot set role more than once, already %s",
			    pk_role_enum_to_text (backend->priv->role));
		return FALSE;
	}

	/* reset the timer */
	pk_time_reset (backend->priv->time);

	pk_debug ("setting role to %s", pk_role_enum_to_text (role));
	backend->priv->role = role;
	backend->priv->status = PK_STATUS_ENUM_WAIT;
	return TRUE;
}

/**
 * pk_backend_get_role:
 **/
PkRoleEnum
pk_backend_get_role (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_ROLE_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->locked != FALSE, PK_ROLE_ENUM_UNKNOWN);
	return backend->priv->role;
}

/**
 * pk_backend_set_exit_code:
 *
 * Should only be used internally, or from PkRunner when setting CANCELLED.
 **/
gboolean
pk_backend_set_exit_code (PkBackend *backend, PkExitEnum exit)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_ROLE_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	if (backend->priv->exit != PK_EXIT_ENUM_UNKNOWN) {
		pk_warning ("already set exit status: old=%s, new=%s",
			    pk_exit_enum_to_text (backend->priv->exit),
			    pk_exit_enum_to_text (exit));
		return FALSE;
	}

	/* new value */
	backend->priv->exit = exit;
	return TRUE;
}

/**
 * pk_backend_finished_delay:
 *
 * We can call into this function if we *know* it's safe.
 **/
static gboolean
pk_backend_finished_delay (gpointer data)
{
	PkBackend *backend = PK_BACKEND (data);

	/* this wasn't set otherwise, assume success */
	if (backend->priv->exit == PK_EXIT_ENUM_UNKNOWN) {
		pk_backend_set_exit_code (backend, PK_EXIT_ENUM_SUCCESS);
	}

	pk_debug ("resetting hash tables to blank");
	g_hash_table_remove_all (backend->priv->hash_pointer);
	g_hash_table_remove_all (backend->priv->hash_string);
	g_hash_table_remove_all (backend->priv->hash_strv);
	g_hash_table_remove_all (backend->priv->hash_array);

	pk_debug ("emit finished %i", backend->priv->exit);
	g_signal_emit (backend, signals [PK_BACKEND_FINISHED], 0, backend->priv->exit);
	backend->priv->signal_finished = 0;
	return FALSE;
}

/**
 * pk_backend_finished:
 **/
gboolean
pk_backend_finished (PkBackend *backend)
{
	const gchar *role_text;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* check we are not doing Init() */
	if (backend->priv->during_initialize) {
		pk_warning ("finished during init");
		return FALSE;
	}

	/* safe to check now */
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* find out what we just did */
	role_text = pk_role_enum_to_text (backend->priv->role);
	pk_debug ("finished role %s", role_text);

	/* are we trying to finish in init? */
	if (backend->priv->during_initialize) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_DAEMON,
				    "You can't call pk_backend_finished in backend_initialize!");
		return FALSE;
	}

	/* check we have not already finished */
	if (backend->priv->finished) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_DAEMON,
				    "Backends cannot request Finished more than once!");
		return FALSE;
	}

	/* check we got a Package() else the UI will suck */
	if (!backend->priv->set_error &&
	    !backend->priv->has_sent_package &&
	    (backend->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	     backend->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES ||
	     backend->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_DAEMON,
				    "Backends need to send a Package() for this role!");
		return FALSE;
	}

	/* if we set an error code notifier, clear */
	if (backend->priv->signal_error_timeout != 0) {
		g_source_remove (backend->priv->signal_error_timeout);
		backend->priv->signal_error_timeout = 0;
	}

	/* check we sent at least one status calls */
	if (backend->priv->set_error == FALSE &&
	    backend->priv->status == PK_STATUS_ENUM_SETUP) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_DAEMON,
				    "Backends should send status <value> signals for %s!", role_text);
		pk_warning ("GUI will remain unchanged!");
	}

	/* make any UI insensitive */
	pk_backend_set_allow_cancel (backend, FALSE);

	/* mark as finished for the UI that might only be watching status */
	pk_backend_set_status (backend, PK_STATUS_ENUM_FINISHED);

	/* we can't ever be re-used */
	backend->priv->finished = TRUE;

	/* we have to run this idle as the command may finish before the transaction
	 * has been sent to the client. I love async... */
	pk_debug ("adding finished %p to timeout loop", backend);
	backend->priv->signal_finished = g_timeout_add (PK_BACKEND_FINISHED_TIMEOUT_GRACE, pk_backend_finished_delay, backend);
	return TRUE;
}

/**
 * pk_backend_not_implemented_yet:
 **/
gboolean
pk_backend_not_implemented_yet (PkBackend *backend, const gchar *method)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (method != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* this function is only valid when we have a running transaction */
	if (backend->priv->c_tid != NULL) {
		pk_error ("only valid when we have a running transaction");
		return FALSE;
	}
	pk_backend_error_code (backend, PK_ERROR_ENUM_NOT_SUPPORTED, "the method '%s' is not implemented yet", method);
	/* don't wait, do this now */
	pk_backend_finished_delay (backend);
	return TRUE;
}

/**
 * pk_backend_is_online:
 **/
gboolean
pk_backend_is_online (PkBackend *backend)
{
	PkNetworkEnum state;
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	state = pk_network_get_network_state (backend->priv->network);
	if (pk_enums_contain (state, PK_NETWORK_ENUM_ONLINE)) {
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_backend_thread_create:
 **/
gboolean
pk_backend_thread_create (PkBackend *backend, PkBackendThreadFunc func)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (backend->priv->thread != NULL) {
		pk_warning ("already has thread");
		return FALSE;
	}
	backend->priv->thread = g_thread_create ((GThreadFunc) func, backend, FALSE, NULL);
	if (backend->priv->thread == NULL) {
		pk_warning ("failed to create thread");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_backend_get_backend_detail:
 */
gboolean
pk_backend_get_backend_detail (PkBackend *backend, gchar **name, gchar **author)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->desc != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	if (name != NULL && backend->desc->description != NULL) {
		*name = g_strdup (backend->desc->description);
	}
	if (author != NULL && backend->desc->author != NULL) {
		*author = g_strdup (backend->desc->author);
	}
	return TRUE;
}

/**
 * pk_backend_get_current_tid:
 */
const gchar *
pk_backend_get_current_tid (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->locked != FALSE, NULL);
	return backend->priv->c_tid;
}

/**
 * pk_backend_set_current_tid:
 */
gboolean
pk_backend_set_current_tid (PkBackend *backend, const gchar *tid)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	pk_debug ("setting backend tid as %s", tid);
	g_free (backend->priv->c_tid);
	backend->priv->c_tid = g_strdup (tid);
	return TRUE;
}

/**
 * pk_backend_accept_eula:
 */
gboolean
pk_backend_accept_eula (PkBackend *backend, const gchar *eula_id)
{
	gpointer present;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (eula_id != NULL, FALSE);

	pk_debug ("eula_id %s", eula_id);
	present = g_hash_table_lookup (backend->priv->eulas, eula_id);
	if (present != NULL) {
		pk_debug ("already added %s to accepted list", eula_id);
		return FALSE;
	}
	g_hash_table_insert (backend->priv->eulas, g_strdup (eula_id), GINT_TO_POINTER (1));
	return TRUE;
}

/**
 * pk_backend_is_eula_valid:
 */
gboolean
pk_backend_is_eula_valid (PkBackend *backend, const gchar *eula_id)
{
	gpointer present;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (eula_id != NULL, FALSE);

	present = g_hash_table_lookup (backend->priv->eulas, eula_id);
	if (present != NULL) {
		return TRUE;
	}
	return FALSE;
}


/**
 * pk_backend_watch_file:
 */
gboolean
pk_backend_watch_file (PkBackend *backend, const gchar *filename, PkBackendFileChanged func, gpointer data)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (backend->priv->file_changed_func != NULL) {
		pk_warning ("already set");
		return FALSE;
	}
	ret = pk_file_monitor_set_file (backend->priv->file_monitor, filename);;

	/* if we set it up, set the function callback */
	if (ret) {
		backend->priv->file_changed_func = func;
		backend->priv->file_changed_data = data;
	}
	return ret;
}

/**
 * pk_backend_file_monitor_changed_cb:
 **/
static void
pk_backend_file_monitor_changed_cb (PkFileMonitor *file_monitor, PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_debug ("config file changed");
	backend->priv->file_changed_func (backend, backend->priv->file_changed_data);
}

/**
 * pk_backend_finalize:
 **/
static void
pk_backend_finalize (GObject *object)
{
	PkBackend *backend;
	g_return_if_fail (PK_IS_BACKEND (object));
	backend = PK_BACKEND (object);

	pk_debug ("backend finalise");

	pk_backend_reset (backend);
	g_free (backend->priv->proxy_http);
	g_free (backend->priv->proxy_ftp);
	g_free (backend->priv->name);
	g_free (backend->priv->c_tid);
	g_object_unref (backend->priv->time);
	g_object_unref (backend->priv->network);
	g_hash_table_destroy (backend->priv->eulas);
	g_hash_table_unref (backend->priv->hash_string);
	g_hash_table_unref (backend->priv->hash_strv);
	g_hash_table_unref (backend->priv->hash_pointer);
	g_hash_table_unref (backend->priv->hash_array);

	if (backend->priv->handle != NULL) {
		g_module_close (backend->priv->handle);
	}
	pk_debug ("parent_class->finalize");
	G_OBJECT_CLASS (pk_backend_parent_class)->finalize (object);
}

/**
 * pk_backend_class_init:
 **/
static void
pk_backend_class_init (PkBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_backend_finalize;
	signals [PK_BACKEND_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_BACKEND_PROGRESS_CHANGED] =
		g_signal_new ("progress-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT_UINT_UINT,
			      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
	signals [PK_BACKEND_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_BACKEND_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_UINT_STRING,
			      G_TYPE_NONE, 8, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_BACKEND_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_BACKEND_MESSAGE] =
		g_signal_new ("message",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_BACKEND_CHANGE_TRANSACTION_DATA] =
		g_signal_new ("change-transaction-data",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
	signals [PK_BACKEND_DETAILS] =
		g_signal_new ("details",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_UINT_STRING_STRING_UINT64,
			      G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_UINT64);
	signals [PK_BACKEND_FILES] =
		g_signal_new ("files",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_BACKEND_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_BACKEND_REPO_SIGNATURE_REQUIRED] =
		g_signal_new ("repo-signature-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_UINT,
			      G_TYPE_NONE, 8, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);
	signals [PK_BACKEND_EULA_REQUIRED] =
		g_signal_new ("eula-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_BACKEND_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_BACKEND_ALLOW_CANCEL] =
		g_signal_new ("allow-cancel",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [PK_BACKEND_REPO_DETAIL] =
		g_signal_new ("repo-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOL,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
	g_type_class_add_private (klass, sizeof (PkBackendPrivate));
}

/**
 * pk_backend_reset:
 **/
gboolean
pk_backend_reset (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* do finish now, as we might be unreffing quickly */
	if (backend->priv->signal_finished != 0) {
		g_source_remove (backend->priv->signal_finished);
		pk_debug ("doing unref quickly delay");
		pk_backend_finished_delay (backend);
	}

	/* if we set an error code notifier, clear */
	if (backend->priv->signal_error_timeout != 0) {
		g_source_remove (backend->priv->signal_error_timeout);
	}

	/* TODO: need to wait for Finished() if running */

	pk_package_item_free (backend->priv->last_package);
	backend->priv->set_error = FALSE;
	backend->priv->set_signature = FALSE;
	backend->priv->set_eula = FALSE;
	backend->priv->allow_cancel = FALSE;
	backend->priv->finished = FALSE;
	backend->priv->has_sent_package = FALSE;
	backend->priv->thread = NULL;
	backend->priv->last_package = NULL;
	backend->priv->status = PK_STATUS_ENUM_UNKNOWN;
	backend->priv->exit = PK_EXIT_ENUM_UNKNOWN;
	backend->priv->role = PK_ROLE_ENUM_UNKNOWN;
	backend->priv->last_remaining = 0;
	backend->priv->last_percentage = PK_BACKEND_PERCENTAGE_DEFAULT;
	backend->priv->last_subpercentage = PK_BACKEND_PERCENTAGE_INVALID;
	pk_time_reset (backend->priv->time);

	return TRUE;
}

/**
 * pk_free_ptr_array:
 **/
static void
pk_free_ptr_array (gpointer data)
{
	g_ptr_array_free ((GPtrArray *) data, TRUE);
}

/**
 * pk_backend_init:
 **/
static void
pk_backend_init (PkBackend *backend)
{
	backend->priv = PK_BACKEND_GET_PRIVATE (backend);
	backend->priv->handle = NULL;
	backend->priv->name = NULL;
	backend->priv->c_tid = NULL;
	backend->priv->proxy_http = NULL;
	backend->priv->proxy_ftp = NULL;
	backend->priv->file_changed_func = NULL;
	backend->priv->file_changed_data = NULL;
	backend->priv->last_package = NULL;
	backend->priv->locked = FALSE;
	backend->priv->signal_finished = 0;
	backend->priv->signal_error_timeout = 0;
	backend->priv->during_initialize = FALSE;
	backend->priv->time = pk_time_new ();
	backend->priv->network = pk_network_new ();
	backend->priv->eulas = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	backend->priv->hash_string = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	backend->priv->hash_strv = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_strfreev);
	backend->priv->hash_pointer = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	backend->priv->hash_array = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, pk_free_ptr_array);

	/* monitor config files for changes */
	backend->priv->file_monitor = pk_file_monitor_new ();
	g_signal_connect (backend->priv->file_monitor, "file-changed",
			  G_CALLBACK (pk_backend_file_monitor_changed_cb), backend);

	pk_backend_reset (backend);
}

/**
 * pk_backend_new:
 * Return value: A new backend class backend.
 **/
PkBackend *
pk_backend_new (void)
{
	pk_debug ("new object");
	if (pk_backend_object != NULL) {
		g_object_ref (pk_backend_object);
	} else {
		pk_backend_object = g_object_new (PK_TYPE_BACKEND, NULL);
		g_object_add_weak_pointer (pk_backend_object, &pk_backend_object);
	}
	return PK_BACKEND (pk_backend_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>
#include <glib/gstdio.h>

static guint number_messages = 0;

/**
 * pk_backend_test_message_cb:
 **/
static void
pk_backend_test_message_cb (PkBackend *backend, PkMessageEnum message, const gchar *details, gpointer data)
{
	pk_debug ("details=%s", details);
	number_messages++;
}

/**
 * pk_backend_test_finished_cb:
 **/
static void
pk_backend_test_finished_cb (PkBackend *backend, PkExitEnum exit, LibSelfTest *test)
{
	libst_loopquit (test);
}

/**
 * pk_backend_test_watch_file_cb:
 **/
static void
pk_backend_test_watch_file_cb (PkBackend *backend, gpointer data)
{
	LibSelfTest *test = (LibSelfTest *) data;
	libst_loopquit (test);
}

static gboolean
pk_backend_test_func_true (PkBackend *backend)
{
	g_usleep (1000*1000);
	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
pk_backend_test_func_immediate_false (PkBackend *backend)
{
	pk_backend_finished (backend);
	return FALSE;
}

void
libst_backend (LibSelfTest *test)
{
	PkBackend *backend;
	gchar *text;
	gboolean ret;
	const gchar *filename;
	const gchar *data_string;
	guint data_uint;
	gboolean data_bool;

	if (libst_start (test, "PkBackend", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get an backend");
	backend = pk_backend_new ();
	if (backend != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "set a blank string");
	ret = pk_backend_set_string (backend, "dave2", "");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "set a ~bool");
	ret = pk_backend_set_bool (backend, "roger2", FALSE);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "set a zero uint");
	ret = pk_backend_set_uint (backend, "linda2", 0);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get a blank string");
	data_string = pk_backend_get_string (backend, "dave2");
	if (pk_strequal (data_string, "")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "data was %s", data_string);
	}

	/************************************************************/
	libst_title (test, "get a ~bool");
	data_bool = pk_backend_get_bool (backend, "roger2");
	if (!data_bool) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "data was %i", data_bool);
	}

	/************************************************************/
	libst_title (test, "get a zero uint");
	data_uint = pk_backend_get_uint (backend, "linda2");
	if (data_uint == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "data was %i", data_uint);
	}

	/************************************************************/
	libst_title (test, "set a string");
	ret = pk_backend_set_string (backend, "dave", "ania");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "set a bool");
	ret = pk_backend_set_bool (backend, "roger", TRUE);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "set a uint");
	ret = pk_backend_set_uint (backend, "linda", 999);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get a string");
	data_string = pk_backend_get_string (backend, "dave");
	if (pk_strequal (data_string, "ania")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "data was %s", data_string);
	}

	/************************************************************/
	libst_title (test, "get a bool");
	data_bool = pk_backend_get_bool (backend, "roger");
	if (data_bool) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "data was %i", data_bool);
	}

	/************************************************************/
	libst_title (test, "get a uint");
	data_uint = pk_backend_get_uint (backend, "linda");
	if (data_uint == 999) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "data was %i", data_uint);
	}

	/************************************************************/
	libst_title (test, "create a config file");
	filename = "/tmp/dave";
	ret = g_file_set_contents (filename, "foo", -1, NULL);
	if (ret) {
		libst_success (test, "set contents");
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "set up a watch file on a config file");
	ret = pk_backend_watch_file (backend, filename, pk_backend_test_watch_file_cb, test);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "eula valid");
	}

	/************************************************************/
	libst_title (test, "change the config file");
	ret = g_file_set_contents (filename, "bar", -1, NULL);
	if (ret) {
		libst_success (test, "set contents");
	} else {
		libst_failed (test, NULL);
	}

	/* wait for config file change */
	libst_loopwait (test, 2000);
	libst_loopcheck (test);

	/************************************************************/
	libst_title (test, "delete the config file");
	ret = g_unlink (filename);
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	g_signal_connect (backend, "message", G_CALLBACK (pk_backend_test_message_cb), NULL);
	g_signal_connect (backend, "finished", G_CALLBACK (pk_backend_test_finished_cb), test);

	/************************************************************/
	libst_title (test, "get eula that does not exist");
	ret = pk_backend_is_eula_valid (backend, "license_foo");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "eula valid");
	}

	/************************************************************/
	libst_title (test, "accept eula");
	ret = pk_backend_accept_eula (backend, "license_foo");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "eula was not accepted");
	}

	/************************************************************/
	libst_title (test, "get eula that does exist");
	ret = pk_backend_is_eula_valid (backend, "license_foo");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "eula valid");
	}

	/************************************************************/
	libst_title (test, "accept eula (again)");
	ret = pk_backend_accept_eula (backend, "license_foo");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "eula was accepted twice");
	}

	/************************************************************/
	libst_title (test, "get backend name");
	text = pk_backend_get_name (backend);
	if (text == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid name %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "load an invalid backend");
	ret = pk_backend_set_name (backend, "invalid");
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "try to load a valid backend");
	ret = pk_backend_set_name (backend, "dummy");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "load an valid backend again");
	ret = pk_backend_set_name (backend, "dummy");
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "loaded twice");
	}

	/************************************************************/
	libst_title (test, "lock an valid backend");
	ret = pk_backend_lock (backend);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed to lock");
	}

	/************************************************************/
	libst_title (test, "lock a backend again");
	ret = pk_backend_lock (backend);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "locked twice should succeed");
	}

	/************************************************************/
	libst_title (test, "check we are out of init");
	if (backend->priv->during_initialize == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "not out of init");
	}

	/************************************************************/
	libst_title (test, "get backend name");
	text = pk_backend_get_name (backend);
	if (pk_strequal(text, "dummy")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid name %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "unlock an valid backend");
	ret = pk_backend_unlock (backend);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed to unlock");
	}

	/************************************************************/
	libst_title (test, "unlock an valid backend again");
	ret = pk_backend_unlock (backend);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "unlocked twice, should succeed");
	}

	/************************************************************/
	libst_title (test, "check we are not finished");
	if (backend->priv->finished == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "we did not clear finish!");
	}

	/************************************************************/
	libst_title (test, "check we have no error");
	if (backend->priv->set_error == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "an error has already been set");
	}

	/************************************************************/
	libst_title (test, "lock again");
	ret = pk_backend_lock (backend);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed to unlock");
	}

	/************************************************************/
	libst_title (test, "wait for a thread to return true");
	ret = pk_backend_thread_create (backend, pk_backend_test_func_true);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "wait for a thread failed");
	}

	/* wait for Finished */
	libst_loopwait (test, 2000);
	libst_loopcheck (test);

	/* reset */
	pk_backend_reset (backend);

	/************************************************************/
	libst_title (test, "wait for a thread to return false (straight away)");
	ret = pk_backend_thread_create (backend, pk_backend_test_func_immediate_false);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "returned false!");
	}

	/* wait for Finished */
	libst_loopwait (test, PK_BACKEND_FINISHED_TIMEOUT_GRACE + 100);
	libst_loopcheck (test);

	/************************************************************/
	pk_backend_reset (backend);
	pk_backend_error_code (backend, PK_ERROR_ENUM_GPG_FAILURE, "test error");

	/* wait for finished */
	libst_loopwait (test, PK_BACKEND_FINISHED_ERROR_TIMEOUT + 200);
	libst_loopcheck (test);

	libst_title (test, "check we enforce finished after error_code");
	if (number_messages == 1) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "we messaged %i times!", number_messages);
	}

	g_object_unref (backend);

	libst_end (test);
}
#endif

