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

#define PK_BACKEND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND, PkBackendPrivate))

struct _PkBackendPrivate
{
	GModule			*handle;
	gchar			*name;
	PkTaskStatus		 role; /* this never changes for the lifetime of a job */
	PkTaskStatus		 status; /* this changes */
	gchar			*package_id; /* never changes, this is linked to role */
	PkTaskExit		 exit;
	GTimer			*timer;
	PkSpawn			*spawn;
	gboolean		 is_killable;
	gboolean		 assigned;
	PkNetwork		*network;
};

enum {
	PK_TASK_JOB_STATUS_CHANGED,
	PK_TASK_PERCENTAGE_CHANGED,
	PK_TASK_SUB_PERCENTAGE_CHANGED,
	PK_TASK_NO_PERCENTAGE_UPDATES,
	PK_TASK_DESCRIPTION,
	PK_TASK_PACKAGE,
	PK_TASK_ERROR_CODE,
	PK_TASK_REQUIRE_RESTART,
	PK_TASK_FINISHED,
	PK_TASK_ALLOW_INTERRUPT,
	PK_TASK_LAST_SIGNAL
};

static guint signals [PK_TASK_LAST_SIGNAL] = { 0, };

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
	path = g_build_filename (LIBDIR, "packagekit-backend", filename, NULL);
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

	if (backend->desc->initialize) {
		backend->desc->initialize (backend);
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
	guint value = 0;
	gchar *command;
	gboolean ret = TRUE;
	PkTaskGroup group;

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
			value = atoi(sections[1]);
			pk_debug ("value=%i, package='%s' shortdesc='%s'", value, sections[2], sections[3]);
			pk_backend_package (backend, value, sections[2], sections[3]);
		} else {
			pk_warning ("invalid package_id");
		}
	} else if (strcmp (command, "description") == 0) {
		if (size != 5) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		group = pk_group_enum_from_text (sections[2]);
		pk_backend_description (backend, sections[1], group, sections[3], sections[4]);
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
	PkTaskErrorCode error_enum;
	PkTaskStatus status_enum;
	PkTaskRestart restart_enum;
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
		pk_backend_change_job_status (backend, status_enum);
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
 * pk_backend_spawn_finished_cb:
 **/
static void
pk_backend_spawn_finished_cb (PkSpawn *spawn, gint exitcode, PkBackend *backend)
{
	PkTaskExit exit;
	pk_debug ("unref'ing spawn %p, exit code %i", spawn, exitcode);
	g_object_unref (spawn);

	/* only emit success with a zero exit code */
	if (exitcode == 0) {
		exit = PK_EXIT_ENUM_SUCCESS;
	} else {
		exit = PK_EXIT_ENUM_FAILED;
	}
	pk_backend_finished (backend, exit);
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
 * pk_backend_spawn_helper_internal:
 **/
static gboolean
pk_backend_spawn_helper_internal (PkBackend *backend, const gchar *script, const gchar *argument)
{
	gboolean ret;
	gchar *filename;
	gchar *command;

	/* build script */
	filename = g_build_filename (DATADIR, "PackageKit", "helpers", script, NULL);

	if (argument != NULL) {
		command = g_strdup_printf ("%s %s", filename, argument);
	} else {
		command = g_strdup (filename);
	}

	backend->priv->spawn = pk_spawn_new ();
	g_signal_connect (backend->priv->spawn, "finished",
			  G_CALLBACK (pk_backend_spawn_finished_cb), backend);
	g_signal_connect (backend->priv->spawn, "stdout",
			  G_CALLBACK (pk_backend_spawn_stdout_cb), backend);
	g_signal_connect (backend->priv->spawn, "stderr",
			  G_CALLBACK (pk_backend_spawn_stderr_cb), backend);
	ret = pk_spawn_command (backend->priv->spawn, command);
	if (ret == FALSE) {
		g_object_unref (backend->priv->spawn);
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Spawn of helper '%s' failed", command);
		pk_backend_finished (backend, PK_EXIT_ENUM_FAILED);
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

/**
 * pk_backend_not_implemented_yet:
 **/
gboolean
pk_backend_not_implemented_yet (PkBackend *backend, const gchar *method)
{
	pk_backend_error_code (backend, PK_ERROR_ENUM_NOT_SUPPORTED, "the method '%s' is not implemented yet", method);
	pk_backend_finished (backend, PK_EXIT_ENUM_FAILED);
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
	pk_debug ("emit percentage-changed %i", percentage);
	g_signal_emit (backend, signals [PK_TASK_PERCENTAGE_CHANGED], 0, percentage);
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
	pk_debug ("emit sub-percentage-changed %i", percentage);
	g_signal_emit (backend, signals [PK_TASK_SUB_PERCENTAGE_CHANGED], 0, percentage);
	return TRUE;
}

/**
 * pk_backend_set_job_role:
 **/
gboolean
pk_backend_set_job_role (PkBackend *backend, PkTaskRole role, const gchar *package_id)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* Should only be called once... */
	if (backend->priv->role != PK_ROLE_ENUM_UNKNOWN) {
		pk_error ("cannot set role more than once, already %s",
			  pk_role_enum_to_text (backend->priv->role));
	}
	pk_debug ("setting role to %s (string is '%s')", pk_role_enum_to_text (role), package_id);
	backend->priv->role = role;
	backend->priv->package_id = g_strdup (package_id);
	return TRUE;
}

/**
 * pk_backend_change_job_status:
 **/
gboolean
pk_backend_change_job_status (PkBackend *backend, PkTaskStatus status)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	backend->priv->status = status;
	pk_debug ("emiting job-status-changed %i", status);
	g_signal_emit (backend, signals [PK_TASK_JOB_STATUS_CHANGED], 0, status);
	return TRUE;
}

/**
 * pk_backend_package:
 **/
gboolean
pk_backend_package (PkBackend *backend, guint value, const gchar *package, const gchar *summary)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	pk_debug ("emit package %i, %s, %s", value, package, summary);
	g_signal_emit (backend, signals [PK_TASK_PACKAGE], 0, value, package, summary);

	return TRUE;
}

/**
 * pk_backend_require_restart:
 **/
gboolean
pk_backend_require_restart (PkBackend *backend, PkTaskRestart restart, const gchar *details)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	pk_debug ("emit require-restart %i, %s", restart, details);
	g_signal_emit (backend, signals [PK_TASK_REQUIRE_RESTART], 0, restart, details);

	return TRUE;
}

