/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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
#include <pk-package-id.h>
#include <pk-enum.h>

#include "pk-debug.h"
#include "pk-backend-internal.h"
#include "pk-marshal.h"
#include "pk-enum.h"
#include "pk-spawn.h"
#include "pk-network.h"
#include "pk-thread-list.h"

#define PK_BACKEND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND, PkBackendPrivate))
#define PK_BACKEND_PERCENTAGE_INVALID	101

struct _PkBackendPrivate
{
	GModule			*handle;
	gchar			*name;
	PkStatusEnum		 role; /* this never changes for the lifetime of a transaction */
	PkStatusEnum		 status; /* this changes */
	gboolean		 xcached_force;
	gboolean		 xcached_allow_deps;
	gchar			*xcached_package_id;
	gchar			*xcached_full_path;
	gchar			*xcached_filter;
	gchar			*xcached_search;
	PkExitEnum		 exit;
	GTimer			*timer;
	PkSpawn			*spawn;
	gboolean		 is_killable;
	gboolean		 during_initialize;
	gboolean		 assigned;
	gboolean		 set_error;
	PkNetwork		*network;
	/* needed for gui coldplugging */
	guint			 last_percentage;
	guint			 last_subpercentage;
	gchar			*last_package;
	PkThreadList		*thread_list;
	gulong			 signal_finished;
	gulong			 signal_stdout;
	gulong			 signal_stderr;
};

enum {
	PK_BACKEND_TRANSACTION_STATUS_CHANGED,
	PK_BACKEND_PERCENTAGE_CHANGED,
	PK_BACKEND_SUB_PERCENTAGE_CHANGED,
	PK_BACKEND_NO_PERCENTAGE_UPDATES,
	PK_BACKEND_DESCRIPTION,
	PK_BACKEND_PACKAGE,
	PK_BACKEND_UPDATE_DETAIL,
	PK_BACKEND_ERROR_CODE,
	PK_BACKEND_UPDATES_CHANGED,
	PK_BACKEND_REQUIRE_RESTART,
	PK_BACKEND_FINISHED,
	PK_BACKEND_ALLOW_INTERRUPT,
	PK_BACKEND_LAST_SIGNAL
};

static guint signals [PK_BACKEND_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkBackend, pk_backend, G_TYPE_OBJECT)

/**
 * pk_backend_build_library_path:
 **/
gchar *
pk_backend_build_library_path (PkBackend *backend)
{
	gchar *path;
	gchar *filename;

	g_return_val_if_fail (backend != NULL, NULL);

	filename = g_strdup_printf ("libpk_backend_%s.so", backend->priv->name);
	path = g_build_filename ("..", "backends", backend->priv->name, ".libs", filename, NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS) == FALSE) {
		g_free (path);
		path = g_build_filename (LIBDIR, "packagekit-backend", filename, NULL);
	}
	g_free (filename);
	pk_debug ("dlopening '%s'", path);

	return path;
}

/**
 * pk_backend_load:
 **/
