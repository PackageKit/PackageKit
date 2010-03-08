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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-common.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-conf.h"
#include "pk-network.h"
#include "pk-marshal.h"
#include "pk-backend-internal.h"
#include "pk-backend.h"
#include "pk-conf.h"
#include "pk-store.h"
#include "pk-shared.h"
#include "pk-time.h"
#include "pk-file-monitor.h"
#include "pk-notify.h"

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
#define PK_BACKEND_FINISHED_TIMEOUT_GRACE	10 /* ms */

struct _PkBackendPrivate
{
	gboolean		 during_initialize;
	gboolean		 finished;
	gboolean		 has_sent_package;
	gboolean		 locked;
	gboolean		 set_error;
	gboolean		 set_eula;
	gboolean		 set_signature;
	gboolean		 simultaneous;
	gboolean		 use_time;
	gchar			*transaction_id;
	gchar			*locale;
	gchar			*name;
	gchar			*proxy_ftp;
	gchar			*proxy_http;
	gpointer		 file_changed_data;
	guint			 download_files;
	guint			 last_percentage;
	guint			 last_remaining;
	guint			 last_subpercentage;
	guint			 signal_error_timeout;
	guint			 signal_finished;
	guint			 speed;
	GHashTable		*eulas;
	GModule			*handle;
	GThread			*thread;
	PkBackendDesc		*desc;
	PkBackendFileChanged	 file_changed_func;
	PkHintEnum		 background;
	PkHintEnum		 interactive;
	PkHintEnum		 allow_cancel;
	PkBitfield		 roles;
	PkConf			*conf;
	PkExitEnum		 exit;
	PkFileMonitor		*file_monitor;
	PkPackage		*last_package;
	PkNetwork		*network;
	PkResults		*results;
	PkRoleEnum		 role; /* this never changes for the lifetime of a transaction */
	PkStatusEnum		 status; /* this changes */
	PkStore			*store;
	PkTime			*time;
};

G_DEFINE_TYPE (PkBackend, pk_backend, G_TYPE_OBJECT)
static gpointer pk_backend_object = NULL;

enum {
	SIGNAL_STATUS_CHANGED,
	SIGNAL_PROGRESS_CHANGED,
	SIGNAL_DETAILS,
	SIGNAL_FILES,
	SIGNAL_DISTRO_UPGRADE,
	SIGNAL_PACKAGE,
	SIGNAL_UPDATE_DETAIL,
	SIGNAL_ERROR_CODE,
	SIGNAL_REPO_SIGNATURE_REQUIRED,
	SIGNAL_EULA_REQUIRED,
	SIGNAL_REQUIRE_RESTART,
	SIGNAL_MESSAGE,
	SIGNAL_CHANGE_TRANSACTION_DATA,
	SIGNAL_FINISHED,
	SIGNAL_ALLOW_CANCEL,
	SIGNAL_REPO_DETAIL,
	SIGNAL_CATEGORY,
	SIGNAL_MEDIA_CHANGE_REQUIRED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_BACKGROUND,
	PROP_INTERACTIVE,
	PROP_STATUS,
	PROP_ROLE,
	PROP_TRANSACTION_ID,
	PROP_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

/**
 * pk_backend_get_groups:
 **/
PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_GROUP_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->locked != FALSE, PK_GROUP_ENUM_UNKNOWN);

	/* not compulsory */
	if (backend->priv->desc->get_groups == NULL)
		return PK_GROUP_ENUM_UNKNOWN;
	return backend->priv->desc->get_groups (backend);
}

/**
 * pk_backend_get_mime_types:
 **/
gchar *
pk_backend_get_mime_types (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->locked != FALSE, NULL);

	/* not compulsory */
	if (backend->priv->desc->get_mime_types == NULL)
		return g_strdup ("");
	return backend->priv->desc->get_mime_types (backend);
}

/**
 * pk_backend_get_filters:
 **/
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_FILTER_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->locked != FALSE, PK_FILTER_ENUM_UNKNOWN);

	/* not compulsory */
	if (backend->priv->desc->get_filters == NULL)
		return PK_FILTER_ENUM_UNKNOWN;
	return backend->priv->desc->get_filters (backend);
}

/**
 * pk_backend_get_role:
 **/
PkRoleEnum
pk_backend_get_role (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_ROLE_ENUM_UNKNOWN);
	return backend->priv->role;
}

/**
 * pk_backend_get_roles:
 **/
PkBitfield
pk_backend_get_roles (PkBackend *backend)
{
	PkBitfield roles = 0;
	PkBackendDesc *desc;

	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_ROLE_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->locked != FALSE, PK_ROLE_ENUM_UNKNOWN);

	/* optimise */
	if (backend->priv->roles != 0)
		goto out;

	/* not compulsory, but use it if we've got it */
	if (backend->priv->desc->get_roles != NULL) {
		backend->priv->roles = backend->priv->desc->get_roles (backend);
		goto out;
	}

	/* lets reduce pointer dereferences... */
	desc = backend->priv->desc;
	if (desc->cancel != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_CANCEL);
	if (desc->get_depends != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DEPENDS);
	if (desc->get_details != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DETAILS);
	if (desc->get_files != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_FILES);
	if (desc->get_requires != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_REQUIRES);
	if (desc->get_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_PACKAGES);
	if (desc->what_provides != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_WHAT_PROVIDES);
	if (desc->get_updates != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_UPDATES);
	if (desc->get_update_detail != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_UPDATE_DETAIL);
	if (desc->install_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_INSTALL_PACKAGES);
	if (desc->install_signature != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_INSTALL_SIGNATURE);
	if (desc->install_files != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_INSTALL_FILES);
	if (desc->refresh_cache != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REFRESH_CACHE);
	if (desc->remove_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REMOVE_PACKAGES);
	if (desc->download_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_DOWNLOAD_PACKAGES);
	if (desc->resolve != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_RESOLVE);
	if (desc->rollback != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_ROLLBACK);
	if (desc->search_details != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SEARCH_DETAILS);
	if (desc->search_files != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SEARCH_FILE);
	if (desc->search_groups != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SEARCH_GROUP);
	if (desc->search_names != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SEARCH_NAME);
	if (desc->update_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_UPDATE_PACKAGES);
	if (desc->update_system != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_UPDATE_SYSTEM);
	if (desc->get_repo_list != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_REPO_LIST);
	if (desc->repo_enable != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REPO_ENABLE);
	if (desc->repo_set_data != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REPO_SET_DATA);
	if (desc->get_distro_upgrades != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);
	if (desc->get_categories != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_CATEGORIES);
	if (desc->simulate_install_files != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SIMULATE_INSTALL_FILES);
	if (desc->simulate_install_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES);
	if (desc->simulate_remove_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES);
	if (desc->simulate_update_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES);
	backend->priv->roles = roles;
out:
	return backend->priv->roles;
}

/**
 * pk_backend_is_implemented:
 **/
gboolean
pk_backend_is_implemented (PkBackend *backend, PkRoleEnum role)
{
	PkBitfield roles;
	roles = pk_backend_get_roles (backend);
	return pk_bitfield_contain (roles, role);
}

/**
 * pk_backend_set_string:
 **/
gboolean
pk_backend_set_string (PkBackend *backend, const gchar *key, const gchar *data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_set_string (backend->priv->store, key, data);
}

/**
 * pk_backend_set_strv:
 **/
gboolean
pk_backend_set_strv (PkBackend *backend, const gchar *key, gchar **data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_set_strv (backend->priv->store, key, data);
}

/**
 * pk_backend_set_array:
 **/
gboolean
pk_backend_set_array (PkBackend *backend, const gchar *key, GPtrArray *data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_set_array (backend->priv->store, key, data);
}

/**
 * pk_backend_set_uint:
 **/
gboolean
pk_backend_set_uint (PkBackend *backend, const gchar *key, guint data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_set_uint (backend->priv->store, key, data);
}

/**
 * pk_backend_set_bool:
 **/
gboolean
pk_backend_set_bool (PkBackend *backend, const gchar *key, gboolean data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_set_bool (backend->priv->store, key, data);
}

/**
 * pk_backend_set_pointer:
 **/
gboolean
pk_backend_set_pointer (PkBackend *backend, const gchar *key, gpointer data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_set_pointer (backend->priv->store, key, data);
}

/**
 * pk_backend_get_string:
 **/
const gchar *
pk_backend_get_string (PkBackend *backend, const gchar *key)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return pk_store_get_string (backend->priv->store, key);
}