/**
 * pk_backend_description:
 **/
gboolean
pk_backend_description (PkBackend *backend, const gchar *package, PkTaskGroup group,
		     const gchar *description, const gchar *url)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	pk_debug ("emit description %s, %i, %s, %s", package, group, description, url);
	g_signal_emit (backend, signals [PK_TASK_DESCRIPTION], 0, package, group, description, url);

	return TRUE;
}

/**
 * pk_backend_error_code:
 **/
gboolean
pk_backend_error_code (PkBackend *backend, PkTaskErrorCode code, const gchar *format, ...)
{
	va_list args;
	gchar buffer[1025];

	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	va_start (args, format);
	g_vsnprintf (buffer, 1024, format, args);
	va_end (args);

	pk_debug ("emit error-code %i, %s", code, buffer);
	g_signal_emit (backend, signals [PK_TASK_ERROR_CODE], 0, code, buffer);

	return TRUE;
}

/**
 * pk_backend_get_job_status:
 **/
gboolean
pk_backend_get_job_status (PkBackend *backend, PkTaskStatus *status)
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
 * pk_backend_get_job_role:
 **/
gboolean
pk_backend_get_job_role (PkBackend *backend, PkTaskRole *role, const gchar **package_id)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* check to see if we have an action */
	if (backend->priv->assigned == FALSE) {
		pk_warning ("Not assigned");
		return FALSE;
	}
	*role = backend->priv->role;
	*package_id = g_strdup (backend->priv->package_id);
	return TRUE;
}

/**
 * pk_backend_finished_idle:
 **/
static gboolean
pk_backend_finished_idle (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	pk_debug ("emit finished %i", backend->priv->exit);
	g_signal_emit (backend, signals [PK_TASK_FINISHED], 0, backend->priv->exit);
	return FALSE;
}

/**
 * pk_backend_finished:
 **/
gboolean
pk_backend_finished (PkBackend *backend, PkTaskExit exit)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* we have to run this idle as the command may finish before the job
	 * has been sent to the client. I love async... */
	pk_debug ("adding finished %p to idle loop", backend);
	backend->priv->exit = exit;
	g_idle_add (pk_backend_finished_idle, backend);
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

	pk_debug ("emit no-percentage-updates");
	g_signal_emit (backend, signals [PK_TASK_NO_PERCENTAGE_UPDATES], 0);
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
	g_signal_emit (backend, signals [PK_TASK_ALLOW_INTERRUPT], 0);
	return TRUE;
}