gboolean
pk_backend_load (PkBackend *backend, const gchar *backend_name)
{
	GModule *handle;
	gchar *path;

	g_return_val_if_fail (backend_name != NULL, FALSE);

	if (backend->priv->handle != NULL) {
		pk_warning ("pk_backend_load called multiple times. This is bad");
		return FALSE;
	}

	/* save the backend name */
	backend->priv->name = g_strdup (backend_name);

	pk_debug ("Trying to load : %s", backend_name);
	path = pk_backend_build_library_path (backend);
	handle = g_module_open (path, 0);
	if (handle == NULL) {
		pk_debug ("opening module %s failed : %s", backend_name, g_module_error ());
		g_free (path);
		return FALSE;
	}
	g_free (path);

	backend->priv->handle = handle;

	if (g_module_symbol (handle, "pk_backend_desc", (gpointer) &backend->desc) == FALSE) {
		g_module_close (handle);
		pk_error ("could not find description in plugin %s, not loading", backend_name);
	}

	/* initialize, but protect against dodgy backends */
	backend->priv->during_initialize = TRUE;
	if (backend->desc->initialize) {
		backend->desc->initialize (backend);
	}
	backend->priv->during_initialize = FALSE;

	/* did we fail? */
	if (backend->priv->set_error == TRUE) {
		pk_debug ("init failed...");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_backend_unload:
 **/
gboolean
pk_backend_unload (PkBackend *backend)
{
	if (backend->priv->handle == NULL) {
		return FALSE;
	}
	g_module_close (backend->priv->handle);
	backend->priv->handle = NULL;
	return TRUE;
}

const gchar *
pk_backend_get_name (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, NULL);

	return backend->priv->name;
}

/**
 * pk_backend_thread_create:
 **/
gboolean
pk_backend_thread_create (PkBackend *backend, PkBackendThreadFunc func, gpointer data)
{
	return pk_thread_list_create (backend->priv->thread_list, (PkThreadFunc) func, backend, data);
}

/**
 * pk_backend_thread_helper:
 **/
gboolean
pk_backend_thread_helper (PkBackend *backend, PkBackendThreadFunc func, gpointer data)
{
	if (pk_backend_thread_create (backend, func, data) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_CREATE_THREAD_FAILED, "Failed to create thread");
		pk_backend_finished (backend);
		return FALSE;
	}

	pk_debug ("waiting for all threads in this backend");
	pk_thread_list_wait (backend->priv->thread_list);

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_parse_common_output:
 *
 * If you are editing this function creating a new backend,
 * then you are probably doing something wrong.
 **/
static gboolean
pk_backend_parse_common_output (PkBackend *backend, const gchar *line)
{
	gchar **sections;
	guint size;
	gchar *command;
	gboolean ret = TRUE;
	PkInfoEnum info;
	PkGroupEnum group;

	/* check if output line */
	if (line == NULL || strstr (line, "\t") == NULL)
		return FALSE;

	/* split by tab */
	sections = g_strsplit (line, "\t", 0);
	command = sections[0];

	/* get size */
	size = g_strv_length (sections);

	if (strcmp (command, "package") == 0) {
		if (size != 4) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		if (pk_package_id_check (sections[2]) == TRUE) {
			info = pk_info_enum_from_text (sections[1]);
			/* just until we've converted the backends */
			if (info == PK_INFO_ENUM_UNKNOWN) {
				g_print ("Info enumerated type '%s' not recognised\n", sections[1]);
				g_print ("See src/pk-enum.c for allowed values.\n");
				pk_error ("Runtime error, cannot continue");
			}
			pk_debug ("info=%s, package='%s' shortdesc='%s'",
				  pk_info_enum_to_text (info), sections[2], sections[3]);
			pk_backend_package (backend, info, sections[2], sections[3]);
		} else {
			pk_warning ("invalid package_id");
		}
	} else if (strcmp (command, "description") == 0) {
		if (size != 6) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		group = pk_group_enum_from_text (sections[3]);
		pk_backend_description (backend, sections[1], sections[2], group, sections[4], sections[5]);
	} else {
		pk_warning ("invalid command '%s'", command);
	}
out:
	g_strfreev (sections);
	return ret;
}

/**
 * pk_backend_parse_common_error:
 *
 * If you are editing this function creating a new backend,
 * then you are probably doing something wrong.
 **/
static gboolean
pk_backend_parse_common_error (PkBackend *backend, const gchar *line)
{
	gchar **sections;
	guint size;
	guint percentage;
	gchar *command;
	PkErrorCodeEnum error_enum;
	PkStatusEnum status_enum;
	PkRestartEnum restart_enum;
	gboolean ret = TRUE;

	/* check if output line */
	if (line == NULL || strstr (line, "\t") == NULL)
		return FALSE;

	/* split by tab */
	sections = g_strsplit (line, "\t", 0);
	command = sections[0];

	/* get size */
	for (size=0; sections[size]; size++);

	if (strcmp (command, "percentage") == 0) {
		if (size != 2) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		percentage = atoi(sections[1]);
		pk_backend_change_percentage (backend, percentage);
	} else if (strcmp (command, "subpercentage") == 0) {
		if (size != 2) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		percentage = atoi(sections[1]);
		pk_backend_change_sub_percentage (backend, percentage);
	} else if (strcmp (command, "error") == 0) {
		if (size != 3) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		error_enum = pk_error_enum_from_text (sections[1]);
		pk_backend_error_code (backend, error_enum, sections[2]);
	} else if (strcmp (command, "requirerestart") == 0) {
		if (size != 3) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		restart_enum = pk_restart_enum_from_text (sections[1]);
		pk_backend_require_restart (backend, restart_enum, sections[2]);
	} else if (strcmp (command, "status") == 0) {
		if (size != 2) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		status_enum = pk_status_enum_from_text (sections[1]);
		pk_backend_change_status (backend, status_enum);
	} else if (strcmp (command, "allow-interrupt") == 0) {
		if (size != 2) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		if (strcmp (sections[1], "true") == 0) {
			pk_backend_allow_interrupt (backend, TRUE);
		} else if (strcmp (sections[1], "false") == 0) {
			pk_backend_allow_interrupt (backend, FALSE);
		} else {
			pk_warning ("invalid section '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
	} else if (strcmp (command, "no-percentage-updates") == 0) {
		if (size != 1) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		pk_backend_no_percentage_updates (backend);
	} else {
		pk_warning ("invalid command '%s'", command);
	}
out:
	g_strfreev (sections);
	return ret;
}

/**
 * pk_backend_spawn_helper_new:
 **/
static gboolean
pk_backend_spawn_helper_delete (PkBackend *backend)
{
	if (backend->priv->spawn == NULL) {
		pk_error ("spawn object not in use");
	}
	pk_debug ("deleting spawn %p", backend->priv->spawn);
	g_signal_handler_disconnect (backend->priv->spawn, backend->priv->signal_finished);
	g_signal_handler_disconnect (backend->priv->spawn, backend->priv->signal_stdout);
	g_signal_handler_disconnect (backend->priv->spawn, backend->priv->signal_stderr);
	g_object_unref (backend->priv->spawn);
	backend->priv->spawn = NULL;
	return TRUE;
}

/**
 * pk_backend_spawn_finished_cb:
 **/
static void
pk_backend_spawn_finished_cb (PkSpawn *spawn, gint exitcode, PkBackend *backend)
{
	pk_debug ("deleting spawn %p, exit code %i", spawn, exitcode);
	pk_backend_spawn_helper_delete (backend);

	/* check shit scripts returned an error on failure */
	if (exitcode != 0 && backend->priv->exit != PK_EXIT_ENUM_FAILED) {
		pk_warning ("script returned false but did not return error");
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
				       "Helper returned non-zero return value but did not set error");
	}
	pk_backend_finished (backend);
}

/**
 * pk_backend_spawn_stdout_cb:
 **/
static void
pk_backend_spawn_stdout_cb (PkSpawn *spawn, const gchar *line, PkBackend *backend)
{
	pk_debug ("stdout from %p = '%s'", spawn, line);
	pk_backend_parse_common_output (backend, line);
}

/**
 * pk_backend_spawn_stderr_cb:
 **/
static void
pk_backend_spawn_stderr_cb (PkSpawn *spawn, const gchar *line, PkBackend *backend)
{
	pk_debug ("stderr from %p = '%s'", spawn, line);
	pk_backend_parse_common_error (backend, line);
}

/**
 * pk_backend_spawn_helper_new:
 **/
static gboolean
pk_backend_spawn_helper_new (PkBackend *backend)
{
	if (backend->priv->spawn != NULL) {
		pk_error ("spawn object already in use");
	}
	backend->priv->spawn = pk_spawn_new ();
	pk_debug ("allocating spawn %p", backend->priv->spawn);
	backend->priv->signal_finished =
		g_signal_connect (backend->priv->spawn, "finished",
				  G_CALLBACK (pk_backend_spawn_finished_cb), backend);
	backend->priv->signal_stdout =
		g_signal_connect (backend->priv->spawn, "stdout",
				  G_CALLBACK (pk_backend_spawn_stdout_cb), backend);
	backend->priv->signal_stderr =
		g_signal_connect (backend->priv->spawn, "stderr",
				  G_CALLBACK (pk_backend_spawn_stderr_cb), backend);
	return TRUE;
}

/**
 * pk_backend_spawn_helper_internal:
 **/
static gboolean
pk_backend_spawn_helper_internal (PkBackend *backend, const gchar *script, const gchar *argument)
{
	gboolean ret;
	gchar *filename;
	gchar *command;

	/* build script */
	filename = g_build_filename (DATADIR, "PackageKit", "helpers", backend->priv->name, script, NULL);
	pk_debug ("using spawn filename %s", filename);

	if (argument != NULL) {
		command = g_strdup_printf ("%s %s", filename, argument);
	} else {
		command = g_strdup (filename);
	}

	pk_backend_spawn_helper_new (backend);
	ret = pk_spawn_command (backend->priv->spawn, command);
	if (ret == FALSE) {
		pk_backend_spawn_helper_delete (backend);
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Spawn of helper '%s' failed", command);
		pk_backend_finished (backend);
	}
	g_free (filename);
	g_free (command);
	return ret;
}

/**
 * pk_backend_spawn_kill:
 **/
gboolean
pk_backend_spawn_kill (PkBackend *backend)
{
	if (backend->priv->spawn == NULL) {
		pk_warning ("cannot kill missing process");
		return FALSE;
	}
	pk_spawn_kill (backend->priv->spawn);
	return TRUE;
}

/**
 * pk_backend_spawn_helper:
 **/
gboolean
pk_backend_spawn_helper (PkBackend *backend, const gchar *script, ...)
{
	gboolean ret;
	va_list args;
	gchar *arguments;

	/* get the argument list */
	va_start (args, script);
	arguments = g_strjoinv (" ", (gchar **)(void *)args);
	va_end (args);

	ret = pk_backend_spawn_helper_internal (backend, script, arguments);
	g_free (arguments);
	return ret;
}

/* ick, we need to call this directly... */
static gboolean
pk_backend_finished_delay (gpointer data);
/**
 * pk_backend_not_implemented_yet:
 **/
gboolean
pk_backend_not_implemented_yet (PkBackend *backend, const gchar *method)
{
	pk_backend_error_code (backend, PK_ERROR_ENUM_NOT_SUPPORTED, "the method '%s' is not implemented yet", method);
	/* don't wait, do this now */
	backend->priv->exit = PK_EXIT_ENUM_FAILED;
	pk_backend_finished_delay (backend);
	return TRUE;
}

/**
 * pk_backend_change_percentage:
 **/
gboolean
pk_backend_change_percentage (PkBackend *backend, guint percentage)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* save in case we need this from coldplug */
	backend->priv->last_percentage = percentage;

	pk_debug ("emit percentage-changed %i", percentage);
	g_signal_emit (backend, signals [PK_BACKEND_PERCENTAGE_CHANGED], 0, percentage);
	return TRUE;
}

/**
 * pk_backend_change_sub_percentage:
 **/
gboolean
pk_backend_change_sub_percentage (PkBackend *backend, guint percentage)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* save in case we need this from coldplug */
	backend->priv->last_subpercentage = percentage;

	pk_debug ("emit sub-percentage-changed %i", percentage);
	g_signal_emit (backend, signals [PK_BACKEND_SUB_PERCENTAGE_CHANGED], 0, percentage);
	return TRUE;
}