/**
 * pk_backend_get_strv:
 **/
gchar **
pk_backend_get_strv (PkBackend *backend, const gchar *key)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return pk_store_get_strv (backend->priv->store, key);
}

/**
 * pk_backend_get_array:
 **/
const GPtrArray *
pk_backend_get_array (PkBackend *backend, const gchar *key)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return pk_store_get_array (backend->priv->store, key);
}

/**
 * pk_backend_get_uint:
 **/
guint
pk_backend_get_uint (PkBackend *backend, const gchar *key)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), 0);
	return pk_store_get_uint (backend->priv->store, key);
}

/**
 * pk_backend_get_bool:
 **/
gboolean
pk_backend_get_bool (PkBackend *backend, const gchar *key)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_get_bool (backend->priv->store, key);
}

/**
 * pk_backend_get_pointer:
 **/
gpointer
pk_backend_get_pointer (PkBackend *backend, const gchar *key)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return pk_store_get_pointer (backend->priv->store, key);
}

/**
 * pk_backend_build_library_path:
 **/
static gchar *
pk_backend_build_library_path (PkBackend *backend, const gchar *name)
{
	gchar *path;
	gchar *filename;
#if PK_BUILD_LOCAL
	const gchar *directory;
#endif
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	filename = g_strdup_printf ("libpk_backend_%s.so", name);
#if PK_BUILD_LOCAL
	/* test_spawn, test_dbus, test_fail, etc. are in the 'test' folder */
	directory = name;
	if (g_str_has_prefix (name, "test_"))
		directory = "test";

	/* prefer the local version */
	path = g_build_filename ("..", "backends", directory, ".libs", filename, NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS) == FALSE) {
		egg_debug ("local backend not found '%s'", path);
		g_free (path);
		path = g_build_filename (LIBDIR, "packagekit-backend", filename, NULL);
	}
#else
	path = g_build_filename (LIBDIR, "packagekit-backend", filename, NULL);
#endif
	g_free (filename);
	egg_debug ("dlopening '%s'", path);

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
		egg_warning ("pk_backend_set_name called multiple times");
		ret = FALSE;
		goto out;
	}

	/* can we load it? */
	egg_debug ("Trying to load : %s", backend_name);
	path = pk_backend_build_library_path (backend, backend_name);
	handle = g_module_open (path, 0);
	if (handle == NULL) {
		egg_warning ("opening module %s failed : %s", backend_name, g_module_error ());
		ret = FALSE;
		goto out;
	}

	/* is is correctly formed? */
	if (!g_module_symbol (handle, "pk_backend_desc", (gpointer) &backend->priv->desc)) {
		g_module_close (handle);
		egg_warning ("could not find description in plugin %s, not loading", backend_name);
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
	g_return_val_if_fail (backend->priv->desc != NULL, FALSE);

	if (backend->priv->locked) {
		egg_warning ("already locked");
		/* we don't return FALSE here, as the action didn't fail */
		return TRUE;
	}
	if (backend->priv->desc->initialize != NULL) {
		backend->priv->during_initialize = TRUE;
		backend->priv->desc->initialize (backend);
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
		egg_warning ("already unlocked");
		/* we don't return FALSE here, as the action didn't fail */
		return TRUE;
	}
	if (backend->priv->desc == NULL) {
		egg_warning ("not yet loaded backend, try pk_backend_lock()");
		return FALSE;
	}
	if (backend->priv->desc->destroy != NULL)
		backend->priv->desc->destroy (backend);
	backend->priv->locked = FALSE;
	return TRUE;
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
	if (percentage == PK_BACKEND_PERCENTAGE_DEFAULT)
		percentage = PK_BACKEND_PERCENTAGE_INVALID;
	subpercentage = backend->priv->last_subpercentage;
	elapsed = pk_time_get_elapsed (backend->priv->time);
	remaining = backend->priv->last_remaining;

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_PROGRESS_CHANGED], 0,
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
		egg_warning ("already set error, cannot process: percentage %i", percentage);
		return FALSE;
	}

	/* set the same twice? */
	if (backend->priv->last_percentage == percentage) {
		egg_debug ("duplicate set of %i", percentage);
		return FALSE;
	}

	/* check over */
	if (percentage > PK_BACKEND_PERCENTAGE_INVALID) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "percentage value is invalid: %i", percentage);
		return FALSE;
	}

	/* check under */
	if (percentage < 100 &&
	    backend->priv->last_percentage < 100 &&
	    percentage < backend->priv->last_percentage) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
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
		egg_debug ("this will now take ~%i seconds", remaining);

		/* value cached from config file */
		if (backend->priv->use_time)
			backend->priv->last_remaining = remaining;
	}

	/* emit the progress changed signal */
	pk_backend_emit_progress_changed (backend);
	return TRUE;
}

/**
 * pk_backend_set_speed:
 **/
gboolean
pk_backend_set_speed (PkBackend *backend, guint speed)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		egg_warning ("already set error, cannot process: speed %i", speed);
		return FALSE;
	}

	/* set the same twice? */
	if (backend->priv->speed == speed) {
		egg_debug ("duplicate set of %i", speed);
		return FALSE;
	}

	/* set new value */
	backend->priv->speed = speed;
	g_object_notify (G_OBJECT (backend), "speed");
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
		egg_warning ("already set error, cannot process: sub-percentage %i", percentage);
		return FALSE;
	}

	/* set the same twice? */
	if (backend->priv->last_subpercentage == percentage) {
		egg_debug ("duplicate set of %i", percentage);
		return FALSE;
	}

	/* invalid number? */
	if (percentage > 100 && percentage != PK_BACKEND_PERCENTAGE_INVALID) {
		egg_debug ("invalid number %i", percentage);
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
		egg_debug ("already set same status");
		return TRUE;
	}

	/* have we already set an error? */
	if (backend->priv->set_error && status != PK_STATUS_ENUM_FINISHED) {
		egg_warning ("already set error, cannot process: status %s", pk_status_enum_to_string (status));
		return FALSE;
	}

	/* backends don't do this */
	if (status == PK_STATUS_ENUM_WAIT) {
		egg_warning ("backend tried to WAIT, only the runner should set this value");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "%s shouldn't use STATUS_WAIT", pk_role_enum_to_string (backend->priv->role));
		return FALSE;
	}

	/* sanity check */
	if (status == PK_STATUS_ENUM_SETUP && backend->priv->status != PK_STATUS_ENUM_WAIT) {
		egg_warning ("backend tried to SETUP, but should be in WAIT");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "%s to SETUP when not in WAIT", pk_role_enum_to_string (backend->priv->role));
		return FALSE;
	}

	/* do we have to enumate a running call? */
	if (status != PK_STATUS_ENUM_RUNNING && status != PK_STATUS_ENUM_SETUP) {
		if (backend->priv->status == PK_STATUS_ENUM_SETUP) {
			/* emit */
			g_signal_emit (backend, signals[SIGNAL_STATUS_CHANGED], 0, PK_STATUS_ENUM_RUNNING);
		}
	}

	backend->priv->status = status;

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_STATUS_CHANGED], 0, status);
	return TRUE;
}

/**
 * pk_backend_package_emulate_finished:
 **/
static gboolean
pk_backend_package_emulate_finished (PkBackend *backend)
{
	gboolean ret = FALSE;
	PkPackage *item;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *summary = NULL;

	/* simultaneous handles this on it's own */
	if (backend->priv->simultaneous)
		goto out;

	/* first package in transaction */
	item = backend->priv->last_package;
	if (item == NULL)
		goto out;


	/* get data */
	g_object_get (item,
		      "info", &info,
		      "package-id", &package_id,
		      "summary", &summary,
		      NULL);

	/* already finished */
	if (info == PK_INFO_ENUM_FINISHED)
		goto out;

	/* only makes sense for some values */
	if (info == PK_INFO_ENUM_DOWNLOADING ||
	    info == PK_INFO_ENUM_UPDATING ||
	    info == PK_INFO_ENUM_INSTALLING ||
	    info == PK_INFO_ENUM_REMOVING ||
	    info == PK_INFO_ENUM_CLEANUP ||
	    info == PK_INFO_ENUM_OBSOLETING ||
	    info == PK_INFO_ENUM_REINSTALLING ||
	    info == PK_INFO_ENUM_DOWNGRADING) {
		pk_backend_package (backend, PK_INFO_ENUM_FINISHED, package_id, summary);
		ret = TRUE;
	}
out:
	g_free (package_id);
	g_free (summary);
	return ret;
}