/**
 * pk_backend_cancel_job_try:
 */
gboolean
pk_backend_cancel_job_try (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, FALSE);
	if (backend->desc->cancel_job_try == NULL) {
		pk_backend_not_implemented_yet (backend, "CancelJobTry");
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
	backend->desc->cancel_job_try (backend);
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_QUERY, package_id);
	backend->desc->get_depends (backend, package_id);
	backend->priv->assigned = TRUE;
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_QUERY, package_id);
	backend->desc->get_description (backend, package_id);
	backend->priv->assigned = TRUE;
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_QUERY, package_id);
	backend->desc->get_requires (backend, package_id);
	backend->priv->assigned = TRUE;
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_QUERY, NULL);
	backend->desc->get_updates (backend);
	backend->priv->assigned = TRUE;
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_PACKAGE_INSTALL, package_id);
	backend->desc->install_package (backend, package_id);
	backend->priv->assigned = TRUE;
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_REFRESH_CACHE, NULL);
	backend->desc->refresh_cache (backend, force);
	backend->priv->assigned = TRUE;
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_PACKAGE_REMOVE, package_id);
	backend->desc->remove_package (backend, package_id, allow_deps);
	backend->priv->assigned = TRUE;
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_QUERY, search);
	backend->desc->search_details (backend, filter, search);
	backend->priv->assigned = TRUE;
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_QUERY, search);
	backend->desc->search_file (backend, filter, search);
	backend->priv->assigned = TRUE;
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_QUERY, search);
	backend->desc->search_group (backend, filter, search);
	backend->priv->assigned = TRUE;
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_QUERY, search);
	backend->desc->search_name (backend, filter, search);
	backend->priv->assigned = TRUE;
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_PACKAGE_UPDATE, package_id);
	backend->desc->update_package (backend, package_id);
	backend->priv->assigned = TRUE;
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
	pk_backend_set_job_role (backend, PK_ROLE_ENUM_SYSTEM_UPDATE, NULL);
	backend->desc->update_system (backend);
	backend->priv->assigned = TRUE;
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
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_ACTION);
	if (backend->desc->cancel_job_try != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_CANCEL_JOB);
	}
	if (backend->desc->get_depends != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_GET_DEPENDS);
	}
	if (backend->desc->get_description != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_GET_DESCRIPTION);
	}
	if (backend->desc->get_requires != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_GET_REQUIRES);
	}
	if (backend->desc->get_updates != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_GET_UPDATES);
	}
	if (backend->desc->install_package != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_INSTALL_PACKAGE);
	}
	if (backend->desc->refresh_cache != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_REFRESH_CACHE);
	}
	if (backend->desc->remove_package != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_REMOVE_PACKAGE);
	}
	if (backend->desc->search_details != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_SEARCH_DETAILS);
	}
	if (backend->desc->search_file != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_SEARCH_FILE);
	}
	if (backend->desc->search_group != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_SEARCH_GROUP);
	}
	if (backend->desc->search_name != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_SEARCH_NAME);
	}
	if (backend->desc->update_package != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_UPDATE_PACKAGE);
	}
	if (backend->desc->update_system != NULL) {
		pk_enum_list_append (elist, PK_ACTION_ENUM_UPDATE_SYSTEM);
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

	pk_debug ("freeing %s", backend->priv->name);
	g_free (backend->priv->name);
	pk_backend_unload (backend);
	g_timer_destroy (backend->priv->timer);
	g_object_unref (backend->priv->network);

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

	signals [PK_TASK_JOB_STATUS_CHANGED] =
		g_signal_new ("job-status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_SUB_PERCENTAGE_CHANGED] =
		g_signal_new ("sub-percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TASK_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_TASK_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_UINT_STRING_STRING,
			      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TASK_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_TASK_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_NO_PERCENTAGE_UPDATES] =
		g_signal_new ("no-percentage-updates",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [PK_TASK_ALLOW_INTERRUPT] =
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
	backend->priv->spawn = NULL;
	backend->priv->package_id = NULL;
	backend->priv->role = PK_ROLE_ENUM_UNKNOWN;
	backend->priv->status = PK_STATUS_ENUM_UNKNOWN;
	backend->priv->exit = PK_EXIT_ENUM_UNKNOWN;
	backend->priv->network = pk_network_new ();
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