/**
 * pk_backend_set_role:
 **/
gboolean
pk_backend_set_role (PkBackend *backend, PkRoleEnum role)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* Should only be called once... */
	if (backend->priv->role != PK_ROLE_ENUM_UNKNOWN) {
		pk_error ("cannot set role more than once, already %s",
			  pk_role_enum_to_text (backend->priv->role));
	}
	pk_debug ("setting role to %s", pk_role_enum_to_text (role));
	backend->priv->assigned = TRUE;
	backend->priv->role = role;
	backend->priv->status = PK_STATUS_ENUM_WAIT;
	return TRUE;
}

/**
 * pk_backend_change_status:
 **/
gboolean
pk_backend_change_status (PkBackend *backend, PkStatusEnum status)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	backend->priv->status = status;
	pk_debug ("emiting transaction-status-changed %i", status);
	g_signal_emit (backend, signals [PK_BACKEND_TRANSACTION_STATUS_CHANGED], 0, status);
	return TRUE;
}

/**
 * pk_backend_package:
 **/
gboolean
pk_backend_package (PkBackend *backend, PkInfoEnum info, const gchar *package, const gchar *summary)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* save in case we need this from coldplug */
	g_free (backend->priv->last_package);
	backend->priv->last_package = g_strdup (package);

	pk_debug ("emit package %i, %s, %s", info, package, summary);
	g_signal_emit (backend, signals [PK_BACKEND_PACKAGE], 0, info, package, summary);

	return TRUE;
}