/**
 * pk_backend_package_emulate_finished_for_package:
 **/
static gboolean
pk_backend_package_emulate_finished_for_package (PkBackend *backend, PkPackage *item)
{
	gboolean ret = FALSE;
	PkInfoEnum info;

	/* simultaneous handles this on it's own */
	if (backend->priv->simultaneous) {
		egg_debug ("backend handling finished");
		goto out;
	}

	/* first package in transaction */
	if (backend->priv->last_package == NULL) {
		egg_debug ("first package, so no finished");
		goto out;
	}

	/* get data */
	g_object_get (item,
		      "info", &info,
		      NULL);

	/* sending finished already */
	if (info == PK_INFO_ENUM_FINISHED) {
		egg_debug ("is finished ourelves");
		goto out;
	}

	/* same package, just info change */
	if (pk_package_equal_id (backend->priv->last_package, item) == 0) {
		egg_debug ("same package_id, ignoring");
		goto out;
	}

	/* emit the old package as finished */
	ret = pk_backend_package_emulate_finished (backend);
out:
	return ret;
}

/**
 * pk_backend_strsafe:
 * @text: The input text to make safe
 *
 * Replaces chars in the text that may be dangerous, or that may print
 * incorrectly. These chars include new lines, tabs and line feed, and are
 * replaced by spaces.
 *
 * Return value: the new string with no insane chars
 **/
static gchar *
pk_backend_strsafe (const gchar *text)
{
	gchar *text_safe;
	gboolean ret;
	const gchar *delimiters;

	if (text == NULL)
		return NULL;

	/* is valid UTF8? */
	ret = g_utf8_validate (text, -1, NULL);
	if (!ret) {
		egg_warning ("text '%s' was not valid UTF8!", text);
		return NULL;
	}

	/* rip out any insane characters */
	delimiters = "\\\f\r\t";
	text_safe = g_strdup (text);
	g_strdelimit (text_safe, delimiters, ' ');
	return text_safe;
}

/**
 * pk_backend_package:
 **/
gboolean
pk_backend_package (PkBackend *backend, PkInfoEnum info, const gchar *package_id, const gchar *summary)
{
	gchar *summary_safe = NULL;
	PkPackage *item = NULL;
	gboolean ret;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* check we are valid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		egg_warning ("package_id invalid and cannot be processed: %s", package_id);
		goto out;
	}

	/* replace unsafe chars */
	summary_safe = pk_backend_strsafe (summary);

	/* fix up available and installed when doing simulate roles */
	if (backend->priv->role == PK_ROLE_ENUM_SIMULATE_INSTALL_FILES ||
	    backend->priv->role == PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES ||
	    backend->priv->role == PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES ||
	    backend->priv->role == PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES) {
		if (info == PK_INFO_ENUM_AVAILABLE)
			info = PK_INFO_ENUM_INSTALLING;
		else if (info == PK_INFO_ENUM_INSTALLED)
			info = PK_INFO_ENUM_REMOVING;
	}

	/* create a new package object AFTER we emulate the info value */
	item = pk_package_new ();
	g_object_set (item,
		      "info", info,
		      "package-id", package_id,
		      "summary", summary_safe,
		      NULL);

	/* is it the same? */
	ret = (backend->priv->last_package != NULL && pk_package_equal (backend->priv->last_package, item));
	if (ret) {
		egg_debug ("skipping duplicate %s", package_id);
		ret = FALSE;
		goto out;
	}

	/* simulate the finish here when required */
	pk_backend_package_emulate_finished_for_package (backend, item);

	/* update the 'last' package */
	if (backend->priv->last_package != NULL)
		g_object_unref (backend->priv->last_package);
	backend->priv->last_package = g_object_ref (item);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		egg_warning ("already set error, cannot process: package %s", package_id);
		ret = FALSE;
		goto out;
	}

	/* we automatically set the transaction status for some PkInfoEnums if running
	 * in non-simultaneous transaction mode */
	if (!backend->priv->simultaneous) {
		egg_debug ("auto-setting status based on info %s", pk_info_enum_to_string (info));
		if (info == PK_INFO_ENUM_DOWNLOADING)
			pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
		else if (info == PK_INFO_ENUM_UPDATING)
			pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
		else if (info == PK_INFO_ENUM_INSTALLING)
			pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
		else if (info == PK_INFO_ENUM_REMOVING)
			pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
		else if (info == PK_INFO_ENUM_CLEANUP)
			pk_backend_set_status (backend, PK_STATUS_ENUM_CLEANUP);
		else if (info == PK_INFO_ENUM_OBSOLETING)
			pk_backend_set_status (backend, PK_STATUS_ENUM_OBSOLETE);
	}

	/* we've sent a package for this transaction */
	backend->priv->has_sent_package = TRUE;

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_PACKAGE], 0, item);
	pk_results_add_package (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (summary_safe);
	return ret;
}

/**
 * pk_backend_update_detail:
 **/
gboolean
pk_backend_update_detail (PkBackend *backend, const gchar *package_id,
			  const gchar *updates, const gchar *obsoletes,
			  const gchar *vendor_url, const gchar *bugzilla_url,
			  const gchar *cve_url, PkRestartEnum restart,
			  const gchar *update_text, const gchar	*changelog,
			  PkUpdateStateEnum state, const gchar *issued_text,
			  const gchar *updated_text)
{
	gchar *update_text_safe = NULL;
	PkUpdateDetail *item = NULL;
	GTimeVal timeval;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		egg_warning ("already set error, cannot process: update_detail %s", package_id);
		goto out;
	}

	/* check the dates are not empty */
	if (issued_text != NULL && issued_text[0] == '\0')
		issued_text = NULL;
	if (updated_text != NULL && updated_text[0] == '\0')
		updated_text = NULL;

	/* check the issued dates are valid */
	if (issued_text != NULL) {
		ret = g_time_val_from_iso8601 (issued_text, &timeval);
		if (!ret) {
			egg_warning ("failed to parse '%s'", issued_text);
			goto out;
		}
	}
	if (updated_text != NULL) {
		ret = g_time_val_from_iso8601 (updated_text, &timeval);
		if (!ret) {
			egg_warning ("failed to parse '%s'", updated_text);
			goto out;
		}
	}

	/* replace unsafe chars */
	update_text_safe = pk_backend_strsafe (update_text);

	/* form PkUpdateDetail struct */
	item = pk_update_detail_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "updates", updates,
		      "obsoletes", obsoletes,
		      "vendor-url", vendor_url,
		      "bugzilla-url", bugzilla_url,
		      "cve-url", cve_url,
		      "restart", restart,
		      "update-text", update_text_safe,
		      "changelog", changelog,
		      "state", state,
		      "issued", issued_text,
		      "updated", updated_text,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_UPDATE_DETAIL], 0, item);
	pk_results_add_update_detail (backend->priv->results, item);

	/* we parsed okay */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (update_text_safe);
	return ret;
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
pk_backend_require_restart (PkBackend *backend, PkRestartEnum restart, const gchar *package_id)
{
	gboolean ret = FALSE;
	PkRequireRestart *item = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		egg_warning ("already set error, cannot process: require-restart %s", pk_restart_enum_to_string (restart));
		goto out;
	}

	/* check we are valid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		egg_warning ("package_id invalid and cannot be processed: %s", package_id);
		goto out;
	}

	/* form PkRequireRestart struct */
	item = pk_require_restart_new ();
	g_object_set (item,
		      "restart", restart,
		      "package-id", package_id,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_REQUIRE_RESTART], 0, item);
	pk_results_add_require_restart (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	return ret;
}

/**
 * pk_backend_message:
 **/
gboolean
pk_backend_message (PkBackend *backend, PkMessageEnum message, const gchar *format, ...)
{
	gboolean ret = FALSE;
	va_list args;
	gchar *buffer = NULL;
	PkMessage *item = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error && message != PK_MESSAGE_ENUM_BACKEND_ERROR) {
		egg_warning ("already set error, cannot process: message %s", pk_message_enum_to_string (message));
		goto out;
	}

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	/* form PkMessage struct */
	item = pk_message_new ();
	g_object_set (item,
		      "type", message,
		      "details", buffer,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_MESSAGE], 0, item);
	pk_results_add_message (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	g_free (buffer);
	if (item != NULL)
		g_object_unref (item);
	return ret;
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
		egg_warning ("already set error, cannot process");
		return FALSE;
	}

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_CHANGE_TRANSACTION_DATA], 0, data);
	return TRUE;
}