/**
 * pk_backend_update_detail:
 **/
gboolean
pk_backend_update_detail (PkBackend *backend, const gchar *package_id,
			  const gchar *updates, const gchar *obsoletes,
			  const gchar *url, const gchar *restart,
			  const gchar *update_text)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	pk_debug ("emit update-detail %s, %s, %s, %s, %s, %s",
		  package_id, updates, obsoletes, url, restart, update_text);
	g_signal_emit (backend, signals [PK_BACKEND_UPDATE_DETAIL], 0,
		       package_id, updates, obsoletes, url, restart, update_text);
	return TRUE;
}


/**
 * pk_backend_get_percentage:
 **/
gboolean
pk_backend_get_percentage (PkBackend *backend, guint *percentage)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* no data yet... */
	if (backend->priv->last_percentage == PK_BACKEND_PERCENTAGE_INVALID) {
		return FALSE;
	}
	*percentage = backend->priv->last_percentage;
	return TRUE;
}

/**
 * pk_backend_get_sub_percentage:
 **/
gboolean
pk_backend_get_sub_percentage (PkBackend *backend, guint *percentage)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* no data yet... */
	if (backend->priv->last_subpercentage == PK_BACKEND_PERCENTAGE_INVALID) {
		return FALSE;
	}
	*percentage = backend->priv->last_subpercentage;
	return TRUE;
}

/**
 * pk_backend_get_package:
 **/
gboolean
pk_backend_get_package (PkBackend *backend, gchar **package_id)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	if (backend->priv->last_package == NULL) {
		return FALSE;
	}
	*package_id = g_strdup (backend->priv->last_package);
	return TRUE;
}

/**
 * pk_backend_require_restart:
 **/
gboolean
pk_backend_require_restart (PkBackend *backend, PkRestartEnum restart, const gchar *details)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	pk_debug ("emit require-restart %i, %s", restart, details);
	g_signal_emit (backend, signals [PK_BACKEND_REQUIRE_RESTART], 0, restart, details);

	return TRUE;
}

/**
 * pk_backend_description:
 **/
gboolean
pk_backend_description (PkBackend *backend, const gchar *package_id,
			const gchar *licence, PkGroupEnum group,
			const gchar *description, const gchar *url)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	pk_debug ("emit description %s, %s, %i, %s, %s", package_id, licence, group, description, url);
	g_signal_emit (backend, signals [PK_BACKEND_DESCRIPTION], 0, package_id, licence, group, description, url);

	return TRUE;
}

/**
 * pk_backend_updates_changed:
 **/
gboolean
pk_backend_updates_changed (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	pk_debug ("emit updates-changed");
	g_signal_emit (backend, signals [PK_BACKEND_UPDATES_CHANGED], 0);
	return TRUE;
}

/**
 * pk_backend_error_code:
 **/
gboolean
pk_backend_error_code (PkBackend *backend, PkErrorCodeEnum code, const gchar *format, ...)
{
	va_list args;
	gchar buffer[1025];

	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	va_start (args, format);
	g_vsnprintf (buffer, 1024, format, args);
	va_end (args);

	/* did we set a duplicate error? */
	if (backend->priv->set_error == TRUE) {
		g_print ("pk_backend_error_code was used more than once in the same backend instance!\n");
		g_print ("You tried to set '%s'\n", buffer);
		pk_error ("Internal error, cannot continue");
		return FALSE;
	}
	backend->priv->set_error = TRUE;

	/* we mark any transaction with errors as failed */
	backend->priv->exit = PK_EXIT_ENUM_FAILED;

	pk_debug ("emit error-code %i, %s", code, buffer);
	g_signal_emit (backend, signals [PK_BACKEND_ERROR_CODE], 0, code, buffer);

	return TRUE;
}

/**
 * pk_backend_get_status:
 **/
gboolean
pk_backend_get_status (PkBackend *backend, PkStatusEnum *status)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* check to see if we have an action */
	if (backend->priv->assigned == FALSE) {
		pk_warning ("Not assigned");
		return FALSE;
	}
	*status = backend->priv->status;
	return TRUE;
}

/**
 * pk_backend_get_role:
 **/
gboolean
pk_backend_get_role (PkBackend *backend, PkRoleEnum *role, const gchar **package_id)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* check to see if we have an action */
	if (backend->priv->assigned == FALSE) {
		pk_warning ("Not assigned");
		return FALSE;
	}
	if (role != NULL) {
		*role = backend->priv->role;
	}
	if (package_id != NULL) {
		*package_id = g_strdup (backend->priv->xcached_package_id);
	}
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
	pk_debug ("emit finished %i", backend->priv->exit);
	g_signal_emit (backend, signals [PK_BACKEND_FINISHED], 0, backend->priv->exit);
	return FALSE;
}

/**
 * pk_backend_finished:
 **/
gboolean
pk_backend_finished (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* are we trying to finish in init? */
	if (backend->priv->during_initialize == TRUE) {
		g_print ("You can't call pk_backend_finished in backend_initialize!\n");
		pk_error ("Internal error, cannot continue");
	}

	/* check we have no threads running */
	if (pk_thread_list_number_running (backend->priv->thread_list) != 0) {
		g_print ("There are threads running and the task has been asked to finish!\n");
		g_print ("If you are using :\n");
		g_print ("* pk_backend_thread_helper\n");
		g_print ("   - You should _not_ use pk_backend_finished directly");
		g_print ("   - Return from the function like normal\n");
		g_print ("* pk_thread_list_create:\n");
		g_print ("   -  If used internally you _have_ to use pk_thread_list_wait\n");
		pk_error ("Internal error, cannot continue (will segfault in the near future...)");
	}

	/* we have to run this idle as the command may finish before the transaction
	 * has been sent to the client. I love async... */
	pk_debug ("adding finished %p to timeout loop", backend);
	g_timeout_add (50, pk_backend_finished_delay, backend);
	return TRUE;
}

/**
 * pk_backend_no_percentage_updates:
 **/
gboolean
pk_backend_no_percentage_updates (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* invalidate previous percentage */
	backend->priv->last_percentage = PK_BACKEND_PERCENTAGE_INVALID;

	pk_debug ("emit no-percentage-updates");
	g_signal_emit (backend, signals [PK_BACKEND_NO_PERCENTAGE_UPDATES], 0);
	return TRUE;
}

/**
 * pk_backend_allow_interrupt:
 **/
gboolean
pk_backend_allow_interrupt (PkBackend *backend, gboolean allow_restart)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	pk_debug ("emit allow-interrupt %i", allow_restart);
	backend->priv->is_killable = allow_restart;
	g_signal_emit (backend, signals [PK_BACKEND_ALLOW_INTERRUPT], 0);
	return TRUE;
}


/**
 * pk_backend_cancel:
 */
gboolean
pk_backend_cancel (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->cancel == NULL) {
		pk_backend_not_implemented_yet (backend, "Cancel");
		return FALSE;
	}
	/* check to see if we have an action */
	if (backend->priv->assigned == FALSE) {
		pk_warning ("Not assigned");
		return FALSE;
	}
	/* check if it's safe to kill */
	if (backend->priv->is_killable == FALSE) {
		pk_warning ("tried to kill a process that is not safe to kill");
		return FALSE;
	}
	if (backend->priv->spawn == NULL) {
		pk_warning ("tried to kill a process that does not exist");
		return FALSE;
	}
	backend->desc->cancel (backend);
	return TRUE;
}

/**
 * pk_backend_run:
 */