/**
 * pk_backend_set_simultaneous_mode:
 **/
gboolean
pk_backend_set_simultaneous_mode (PkBackend *backend, gboolean simultaneous)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	backend->priv->simultaneous = simultaneous;
	if (simultaneous)
		egg_warning ("simultaneous mode is not well tested, use with caution");
	return TRUE;
}

/**
 * pk_backend_get_locale:
 *
 * Return value: session locale, e.g. en_GB
 **/
gchar *
pk_backend_get_locale (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return g_strdup (backend->priv->locale);
}

/**
 * pk_backend_set_locale:
 **/
gboolean
pk_backend_set_locale (PkBackend *backend, const gchar *code)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (code != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	egg_debug ("locale changed to %s", code);
	g_free (backend->priv->locale);
	backend->priv->locale = g_strdup (code);

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
	gchar *description_safe = NULL;
	PkDetails *item = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		egg_warning ("already set error, cannot process: details %s", package_id);
		goto out;
	}

	/* replace unsafe chars */
	description_safe = pk_backend_strsafe (description);

	/* form PkDetails struct */
	item = pk_details_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "license", license,
		      "group", group,
		      "description", description_safe,
		      "url", url,
		      "size", (guint64) size,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_DETAILS], 0, item);
	pk_results_add_details (backend->priv->results, item);

	/* we parsed okay */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (description_safe);
	return ret;
}

/**
 * pk_backend_files:
 *
 * package_id is NULL when we are using this as a calback from DownloadPackages
 **/
gboolean
pk_backend_files (PkBackend *backend, const gchar *package_id, const gchar *filelist)
{
	gboolean ret;
	PkFiles *item = NULL;
	gchar **files = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (filelist != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		egg_warning ("already set error, cannot process: files %s", package_id);
		return FALSE;
	}

	/* check we are valid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		egg_warning ("package_id invalid and cannot be processed: %s", package_id);
		goto out;
	}

	/* form PkFiles struct */
	files = g_strsplit (filelist, ";", -1);
	item = pk_files_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "files", files,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_FILES], 0, item);
	pk_results_add_files (backend->priv->results, item);

	/* success */
	backend->priv->download_files++;
	ret = TRUE;
out:
	g_strfreev (files);
	if (item != NULL)
		g_object_unref (item);
	return ret;
}

/**
 * pk_backend_distro_upgrade:
 **/
gboolean
pk_backend_distro_upgrade (PkBackend *backend, PkDistroUpgradeEnum state, const gchar *name, const gchar *summary)
{
	gboolean ret = FALSE;
	gchar *name_safe = NULL;
	gchar *summary_safe = NULL;
	PkDistroUpgrade *item = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (state != PK_DISTRO_UPGRADE_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (summary != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		egg_warning ("already set error, cannot process: distro-upgrade");
		goto out;
	}

	/* replace unsafe chars */
	name_safe = pk_backend_strsafe (name);
	summary_safe = pk_backend_strsafe (summary);

	/* form PkDistroUpgrade struct */
	item = pk_distro_upgrade_new ();
	g_object_set (item,
		      "state", state,
		      "name", name_safe,
		      "summary", summary_safe,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_DISTRO_UPGRADE], 0, item);
	pk_results_add_distro_upgrade (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (name_safe);
	g_free (summary_safe);
	return ret;
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
	gboolean ret = FALSE;
	PkRepoSignatureRequired *item = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (repository_name != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		egg_warning ("already set error, cannot process: repo-sig-reqd");
		goto out;
	}

	/* check we don't do this more than once */
	if (backend->priv->set_signature) {
		egg_warning ("already asked for a signature, cannot process");
		goto out;
	}

	/* form PkRepoSignatureRequired struct */
	item = pk_repo_signature_required_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "repository-name", repository_name,
		      "key-url", key_url,
		      "key-userid", key_userid,
		      "key-id", key_id,
		      "key-fingerprint", key_fingerprint,
		      "key-timestamp", key_timestamp,
		      "type", type,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_REPO_SIGNATURE_REQUIRED], 0, item);
	pk_results_add_repo_signature_required (backend->priv->results, item);

	/* success */
	backend->priv->set_signature = TRUE;
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	return ret;
}

/**
 * pk_backend_eula_required:
 **/
gboolean
pk_backend_eula_required (PkBackend *backend, const gchar *eula_id, const gchar *package_id,
			  const gchar *vendor_name, const gchar *license_agreement)
{
	PkEulaRequired *item = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (eula_id != NULL, FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (vendor_name != NULL, FALSE);
	g_return_val_if_fail (license_agreement != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		egg_warning ("already set error, cannot process: eula required");
		goto out;
	}

	/* check we don't do this more than once */
	if (backend->priv->set_eula) {
		egg_warning ("already asked for a signature, cannot process");
		goto out;
	}

	/* form PkEulaRequired struct */
	item = pk_eula_required_new ();
	g_object_set (item,
		      "eula-id", eula_id,
		      "package-id", package_id,
		      "vendor-name", vendor_name,
		      "license-agreement", license_agreement,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_EULA_REQUIRED], 0, item);
	pk_results_add_eula_required (backend->priv->results, item);

	/* success */
	backend->priv->set_eula = TRUE;
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	return ret;
}

/**
 * pk_backend_media_change_required:
 **/
gboolean
pk_backend_media_change_required (PkBackend *backend,
				  PkMediaTypeEnum media_type,
				  const gchar *media_id,
				  const gchar *media_text)
{
	PkMediaChangeRequired *item = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (media_id != NULL, FALSE);
	g_return_val_if_fail (media_text != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		egg_warning ("already set error, cannot process: media change required");
		goto out;
	}

	/* form PkMediaChangeRequired struct */
	item = pk_media_change_required_new ();
	g_object_set (item,
		      "media-type", media_type,
		      "media-id", media_id,
		      "media-text", media_text,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_MEDIA_CHANGE_REQUIRED], 0, item);
	pk_results_add_media_change_required (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	return ret;
}

/**
 * pk_backend_repo_detail:
 **/
gboolean
pk_backend_repo_detail (PkBackend *backend, const gchar *repo_id,
			const gchar *description, gboolean enabled)
{
	gchar *description_safe = NULL;
	PkRepoDetail *item = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (repo_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		egg_warning ("already set error, cannot process: repo-detail %s", repo_id);
		goto out;
	}

	/* replace unsafe chars */
	description_safe = pk_backend_strsafe (description);

	/* form PkRepoDetail struct */
	item = pk_repo_detail_new ();
	g_object_set (item,
		      "repo-id", repo_id,
		      "description", description,
		      "enabled", enabled,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_REPO_DETAIL], 0, item);
	pk_results_add_repo_detail (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (description_safe);
	return ret;
}

/**
 * pk_backend_category:
 **/
gboolean
pk_backend_category (PkBackend *backend, const gchar *parent_id, const gchar *cat_id, const gchar *name, const gchar *summary, const gchar *icon)
{
	gchar *summary_safe = NULL;
	PkCategory *item = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (cat_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		egg_warning ("already set error, cannot process: category %s", cat_id);
		goto out;
	}

	/* replace unsafe chars */
	summary_safe = pk_backend_strsafe (summary);

	/* form PkCategory struct */
	item = pk_category_new ();
	g_object_set (item,
		      "parent-id", parent_id,
		      "cat-id", cat_id,
		      "name", name,
		      "summary", summary,
		      "icon", icon,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_CATEGORY], 0, item);
	pk_results_add_category (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (summary_safe);
	return ret;
}

/**
 * pk_backend_repo_list_changed:
 **/
gboolean
pk_backend_repo_list_changed (PkBackend *backend)
{
	PkNotify *notify;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	notify = pk_notify_new ();
	pk_notify_repo_list_changed (notify);
	g_object_unref (notify);

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
	PkMessage *item = NULL;

	/* check we have not already finished */
	if (backend->priv->finished) {
		egg_warning ("consistency error");
		egg_debug_backtrace ();
		goto out;
	}

	/* form PkMessage struct */
	item = pk_message_new ();
	g_object_set (item,
		      "type", PK_MESSAGE_ENUM_BACKEND_ERROR,
		      "details", "ErrorCode() has to be followed with Finished()!",
		      NULL);

	/* warn the backend developer that they've done something worng
	 * - we can't use pk_backend_message here as we have already set
	 * backend->priv->set_error to TRUE and hence the message would be ignored */
	g_signal_emit (backend, signals[SIGNAL_MESSAGE], 0, item);

	pk_backend_finished (backend);
out:
	if (item != NULL)
		g_object_unref (item);
	backend->priv->signal_error_timeout = 0;
	return FALSE;
}