gboolean
pk_backend_run (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, FALSE);

	/* we are no longer waiting, we are setting up */
	backend->priv->status = PK_STATUS_ENUM_SETUP;

	/* do the correct action with the cached parameters */
	if (backend->priv->role == PK_ROLE_ENUM_GET_DEPENDS) {
		backend->desc->get_depends (backend,
					    backend->priv->xcached_package_id);
	} else if (backend->priv->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		backend->desc->get_update_detail (backend,
						  backend->priv->xcached_package_id);
	} else if (backend->priv->role == PK_ROLE_ENUM_RESOLVE) {
		backend->desc->resolve (backend, backend->priv->xcached_package_id);
	} else if (backend->priv->role == PK_ROLE_ENUM_GET_DESCRIPTION) {
		backend->desc->get_description (backend,
						backend->priv->xcached_package_id);
	} else if (backend->priv->role == PK_ROLE_ENUM_GET_REQUIRES) {
		backend->desc->get_requires (backend,
					     backend->priv->xcached_package_id);
	} else if (backend->priv->role == PK_ROLE_ENUM_GET_UPDATES) {
		backend->desc->get_updates (backend);
	} else if (backend->priv->role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		backend->desc->search_details (backend,
					       backend->priv->xcached_filter,
					       backend->priv->xcached_search);
	} else if (backend->priv->role == PK_ROLE_ENUM_SEARCH_FILE) {
		backend->desc->search_file (backend,
					    backend->priv->xcached_filter,
					    backend->priv->xcached_search);
	} else if (backend->priv->role == PK_ROLE_ENUM_SEARCH_GROUP) {
		backend->desc->search_group (backend,
					     backend->priv->xcached_filter,
					     backend->priv->xcached_search);
	} else if (backend->priv->role == PK_ROLE_ENUM_SEARCH_NAME) {
		backend->desc->search_name (backend,
					    backend->priv->xcached_filter,
					    backend->priv->xcached_search);
	} else if (backend->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGE) {
		backend->desc->install_package (backend,
						backend->priv->xcached_package_id);
	} else if (backend->priv->role == PK_ROLE_ENUM_INSTALL_FILE) {
		backend->desc->install_file (backend,
					     backend->priv->xcached_full_path);
	} else if (backend->priv->role == PK_ROLE_ENUM_REFRESH_CACHE) {
		backend->desc->refresh_cache (backend,
					      backend->priv->xcached_force);
	} else if (backend->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGE) {
		backend->desc->remove_package (backend,
					       backend->priv->xcached_package_id,
					       backend->priv->xcached_allow_deps);
	} else if (backend->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGE) {
		backend->desc->update_package (backend,
					       backend->priv->xcached_package_id);
	} else if (backend->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		backend->desc->update_system (backend);
	} else {
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_backend_get_depends:
 */
gboolean
pk_backend_get_depends (PkBackend *backend, const gchar *package_id)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->get_depends == NULL) {
		pk_backend_not_implemented_yet (backend, "GetDepends");
		return FALSE;
	}
	backend->priv->xcached_package_id = g_strdup (package_id);
	pk_backend_set_role (backend, PK_ROLE_ENUM_GET_DEPENDS);
	return TRUE;
}

/**
 * pk_backend_get_update_detail:
 */
gboolean
pk_backend_get_update_detail (PkBackend *backend, const gchar *package_id)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->get_update_detail == NULL) {
		pk_backend_not_implemented_yet (backend, "GetUpdateDetail");
		return FALSE;
	}
	backend->priv->xcached_package_id = g_strdup (package_id);
	pk_backend_set_role (backend, PK_ROLE_ENUM_GET_UPDATE_DETAIL);
	return TRUE;
}

/**
 * pk_backend_get_description:
 */
gboolean
pk_backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->get_description == NULL) {
		pk_backend_not_implemented_yet (backend, "GetDescription");
		return FALSE;
	}
	backend->priv->xcached_package_id = g_strdup (package_id);
	pk_backend_set_role (backend, PK_ROLE_ENUM_GET_DESCRIPTION);
	return TRUE;
}

/**
 * pk_backend_get_requires:
 */
gboolean
pk_backend_get_requires (PkBackend *backend, const gchar *package_id)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->get_requires == NULL) {
		pk_backend_not_implemented_yet (backend, "GetRequires");
		return FALSE;
	}
	backend->priv->xcached_package_id = g_strdup (package_id);
	pk_backend_set_role (backend, PK_ROLE_ENUM_GET_REQUIRES);
	return TRUE;
}

/**
 * pk_backend_get_updates:
 */
gboolean
pk_backend_get_updates (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->get_updates == NULL) {
		pk_backend_not_implemented_yet (backend, "GetUpdates");
		return FALSE;
	}
	pk_backend_set_role (backend, PK_ROLE_ENUM_GET_UPDATES);
	return TRUE;
}

/**
 * pk_backend_install_package:
 */
gboolean
pk_backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->install_package == NULL) {
		pk_backend_not_implemented_yet (backend, "InstallPackage");
		return FALSE;
	}
	backend->priv->xcached_package_id = g_strdup (package_id);
	pk_backend_set_role (backend, PK_ROLE_ENUM_INSTALL_PACKAGE);
	return TRUE;
}

/**
 * pk_backend_install_file:
 */
gboolean
pk_backend_install_file (PkBackend *backend, const gchar *full_path)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->install_file == NULL) {
		pk_backend_not_implemented_yet (backend, "InstallFile");
		return FALSE;
	}
	backend->priv->xcached_full_path = g_strdup (full_path);
	pk_backend_set_role (backend, PK_ROLE_ENUM_INSTALL_FILE);
	return TRUE;
}