/**
 * pk_backend_error_code_is_need_untrusted:
 **/
static gboolean
pk_backend_error_code_is_need_untrusted (PkErrorEnum error_code)
{
	gboolean ret = FALSE;
	switch (error_code) {
		case PK_ERROR_ENUM_GPG_FAILURE:
		case PK_ERROR_ENUM_BAD_GPG_SIGNATURE:
		case PK_ERROR_ENUM_MISSING_GPG_SIGNATURE:
		case PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED:
		case PK_ERROR_ENUM_CANNOT_UPDATE_REPO_UNSIGNED:
			ret = TRUE;
			break;
		default:
			break;
	}
	return ret;
}

/**
 * pk_backend_error_code:
 **/
gboolean
pk_backend_error_code (PkBackend *backend, PkErrorEnum error_code, const gchar *format, ...)
{
	va_list args;
	gchar *buffer;
	gboolean ret = TRUE;
	gboolean need_untrusted;
	PkError *item = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	va_start (args, format);
	buffer = g_strdup_vprintf (format, args);
	va_end (args);

	/* check we are not doing Init() */
	if (backend->priv->during_initialize) {
		egg_warning ("set during init: %s", buffer);
		ret = FALSE;
		goto out;
	}

	/* did we set a duplicate error? */
	if (backend->priv->set_error) {
		egg_warning ("More than one error emitted! You tried to set '%s'", buffer);
		ret = FALSE;
		goto out;
	}
	backend->priv->set_error = TRUE;

	/* we only allow a short time to send finished after error_code */
	backend->priv->signal_error_timeout = g_timeout_add (PK_BACKEND_FINISHED_ERROR_TIMEOUT,
							     pk_backend_error_timeout_delay_cb, backend);

	/* some error codes have a different exit code */
	need_untrusted = pk_backend_error_code_is_need_untrusted (error_code);
	if (need_untrusted)
		pk_backend_set_exit_code (backend, PK_EXIT_ENUM_NEED_UNTRUSTED);
	else
		pk_backend_set_exit_code (backend, PK_EXIT_ENUM_FAILED);

	/* form PkError struct */
	item = pk_error_new ();
	g_object_set (item,
		      "code", error_code,
		      "details", buffer,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_ERROR_CODE], 0, item);
	pk_results_set_error_code (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
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
	g_return_val_if_fail (backend->priv->desc != NULL, FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error && allow_cancel) {
		egg_warning ("already set error, cannot process: allow-cancel %i", allow_cancel);
		return FALSE;
	}

	/* same as last state? */
	if (backend->priv->allow_cancel == (PkHintEnum) allow_cancel) {
		egg_debug ("ignoring same allow-cancel state");
		return FALSE;
	}

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_ALLOW_CANCEL], 0, allow_cancel);
	backend->priv->allow_cancel = allow_cancel;
	return TRUE;
}

/**
 * pk_backend_get_allow_cancel:
 **/
gboolean
pk_backend_get_allow_cancel (PkBackend *backend)
{
	gboolean allow_cancel = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* return FALSE if we never set state */
	if (backend->priv->allow_cancel != PK_HINT_ENUM_UNSET)
		allow_cancel = backend->priv->allow_cancel;

	return allow_cancel;
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
		egg_warning ("cannot set role more than once, already %s",
			    pk_role_enum_to_string (backend->priv->role));
		return FALSE;
	}

	/* reset the timer */
	pk_time_reset (backend->priv->time);

	egg_debug ("setting role to %s", pk_role_enum_to_string (role));
	backend->priv->role = role;
	backend->priv->status = PK_STATUS_ENUM_WAIT;
	return TRUE;
}

/**
 * pk_backend_set_exit_code:
 *
 * Should only be used internally, or from PkRunner when setting CANCELLED.
 **/
gboolean
pk_backend_set_exit_code (PkBackend *backend, PkExitEnum exit_enum)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_ROLE_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	if (backend->priv->exit != PK_EXIT_ENUM_UNKNOWN) {
		egg_warning ("already set exit status: old=%s, new=%s",
			    pk_exit_enum_to_string (backend->priv->exit),
			    pk_exit_enum_to_string (exit_enum));
		egg_debug_backtrace ();
		return FALSE;
	}

	/* new value */
	backend->priv->exit = exit_enum;
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
	if (backend->priv->exit == PK_EXIT_ENUM_UNKNOWN)
		pk_backend_set_exit_code (backend, PK_EXIT_ENUM_SUCCESS);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_FINISHED], 0, backend->priv->exit);
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
		egg_warning ("finished during init");
		return FALSE;
	}

	/* safe to check now */
	g_return_val_if_fail (backend->priv->locked != FALSE, FALSE);

	/* find out what we just did */
	role_text = pk_role_enum_to_string (backend->priv->role);
	egg_debug ("finished role %s", role_text);

	/* are we trying to finish in init? */
	if (backend->priv->during_initialize) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "%s can't call pk_backend_finished in backend_initialize!", role_text);
		return FALSE;
	}

	/* check we have not already finished */
	if (backend->priv->finished) {
		egg_warning ("already finished");
		return FALSE;
	}

	/* check we got a Package() else the UI will suck */
	if (!backend->priv->set_error &&
	    !backend->priv->has_sent_package &&
	    (backend->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	     backend->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES ||
	     backend->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "Backends should send a Package() for %s!", role_text);
	}

	/* ensure the same number of ::Files() were sent as packages for DownloadPackages */
	if (!backend->priv->set_error &&
	    backend->priv->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES &&
	    backend->priv->download_files == 0) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "Backends should send multiple Files() for each package_id!");
	}

	/* if we set an error code notifier, clear */
	if (backend->priv->signal_error_timeout != 0) {
		g_source_remove (backend->priv->signal_error_timeout);
		backend->priv->signal_error_timeout = 0;
	}

	/* check we sent at least one status calls */
	if (backend->priv->set_error == FALSE &&
	    backend->priv->status == PK_STATUS_ENUM_SETUP) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "Backends should send status <value> signals for %s!", role_text);
		egg_warning ("GUI will remain unchanged!");
	}

	/* emulate the last finished package if not done already */
	pk_backend_package_emulate_finished (backend);

	/* make any UI insensitive */
	pk_backend_set_allow_cancel (backend, FALSE);

	/* mark as finished for the UI that might only be watching status */
	pk_backend_set_status (backend, PK_STATUS_ENUM_FINISHED);

	/* we can't ever be re-used */
	backend->priv->finished = TRUE;

	/* we have to run this idle as the command may finish before the transaction
	 * has been sent to the client. I love async... */
	egg_debug ("adding finished %p to timeout loop", backend);
	backend->priv->signal_finished = g_timeout_add (PK_BACKEND_FINISHED_TIMEOUT_GRACE, pk_backend_finished_delay, backend);
	return TRUE;
}

/**
 * pk_backend_bool_to_string:
 */
const gchar *
pk_backend_bool_to_string (gboolean value)
{
	if (value)
		return "yes";
	return "no";
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
	if (backend->priv->transaction_id != NULL)
		egg_warning ("only valid when we have a running transaction");
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
	if (state == PK_NETWORK_ENUM_ONLINE ||
	    state == PK_NETWORK_ENUM_MOBILE ||
	    state == PK_NETWORK_ENUM_WIFI ||
	    state == PK_NETWORK_ENUM_WIRED)
		return TRUE;
	return FALSE;
}

/**
 * pk_backend_use_background:
 **/
gboolean
pk_backend_use_background (PkBackend *backend)
{
	gboolean ret;

	/* we're watching the GUI, do as fast as possible */
	if (backend->priv->interactive == PK_HINT_ENUM_TRUE)
		return FALSE;

	/* check we are allowed */
	ret = pk_conf_get_bool (backend->priv->conf, "UseIdleBandwidth");
	if (!ret)
		return FALSE;

	/* the session has set it one way or the other */
	if (backend->priv->background == PK_HINT_ENUM_TRUE)
		return TRUE;
	if (backend->priv->background == PK_HINT_ENUM_FALSE)
		return FALSE;

	/* use a metric to try to guess a correct value */
	if (backend->priv->role == PK_ROLE_ENUM_GET_UPDATES ||
	    backend->priv->role == PK_ROLE_ENUM_REFRESH_CACHE)
		return TRUE;
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
		egg_warning ("already has thread");
		return FALSE;
	}
	backend->priv->thread = g_thread_create ((GThreadFunc) func, backend, FALSE, NULL);
	if (backend->priv->thread == NULL) {
		egg_warning ("failed to create thread");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_backend_get_name:
 **/
gchar *
pk_backend_get_name (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->desc != NULL, NULL);
	g_return_val_if_fail (backend->priv->locked != FALSE, NULL);
	return g_strdup (backend->priv->name);
}