/**
 * pk_backend_refresh_cache:
 */
gboolean
pk_backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->refresh_cache == NULL) {
		pk_backend_not_implemented_yet (backend, "RefreshCache");
		return FALSE;
	}
	backend->priv->xcached_force = force;
	pk_backend_set_role (backend, PK_ROLE_ENUM_REFRESH_CACHE);
	return TRUE;
}

/**
 * pk_backend_remove_package:
 */
gboolean
pk_backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->remove_package == NULL) {
		pk_backend_not_implemented_yet (backend, "RemovePackage");
		return FALSE;
	}
	backend->priv->xcached_allow_deps = allow_deps;
	backend->priv->xcached_package_id = g_strdup (package_id);
	pk_backend_set_role (backend, PK_ROLE_ENUM_REMOVE_PACKAGE);
	return TRUE;
}

/**
 * pk_backend_resolve:
 */
gboolean
pk_backend_resolve (PkBackend *backend, const gchar *package)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->resolve == NULL) {
		pk_backend_not_implemented_yet (backend, "Resolve");
		return FALSE;
	}
	backend->priv->xcached_package_id = g_strdup (package);
	pk_backend_set_role (backend, PK_ROLE_ENUM_RESOLVE);
	return TRUE;
}

/**
 * pk_backend_search_details:
 */
gboolean
pk_backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->search_details == NULL) {
		pk_backend_not_implemented_yet (backend, "SearchDetails");
		return FALSE;
	}
	backend->priv->xcached_filter = g_strdup (filter);
	backend->priv->xcached_search = g_strdup (search);
	pk_backend_set_role (backend, PK_ROLE_ENUM_SEARCH_DETAILS);
	return TRUE;
}

/**
 * pk_backend_search_file:
 */
gboolean
pk_backend_search_file (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->search_file == NULL) {
		pk_backend_not_implemented_yet (backend, "SearchFile");
		return FALSE;
	}
	backend->priv->xcached_filter = g_strdup (filter);
	backend->priv->xcached_search = g_strdup (search);
	pk_backend_set_role (backend, PK_ROLE_ENUM_SEARCH_FILE);
	return TRUE;
}

/**
 * pk_backend_search_group:
 */
gboolean
pk_backend_search_group (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->search_group == NULL) {
		pk_backend_not_implemented_yet (backend, "SearchGroup");
		return FALSE;
	}
	backend->priv->xcached_filter = g_strdup (filter);
	backend->priv->xcached_search = g_strdup (search);
	pk_backend_set_role (backend, PK_ROLE_ENUM_SEARCH_GROUP);
	return TRUE;
}

/**
 * pk_backend_search_name:
 */
gboolean
pk_backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->search_name == NULL) {
		pk_backend_not_implemented_yet (backend, "SearchName");
		return FALSE;
	}
	backend->priv->xcached_filter = g_strdup (filter);
	backend->priv->xcached_search = g_strdup (search);
	pk_backend_set_role (backend, PK_ROLE_ENUM_SEARCH_NAME);
	return TRUE;
}

/**
 * pk_backend_update_package:
 */
gboolean
pk_backend_update_package (PkBackend *backend, const gchar *package_id)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->update_package == NULL) {
		pk_backend_not_implemented_yet (backend, "UpdatePackage");
		return FALSE;
	}
	backend->priv->xcached_package_id = g_strdup (package_id);
	pk_backend_set_role (backend, PK_ROLE_ENUM_UPDATE_PACKAGE);
	return TRUE;
}

/**
 * pk_backend_update_system:
 */
gboolean
pk_backend_update_system (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->update_system == NULL) {
		pk_backend_not_implemented_yet (backend, "UpdateSystem");
		return FALSE;
	}
	pk_backend_set_role (backend, PK_ROLE_ENUM_UPDATE_SYSTEM);
	return TRUE;
}

/**
 * pk_backend_get_backend_detail:
 */
gboolean
pk_backend_get_backend_detail (PkBackend *backend, gchar **name, gchar **author, gchar **version)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (name != NULL && backend->desc->description != NULL) {
		*name = g_strdup (backend->desc->description);
	}
	if (author != NULL && backend->desc->author != NULL) {
		*author = g_strdup (backend->desc->author);
	}
	if (version != NULL && backend->desc->version != NULL) {
		*version = g_strdup (backend->desc->version);
	}
	return TRUE;
}

/**
 * pk_backend_get_actions:
 *
 * You need to g_object_unref the returned object
 */