/**
 * pk_backend_get_description:
 **/
gchar *
pk_backend_get_description (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->desc != NULL, NULL);
	g_return_val_if_fail (backend->priv->locked != FALSE, NULL);
	return g_strdup (backend->priv->desc->description);
}

/**
 * pk_backend_get_author:
 **/
gchar *
pk_backend_get_author (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->desc != NULL, NULL);
	g_return_val_if_fail (backend->priv->locked != FALSE, NULL);
	return g_strdup (backend->priv->desc->author);
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

	egg_debug ("eula_id %s", eula_id);
	present = g_hash_table_lookup (backend->priv->eulas, eula_id);
	if (present != NULL) {
		egg_debug ("already added %s to accepted list", eula_id);
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
	if (present != NULL)
		return TRUE;
	return FALSE;
}

/**
 * pk_backend_is_eula_valid:
 */
gchar *
pk_backend_get_accepted_eula_string (PkBackend *backend)
{
	GString *string;
	gchar *result = NULL;
	GList *keys = NULL;
	GList *l;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* optimise for the common case */
	if (g_hash_table_size (backend->priv->eulas) == 0)
		goto out;

	/* create a string of the accepted EULAs */
	string = g_string_new ("");
	keys = g_hash_table_get_keys (backend->priv->eulas);
	for (l=keys; l != NULL; l=l->next)
		g_string_append_printf (string, "%s;", (const gchar *) l->data);

	/* remove the trailing ';' */
	g_string_set_size (string, string->len -1);
	result = g_string_free (string, FALSE);
out:
	g_list_free (keys);
	return result;
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
		egg_warning ("already set");
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
	egg_debug ("config file changed");
	backend->priv->file_changed_func (backend, backend->priv->file_changed_data);
}

/**
 * pk_backend_get_property:
 **/
static void
pk_backend_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkBackend *backend = PK_BACKEND (object);
	PkBackendPrivate *priv = backend->priv;

	switch (prop_id) {
	case PROP_BACKGROUND:
		g_value_set_uint (value, priv->background);
		break;
	case PROP_INTERACTIVE:
		g_value_set_uint (value, priv->interactive);
		break;
	case PROP_STATUS:
		g_value_set_uint (value, priv->status);
		break;
	case PROP_ROLE:
		g_value_set_uint (value, priv->role);
		break;
	case PROP_TRANSACTION_ID:
		g_value_set_string (value, priv->transaction_id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_backend_set_property:
 **/
static void
pk_backend_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkBackend *backend = PK_BACKEND (object);
	PkBackendPrivate *priv = backend->priv;

	switch (prop_id) {
	case PROP_BACKGROUND:
		priv->background = g_value_get_uint (value);
		break;
	case PROP_INTERACTIVE:
		priv->interactive = g_value_get_uint (value);
		break;
	case PROP_STATUS:
		priv->status = g_value_get_uint (value);
		break;
	case PROP_ROLE:
		priv->role = g_value_get_uint (value);
		break;
	case PROP_TRANSACTION_ID:
		g_free (priv->transaction_id);
		priv->transaction_id = g_value_dup_string (value);
		egg_debug ("setting backend tid as %s", priv->transaction_id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
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

	pk_backend_reset (backend);
	g_free (backend->priv->proxy_http);
	g_free (backend->priv->proxy_ftp);
	g_free (backend->priv->name);
	g_free (backend->priv->locale);
	g_free (backend->priv->transaction_id);
	g_object_unref (backend->priv->results);
	g_object_unref (backend->priv->time);
	g_object_unref (backend->priv->network);
	g_object_unref (backend->priv->store);
	g_object_unref (backend->priv->conf);
	g_hash_table_destroy (backend->priv->eulas);

	if (backend->priv->handle != NULL)
		g_module_close (backend->priv->handle);
	egg_debug ("parent_class->finalize");
	G_OBJECT_CLASS (pk_backend_parent_class)->finalize (object);
}

/**
 * pk_backend_class_init:
 **/
static void
pk_backend_class_init (PkBackendClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_backend_finalize;
	object_class->get_property = pk_backend_get_property;
	object_class->set_property = pk_backend_set_property;

	/**
	 * PkBackend:background:
	 */
	pspec = g_param_spec_uint ("background", NULL, NULL,
				   PK_HINT_ENUM_FALSE, PK_HINT_ENUM_UNSET, PK_HINT_ENUM_UNSET,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_BACKGROUND, pspec);

	/**
	 * PkBackend:interactive:
	 */
	pspec = g_param_spec_uint ("interactive", NULL, NULL,
				   PK_HINT_ENUM_FALSE, PK_HINT_ENUM_UNSET, PK_HINT_ENUM_UNSET,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_INTERACTIVE, pspec);

	/**
	 * PkBackend:status:
	 */
	pspec = g_param_spec_uint ("status", NULL, NULL,
				   0, G_MAXUINT, PK_STATUS_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_STATUS, pspec);

	/**
	 * PkBackend:role:
	 */
	pspec = g_param_spec_uint ("role", NULL, NULL,
				   0, G_MAXUINT, PK_STATUS_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ROLE, pspec);

	/**
	 * PkBackend:transaction-id:
	 */
	pspec = g_param_spec_string ("transaction-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_TRANSACTION_ID, pspec);

	/* properties */
	signals[SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_PROGRESS_CHANGED] =
		g_signal_new ("progress-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT_UINT_UINT,
			      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
	signals[SIGNAL_CHANGE_TRANSACTION_DATA] =
		g_signal_new ("change-transaction-data",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
	signals[SIGNAL_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_ALLOW_CANCEL] =
		g_signal_new ("allow-cancel",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	/* objects */
	signals[SIGNAL_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_MESSAGE] =
		g_signal_new ("message",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_DETAILS] =
		g_signal_new ("details",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_FILES] =
		g_signal_new ("files",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_DISTRO_UPGRADE] =
		g_signal_new ("distro-upgrade",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_REPO_SIGNATURE_REQUIRED] =
		g_signal_new ("repo-signature-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_EULA_REQUIRED] =
		g_signal_new ("eula-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_MEDIA_CHANGE_REQUIRED] =
		g_signal_new ("media-change-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_REPO_DETAIL] =
		g_signal_new ("repo-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_CATEGORY] =
		g_signal_new ("category",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	g_type_class_add_private (klass, sizeof (PkBackendPrivate));
}

/**
 * pk_backend_reset:
 **/
gboolean
pk_backend_reset (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* we can't reset when we are running */
	if (backend->priv->status == PK_STATUS_ENUM_RUNNING) {
		egg_warning ("cannot reset %s when running", backend->priv->transaction_id);
		return FALSE;
	}

	/* do finish now, as we might be unreffing quickly */
	if (backend->priv->signal_finished != 0) {
		g_source_remove (backend->priv->signal_finished);
		egg_debug ("doing unref quickly delay");
		pk_backend_finished_delay (backend);
	}

	/* if we set an error code notifier, clear */
	if (backend->priv->signal_error_timeout != 0) {
		g_source_remove (backend->priv->signal_error_timeout);
		backend->priv->signal_error_timeout = 0;
	}

	if (backend->priv->last_package != NULL) {
		g_object_unref (backend->priv->last_package);
		backend->priv->last_package = NULL;
	}
	backend->priv->set_error = FALSE;
	backend->priv->set_signature = FALSE;
	backend->priv->set_eula = FALSE;
	backend->priv->finished = FALSE;
	backend->priv->has_sent_package = FALSE;
	backend->priv->download_files = 0;
	backend->priv->thread = NULL;
	backend->priv->last_package = NULL;
	backend->priv->allow_cancel = PK_HINT_ENUM_UNSET;
	backend->priv->status = PK_STATUS_ENUM_UNKNOWN;
	backend->priv->exit = PK_EXIT_ENUM_UNKNOWN;
	backend->priv->role = PK_ROLE_ENUM_UNKNOWN;
	backend->priv->last_remaining = 0;
	backend->priv->last_percentage = PK_BACKEND_PERCENTAGE_DEFAULT;
	backend->priv->last_subpercentage = PK_BACKEND_PERCENTAGE_INVALID;
	pk_store_reset (backend->priv->store);
	pk_time_reset (backend->priv->time);

	/* unref then create rather then set zero size, as another object
	 * might have a reference on the data */
	g_object_unref (backend->priv->results);
	backend->priv->results = pk_results_new ();

	return TRUE;
}

/**
 * pk_backend_cancel:
 */
void
pk_backend_cancel (PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));

	/* call into the backend */
	backend->priv->desc->cancel (backend);

	/* set an error if the backend didn't do it for us */
	if (!backend->priv->set_error)
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "transaction was cancelled");
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_store_set_string (backend->priv->store, "directory", directory);
	backend->priv->desc->download_packages (backend, package_ids, directory);
}

/**
 * pk_pk_backend_get_categories:
 */
void
pk_backend_get_categories (PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	backend->priv->desc->get_categories (backend);
}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_store_set_bool (backend->priv->store, "recursive", recursive);
	backend->priv->desc->get_depends (backend, filters, package_ids, recursive);
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	backend->priv->desc->get_details (backend, package_ids);
}

/**
 * pk_backend_get_distro_upgrades:
 */
void
pk_backend_get_distro_upgrades (PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	backend->priv->desc->get_distro_upgrades (backend);
}

/**
 * pk_backend_get_files:
 */
void
pk_backend_get_files (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	backend->priv->desc->get_files (backend, package_ids);
}

/**
 * pk_backend_get_requires:
 */
void
pk_backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_store_set_bool (backend->priv->store, "recursive", recursive);
	backend->priv->desc->get_requires (backend, filters, package_ids, recursive);
}

/**
 * pk_backend_get_update_detail:
 */
void
pk_backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	backend->priv->desc->get_update_detail (backend, package_ids);
}

/**
 * pk_backend_get_updates:
 */
void
pk_backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_uint (backend->priv->store, "filters", filters);
	backend->priv->desc->get_updates (backend, filters);
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_bool (backend->priv->store, "only_trusted", only_trusted);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	backend->priv->desc->install_packages (backend, only_trusted, package_ids);
}

/**
 * pk_backend_install_signature:
 */
void
pk_backend_install_signature (PkBackend *backend, PkSigTypeEnum type, const gchar *key_id, const gchar *package_id)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_string (backend->priv->store, "key_id", key_id);
	pk_store_set_string (backend->priv->store, "package_id", package_id);
	backend->priv->desc->install_signature (backend, type, key_id, package_id);
}

/**
 * pk_backend_install_files:
 */
void
pk_backend_install_files (PkBackend *backend, gboolean only_trusted, gchar **full_paths)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_bool (backend->priv->store, "only_trusted", only_trusted);
	pk_store_set_strv (backend->priv->store, "full_paths", full_paths);
	backend->priv->desc->install_files (backend, only_trusted, full_paths);
}

/**
 * pk_backend_refresh_cache:
 */
void
pk_backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_bool (backend->priv->store, "force", force);
	backend->priv->desc->refresh_cache (backend, force);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_store_set_bool (backend->priv->store, "allow_deps", allow_deps);
	pk_store_set_bool (backend->priv->store, "autoremove", autoremove);
	backend->priv->desc->remove_packages (backend, package_ids, allow_deps, autoremove);
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBitfield filters, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	backend->priv->desc->resolve (backend, filters, package_ids);
}

/**
 * pk_backend_rollback:
 */
void
pk_backend_rollback (PkBackend *backend, const gchar *transaction_id)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_string (backend->priv->store, "transaction_id", transaction_id);
	backend->priv->desc->rollback (backend, transaction_id);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "search", values);
	backend->priv->desc->search_details (backend, filters, values);
}