PkEnumList *
pk_backend_get_actions (PkBackend *backend)
{
	PkEnumList *elist;
	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_ROLE);
	if (backend->desc->cancel != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_CANCEL);
	}
	if (backend->desc->get_depends != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_DEPENDS);
	}
	if (backend->desc->get_description != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_DESCRIPTION);
	}
	if (backend->desc->get_requires != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_REQUIRES);
	}
	if (backend->desc->get_updates != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_UPDATES);
	}
	if (backend->desc->install_package != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_INSTALL_PACKAGE);
	}
	if (backend->desc->install_file != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_INSTALL_FILE);
	}
	if (backend->desc->refresh_cache != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_REFRESH_CACHE);
	}
	if (backend->desc->remove_package != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_REMOVE_PACKAGE);
	}
	if (backend->desc->resolve != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_RESOLVE);
	}
	if (backend->desc->search_details != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_DETAILS);
	}
	if (backend->desc->search_file != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_FILE);
	}
	if (backend->desc->search_group != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_GROUP);
	}
	if (backend->desc->search_name != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_NAME);
	}
	if (backend->desc->update_package != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_UPDATE_PACKAGE);
	}
	if (backend->desc->update_system != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_UPDATE_SYSTEM);
	}
	return elist;
}

/**
 * pk_backend_get_groups:
 *
 * You need to g_object_unref the returned object
 */
PkEnumList *
pk_backend_get_groups (PkBackend *backend)
{
	PkEnumList *elist;
	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_GROUP);
	if (backend->desc->get_groups != NULL) {
		backend->desc->get_groups (backend, elist);
	}
	return elist;
}

/**
 * pk_backend_get_filters:
 *
 * You need to g_object_unref the returned object
 */
PkEnumList *
pk_backend_get_filters (PkBackend *backend)
{
	PkEnumList *elist;
	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_FILTER);
	if (backend->desc->get_filters != NULL) {
		backend->desc->get_filters (backend, elist);
	}
	return elist;
}

/**
 * pk_backend_get_runtime:
 */
gdouble
pk_backend_get_runtime (PkBackend *backend)
{
	return g_timer_elapsed (backend->priv->timer, NULL);
}

/**
 * pk_backend_network_is_online:
 */
gboolean
pk_backend_network_is_online (PkBackend *backend)
{
	return pk_network_is_online (backend->priv->network);
}

/**
 * pk_backend_finalize:
 **/
static void
pk_backend_finalize (GObject *object)
{
	PkBackend *backend;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_BACKEND (object));

	backend = PK_BACKEND (object);

	if (backend->desc != NULL) {
		if (backend->desc->destroy != NULL) {
			backend->desc->destroy (backend);
		}		
	}

	pk_debug ("freeing %s (%p)", backend->priv->name, backend);
	g_free (backend->priv->name);
	g_free (backend->priv->last_package);
	pk_backend_unload (backend);
	g_timer_destroy (backend->priv->timer);

	g_free (backend->priv->xcached_package_id);
	g_free (backend->priv->xcached_filter);
	g_free (backend->priv->xcached_search);

	if (backend->priv->spawn != NULL) {
		pk_backend_spawn_helper_delete (backend);
	}
	g_object_unref (backend->priv->network);
	g_object_unref (backend->priv->thread_list);

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

	signals [PK_BACKEND_TRANSACTION_STATUS_CHANGED] =
		g_signal_new ("transaction-status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_BACKEND_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_BACKEND_SUB_PERCENTAGE_CHANGED] =
		g_signal_new ("sub-percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_BACKEND_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_BACKEND_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_BACKEND_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_BACKEND_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_UINT_STRING_STRING,
			      G_TYPE_NONE, 5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_BACKEND_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_BACKEND_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [PK_BACKEND_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_BACKEND_NO_PERCENTAGE_UPDATES] =
		g_signal_new ("no-percentage-updates",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [PK_BACKEND_ALLOW_INTERRUPT] =
		g_signal_new ("allow-interrupt",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (PkBackendPrivate));
}

/**
 * pk_backend_init:
 **/
static void
pk_backend_init (PkBackend *backend)
{
	backend->priv = PK_BACKEND_GET_PRIVATE (backend);
	backend->priv->timer = g_timer_new ();
	backend->priv->assigned = FALSE;
	backend->priv->is_killable = FALSE;
	backend->priv->set_error = FALSE;
	backend->priv->during_initialize = FALSE;
	backend->priv->spawn = NULL;
	backend->priv->handle = NULL;
	backend->priv->xcached_package_id = NULL;
	backend->priv->xcached_full_path = NULL;
	backend->priv->xcached_filter = NULL;
	backend->priv->xcached_search = NULL;
	backend->priv->last_percentage = PK_BACKEND_PERCENTAGE_INVALID;
	backend->priv->last_subpercentage = PK_BACKEND_PERCENTAGE_INVALID;
	backend->priv->last_package = NULL;
	backend->priv->role = PK_ROLE_ENUM_UNKNOWN;
	backend->priv->status = PK_STATUS_ENUM_UNKNOWN;
	backend->priv->exit = PK_EXIT_ENUM_SUCCESS;
	backend->priv->network = pk_network_new ();
	backend->priv->thread_list = pk_thread_list_new ();
}

/**
 * pk_backend_new:
 **/
PkBackend *
pk_backend_new (void)
{
	PkBackend *backend;
	backend = g_object_new (PK_TYPE_BACKEND, NULL);
	return PK_BACKEND (backend);
}