/**
 * pk_backend_search_files:
 */
void
pk_backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "search", values);
	backend->priv->desc->search_files (backend, filters, values);
}

/**
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "search", values);
	backend->priv->desc->search_groups (backend, filters, values);
}

/**
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "search", values);
	backend->priv->desc->search_names (backend, filters, values);
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_bool (backend->priv->store, "only_trusted", only_trusted);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	backend->priv->desc->update_packages (backend, only_trusted, package_ids);
}

/**
 * pk_backend_update_system:
 */
void
pk_backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_bool (backend->priv->store, "only_trusted", only_trusted);
	backend->priv->desc->update_system (backend, only_trusted);
}

/**
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_uint (backend->priv->store, "filters", filters);
	backend->priv->desc->get_repo_list (backend, filters);
}

/**
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend, const gchar *repo_id, gboolean enabled)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_string (backend->priv->store, "repo_id", repo_id);
	pk_store_set_bool (backend->priv->store, "enabled", enabled);
	backend->priv->desc->repo_enable (backend, repo_id, enabled);
}

/**
 * pk_backend_repo_set_data:
 */
void
pk_backend_repo_set_data (PkBackend *backend, const gchar *repo_id, const gchar *parameter, const gchar *value)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_string (backend->priv->store, "repo_id", repo_id);
	pk_store_set_string (backend->priv->store, "parameter", parameter);
	pk_store_set_string (backend->priv->store, "value", value);
	backend->priv->desc->repo_set_data (backend, repo_id, parameter, value);
}

/**
 * pk_backend_what_provides:
 */
void
pk_backend_what_provides (PkBackend *backend, PkBitfield filters, PkProvidesEnum provides, gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_uint (backend->priv->store, "provides", provides);
	pk_store_set_strv (backend->priv->store, "search", values);
	backend->priv->desc->what_provides (backend, filters, provides, values);
}

/**
 * pk_backend_get_packages:
 */
void
pk_backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_uint (backend->priv->store, "filters", filters);
	backend->priv->desc->get_packages (backend, filters);
}

/**
 * pk_backend_simulate_install_files:
 */
void
pk_backend_simulate_install_files (PkBackend *backend, gchar **full_paths)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_strv (backend->priv->store, "full_paths", full_paths);
	backend->priv->desc->simulate_install_files (backend, full_paths);
}

/**
 * pk_backend_simulate_install_packages:
 */
void
pk_backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	backend->priv->desc->simulate_install_packages (backend, package_ids);
}

/**
 * pk_backend_simulate_remove_packages:
 */
void
pk_backend_simulate_remove_packages (PkBackend *backend, gchar **package_ids, gboolean	 autoremove)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_store_set_bool (backend->priv->store, "autoremove", autoremove);
	backend->priv->desc->simulate_remove_packages (backend, package_ids, autoremove);
}

/**
 * pk_backend_simulate_update_packages:
 */
void
pk_backend_simulate_update_packages (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	backend->priv->desc->simulate_update_packages (backend, package_ids);
}

/**
 * pk_backend_init:
 **/
static void
pk_backend_init (PkBackend *backend)
{
	PkConf *conf;

	backend->priv = PK_BACKEND_GET_PRIVATE (backend);
	backend->priv->handle = NULL;
	backend->priv->name = NULL;
	backend->priv->locale = NULL;
	backend->priv->transaction_id = NULL;
	backend->priv->proxy_http = NULL;
	backend->priv->proxy_ftp = NULL;
	backend->priv->file_changed_func = NULL;
	backend->priv->file_changed_data = NULL;
	backend->priv->last_package = NULL;
	backend->priv->locked = FALSE;
	backend->priv->signal_finished = 0;
	backend->priv->speed = 0;
	backend->priv->signal_error_timeout = 0;
	backend->priv->during_initialize = FALSE;
	backend->priv->simultaneous = FALSE;
	backend->priv->roles = 0;
	backend->priv->conf = pk_conf_new ();
	backend->priv->results = pk_results_new ();
	backend->priv->store = pk_store_new ();
	backend->priv->time = pk_time_new ();
	backend->priv->network = pk_network_new ();
	backend->priv->eulas = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	/* monitor config files for changes */
	backend->priv->file_monitor = pk_file_monitor_new ();
	g_signal_connect (backend->priv->file_monitor, "file-changed",
			  G_CALLBACK (pk_backend_file_monitor_changed_cb), backend);

	/* do we use time estimation? */
	conf = pk_conf_new ();
	backend->priv->use_time = pk_conf_get_bool (conf, "UseRemainingTimeEstimation");
	g_object_unref (conf);

	pk_backend_reset (backend);
}

/**
 * pk_backend_new:
 * Return value: A new backend class backend.
 **/
PkBackend *
pk_backend_new (void)
{
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
#ifdef EGG_TEST
#include "egg-test.h"
#include <glib/gstdio.h>

static guint number_messages = 0;
static guint number_packages = 0;

/**
 * pk_backend_test_message_cb:
 **/
static void
pk_backend_test_message_cb (PkBackend *backend, PkMessageEnum message, const gchar *details, gpointer data)
{
	egg_debug ("details=%s", details);
	number_messages++;
}

/**
 * pk_backend_test_finished_cb:
 **/
static void
pk_backend_test_finished_cb (PkBackend *backend, PkExitEnum exit, EggTest *test)
{
	egg_test_loop_quit (test);
}

/**
 * pk_backend_test_watch_file_cb:
 **/
static void
pk_backend_test_watch_file_cb (PkBackend *backend, gpointer data)
{
	EggTest *test = (EggTest *) data;
	egg_test_loop_quit (test);
}

static gboolean
pk_backend_test_func_true (PkBackend *backend)
{
	g_usleep (1000*1000);
	/* trigger duplicate test */
	pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE, "vips-doc;7.12.4-2.fc8;noarch;linva", "The vips documentation package.");
	pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE, "vips-doc;7.12.4-2.fc8;noarch;linva", "The vips documentation package.");
	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
pk_backend_test_func_immediate_false (PkBackend *backend)
{
	pk_backend_finished (backend);
	return FALSE;
}

/**
 * pk_backend_test_package_cb:
 **/
static void
pk_backend_test_package_cb (PkBackend *backend, PkPackage *item, EggTest *test)
{
	egg_debug ("package:%s", pk_package_get_id (item));
	number_packages++;
}

void
pk_backend_test (EggTest *test)
{
	PkBackend *backend;
	PkConf *conf;
	gchar *text;
	gchar *text_safe;
	gboolean ret;
	const gchar *filename;
	gboolean developer_mode;

	if (!egg_test_start (test, "PkBackend"))
		return;

	/************************************************************
	 ****************       REPLACE CHARS      ******************
	 ************************************************************/
	egg_test_title (test, "test replace unsafe (okay)");
	text_safe = pk_backend_strsafe ("Richard Hughes");
	if (g_strcmp0 (text_safe, "Richard Hughes") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the replace unsafe '%s'", text_safe);
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "test replace UTF8 unsafe (okay)");
	text_safe = pk_backend_strsafe ("Gölas");
	if (g_strcmp0 (text_safe, "Gölas") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the replace unsafe '%s'", text_safe);
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "test replace unsafe (one invalid)");
	text_safe = pk_backend_strsafe ("Richard\rHughes");
	if (g_strcmp0 (text_safe, "Richard Hughes") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the replace unsafe '%s'", text_safe);
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "test replace unsafe (multiple invalid)");
	text_safe = pk_backend_strsafe (" Richard\rHughes\f");
	if (g_strcmp0 (text_safe, " Richard Hughes ") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the replace unsafe '%s'", text_safe);
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "get an backend");
	backend = pk_backend_new ();
	if (backend != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/* connect */
	g_signal_connect (backend, "package",
			  G_CALLBACK (pk_backend_test_package_cb), test);

	/************************************************************/
	egg_test_title (test, "create a config file");
	filename = "/tmp/dave";
	ret = g_file_set_contents (filename, "foo", -1, NULL);
	if (ret) {
		egg_test_success (test, "set contents");
	} else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "set up a watch file on a config file");
	ret = pk_backend_watch_file (backend, filename, pk_backend_test_watch_file_cb, test);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "eula valid");

	/************************************************************/
	egg_test_title (test, "change the config file");
	ret = g_file_set_contents (filename, "bar", -1, NULL);
	if (ret) {
		egg_test_success (test, "set contents");
	} else
		egg_test_failed (test, NULL);

	/* wait for config file change */
	egg_test_loop_wait (test, 2000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "delete the config file");
	ret = g_unlink (filename);
	egg_test_assert (test, !ret);

	g_signal_connect (backend, "message", G_CALLBACK (pk_backend_test_message_cb), NULL);
	g_signal_connect (backend, "finished", G_CALLBACK (pk_backend_test_finished_cb), test);

	/************************************************************/
	egg_test_title (test, "get eula that does not exist");
	ret = pk_backend_is_eula_valid (backend, "license_foo");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "eula valid");

	/************************************************************/
	egg_test_title (test, "accept eula");
	ret = pk_backend_accept_eula (backend, "license_foo");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "eula was not accepted");

	/************************************************************/
	egg_test_title (test, "get eula that does exist");
	ret = pk_backend_is_eula_valid (backend, "license_foo");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "eula valid");

	/************************************************************/
	egg_test_title (test, "accept eula (again)");
	ret = pk_backend_accept_eula (backend, "license_foo");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "eula was accepted twice");

	/************************************************************/
	egg_test_title (test, "load an invalid backend");
	ret = pk_backend_set_name (backend, "invalid");
	if (ret == FALSE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "try to load a valid backend");
	ret = pk_backend_set_name (backend, "dummy");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "load an valid backend again");
	ret = pk_backend_set_name (backend, "dummy");
	if (ret == FALSE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "loaded twice");

	/************************************************************/
	egg_test_title (test, "lock an valid backend");
	ret = pk_backend_lock (backend);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to lock");

	/************************************************************/
	egg_test_title (test, "lock a backend again");
	ret = pk_backend_lock (backend);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "locked twice should succeed");

	/************************************************************/
	egg_test_title (test, "check we are out of init");
	if (backend->priv->during_initialize == FALSE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "not out of init");

	/************************************************************/
	egg_test_title (test, "get backend name");
	text = pk_backend_get_name (backend);
	if (g_strcmp0 (text, "dummy") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid name %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "unlock an valid backend");
	ret = pk_backend_unlock (backend);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to unlock");

	/************************************************************/
	egg_test_title (test, "unlock an valid backend again");
	ret = pk_backend_unlock (backend);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "unlocked twice, should succeed");

	/************************************************************/
	egg_test_title (test, "check we are not finished");
	if (backend->priv->finished == FALSE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "we did not clear finish!");

	/************************************************************/
	egg_test_title (test, "check we have no error");
	if (backend->priv->set_error == FALSE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "an error has already been set");

	/************************************************************/
	egg_test_title (test, "lock again");
	ret = pk_backend_lock (backend);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to unlock");

	/************************************************************/
	egg_test_title (test, "wait for a thread to return true");
	ret = pk_backend_thread_create (backend, pk_backend_test_func_true);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wait for a thread failed");

	/* wait for Finished */
	egg_test_loop_wait (test, 2000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "check duplicate filter");
	if (number_packages == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong number of packages: %i", number_packages);

	/* reset */
	pk_backend_reset (backend);

	/************************************************************/
	egg_test_title (test, "wait for a thread to return false (straight away)");
	ret = pk_backend_thread_create (backend, pk_backend_test_func_immediate_false);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "returned false!");

	/* wait for Finished */
	egg_test_loop_wait (test, PK_BACKEND_FINISHED_TIMEOUT_GRACE + 100);
	egg_test_loop_check (test);

	/************************************************************/
	pk_backend_reset (backend);
	pk_backend_error_code (backend, PK_ERROR_ENUM_GPG_FAILURE, "test error");

	/* wait for finished */
	egg_test_loop_wait (test, PK_BACKEND_FINISHED_ERROR_TIMEOUT + 400);
	egg_test_loop_check (test);

	/************************************************************
	 ****************     CANCEL TRISTATE      ******************
	 ************************************************************/
	egg_test_title (test, "get allow cancel after reset");
	pk_backend_reset (backend);
	ret = pk_backend_get_allow_cancel (backend);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "set allow cancel TRUE");
	ret = pk_backend_set_allow_cancel (backend, TRUE);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "set allow cancel TRUE (repeat)");
	ret = pk_backend_set_allow_cancel (backend, TRUE);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "set allow cancel FALSE");
	ret = pk_backend_set_allow_cancel (backend, FALSE);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "set allow cancel FALSE (after reset)");
	pk_backend_reset (backend);
	ret = pk_backend_set_allow_cancel (backend, FALSE);
	egg_test_assert (test, ret);

	/* if running in developer mode, then expect a Message */
	conf = pk_conf_new ();
	developer_mode = pk_conf_get_bool (conf, "DeveloperMode");
	g_object_unref (conf);
	if (developer_mode) {
		/************************************************************/
		egg_test_title (test, "check we enforce finished after error_code");
		if (number_messages == 1)
			egg_test_success (test, NULL);
		else
			egg_test_failed (test, "we messaged %i times!", number_messages);
	}

	g_object_unref (backend);

	egg_test_end (test);
}
#endif

