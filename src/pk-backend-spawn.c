/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2010 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

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
#include <glib/gprintf.h>
#include <gmodule.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-package-id.h>

#include "pk-backend.h"
#include "pk-backend-spawn.h"
#include "pk-spawn.h"
#include "pk-shared.h"

//#define ENABLE_STRACE

#define PK_BACKEND_SPAWN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND_SPAWN, PkBackendSpawnPrivate))
#define PK_BACKEND_SPAWN_PERCENTAGE_INVALID	101

#define	PK_UNSAFE_DELIMITERS	"\\\f\r\t"

struct PkBackendSpawnPrivate
{
	PkSpawn			*spawn;
	PkBackend		*backend;
	PkBackendJob		*job;
	gchar			*name;
	guint			 kill_id;
	GKeyFile		*conf;
	gboolean		 finished;
	gboolean		 allow_sigkill;
	gboolean		 is_busy;
	PkBackendSpawnFilterFunc stdout_func;
	PkBackendSpawnFilterFunc stderr_func;
};

G_DEFINE_TYPE (PkBackendSpawn, pk_backend_spawn, G_TYPE_OBJECT)

gboolean
pk_backend_spawn_set_filter_stdout (PkBackendSpawn *backend_spawn, PkBackendSpawnFilterFunc func)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);
	backend_spawn->priv->stdout_func = func;
	return TRUE;
}

gboolean
pk_backend_spawn_set_filter_stderr (PkBackendSpawn *backend_spawn, PkBackendSpawnFilterFunc func)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);
	backend_spawn->priv->stderr_func = func;
	return TRUE;
}

static gboolean
pk_backend_spawn_exit_timeout_cb (PkBackendSpawn *backend_spawn)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);

	/* only try to close if running */
	if (pk_spawn_is_running (backend_spawn->priv->spawn)) {
		g_debug ("closing dispatcher as running and is idle");
		pk_spawn_exit (backend_spawn->priv->spawn);
	}
	backend_spawn->priv->kill_id = 0;
	return FALSE;
}

static void
pk_backend_spawn_start_kill_timer (PkBackendSpawn *backend_spawn)
{
	gint timeout;
	PkBackendSpawnPrivate *priv = backend_spawn->priv;

	/* we finished okay, so we don't need to emulate Finished() for a crashing script */
	priv->finished = TRUE;
	g_debug ("backend marked as finished, so starting kill timer");

	if (priv->kill_id > 0)
		g_source_remove (priv->kill_id);

	/* get policy timeout */
	timeout = g_key_file_get_integer (priv->conf, "Daemon", "BackendShutdownTimeout", NULL);
	if (timeout == 0) {
		g_warning ("using built in default value");
		timeout = 5;
	}

	/* close down the dispatcher if it is still open after this much time */
	priv->kill_id = g_timeout_add_seconds (timeout, (GSourceFunc) pk_backend_spawn_exit_timeout_cb, backend_spawn);
	g_source_set_name_by_id (priv->kill_id, "[PkBackendSpawn] exit");
}

static gboolean
pk_backend_spawn_parse_stdout (PkBackendSpawn *backend_spawn,
			       PkBackendJob *job,
			       const gchar *line,
			       GError **error)
{
	guint size;
	gchar *command;
	gchar *text;
	guint64 speed;
	guint64 download_size_remaining;
	PkInfoEnum info;
	PkRestartEnum restart;
	PkGroupEnum group;
	gulong package_size;
	gint percentage;
	PkErrorEnum error_enum;
	PkStatusEnum status_enum;
	PkRestartEnum restart_enum;
	PkSigTypeEnum sig_type;
	PkUpdateStateEnum update_state_enum;
	PkMediaTypeEnum media_type_enum;
	PkDistroUpgradeEnum distro_upgrade_enum;
	PkBackendSpawnPrivate *priv = backend_spawn->priv;
	g_auto(GStrv) sections = NULL;

	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);

	/* check if output line */
	if (line == NULL)
		return FALSE;

	/* split by tab */
	sections = g_strsplit (line, "\t", 0);
	command = sections[0];

	/* get size */
	size = g_strv_length (sections);

	if (g_strcmp0 (command, "package") == 0) {
		if (size != 4) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		if (pk_package_id_check (sections[2]) == FALSE) {
			g_set_error_literal (error, 1, 0, "invalid package_id");
			return FALSE;
		}
		info = pk_info_enum_from_string (sections[1]);
		if (info == PK_INFO_ENUM_UNKNOWN) {
			g_set_error (error, 1, 0, "Info enum not recognised, and hence ignored: '%s'", sections[1]);
			return FALSE;
		}
		g_strdelimit (sections[3], PK_UNSAFE_DELIMITERS, ' ');
		if (!g_utf8_validate (sections[3], -1, NULL)) {
			g_set_error (error, 1, 0,
				     "text '%s' was not valid UTF8!",
				     sections[3]);
			return FALSE;
		}
		pk_backend_job_package (job, info, sections[2], sections[3]);
	} else if (g_strcmp0 (command, "details") == 0) {
		if (size != 8) {
			g_set_error (error, 1, 0,
				     "invalid command'%s', size %i",
				     command, size);
			return FALSE;
		}
		group = pk_group_enum_from_string (sections[4]);

		/* ITS4: ignore, checked for overflow */
		package_size = atol (sections[7]);
		if (package_size > 1073741824) {
			g_set_error_literal (error, 1, 0,
					     "package size cannot be that large");
			return FALSE;
		}
		g_strdelimit (sections[5], PK_UNSAFE_DELIMITERS, ' ');
		if (!g_utf8_validate (sections[4], -1, NULL)) {
			g_set_error (error, 1, 0,
				     "text '%s' was not valid UTF8!",
				     sections[5]);
			return FALSE;
		}
		text = g_strdup (sections[5]);
		/* convert ; to \n as we can't emit them on stdout */
		g_strdelimit (text, ";", '\n');
		pk_backend_job_details (job, sections[1], sections[2], sections[3],
					group, text, sections[6], package_size);
		g_free (text);
	} else if (g_strcmp0 (command, "finished") == 0) {
		if (size != 1) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		pk_backend_job_finished (job);
		priv->is_busy = FALSE;

		/* from this point on, we can start the kill timer */
		pk_backend_spawn_start_kill_timer (backend_spawn);

	} else if (g_strcmp0 (command, "files") == 0) {
		g_auto(GStrv) tmp = NULL;
		if (size != 3) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		tmp = g_strsplit (sections[2], ";", -1);
		pk_backend_job_files (job, sections[1], tmp);
	} else if (g_strcmp0 (command, "repo-detail") == 0) {
		if (size != 4) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		g_strdelimit (sections[2], PK_UNSAFE_DELIMITERS, ' ');
		if (!g_utf8_validate (sections[2], -1, NULL)) {
			g_set_error (error, 1, 0,
				     "text '%s' was not valid UTF8!",
				     sections[2]);
			return FALSE;
		}
		if (g_strcmp0 (sections[3], "true") == 0) {
			pk_backend_job_repo_detail (job, sections[1], sections[2], TRUE);
		} else if (g_strcmp0 (sections[3], "false") == 0) {
			pk_backend_job_repo_detail (job, sections[1], sections[2], FALSE);
		} else {
			g_set_error (error, 1, 0, "invalid qualifier '%s'", sections[3]);
			return FALSE;
		}
	} else if (g_strcmp0 (command, "updatedetail") == 0) {
		g_auto(GStrv) updates = NULL;
		g_auto(GStrv) obsoletes = NULL;
		g_auto(GStrv) vendor_urls = NULL;
		g_auto(GStrv) bugzilla_urls = NULL;
		g_auto(GStrv) cve_urls = NULL;
		if (size != 13) {
			g_set_error (error, 1, 0, "invalid command '%s', size %i", command, size);
			return FALSE;
		}
		restart = pk_restart_enum_from_string (sections[7]);
		if (restart == PK_RESTART_ENUM_UNKNOWN) {
			g_set_error (error, 1, 0, "Restart enum not recognised, and hence ignored: '%s'", sections[7]);
			return FALSE;
		}
		g_strdelimit (sections[12], PK_UNSAFE_DELIMITERS, ' ');
		if (!g_utf8_validate (sections[12], -1, NULL)) {
			g_set_error (error, 1, 0,
				     "text '%s' was not valid UTF8!",
				     sections[12]);
			return FALSE;
		}
		update_state_enum = pk_update_state_enum_from_string (sections[10]);
		/* convert ; to \n as we can't emit them on stdout */
		g_strdelimit (sections[8], ";", '\n');
		g_strdelimit (sections[9], ";", '\n');
		updates = g_strsplit (sections[2], "&", -1);
		obsoletes = g_strsplit (sections[3], "&", -1);
		vendor_urls = g_strsplit (sections[4], ";", -1);
		bugzilla_urls = g_strsplit (sections[5], ";", -1);
		cve_urls = g_strsplit (sections[6], ";", -1);
		pk_backend_job_update_detail (job,
					  sections[1],
					  updates,
					  obsoletes,
					  vendor_urls,
					  bugzilla_urls,
					  cve_urls,
					  restart,
					  sections[8],
					  sections[9],
					  update_state_enum,
					  sections[11],
					  sections[12]);
	} else if (g_strcmp0 (command, "percentage") == 0) {
		if (size != 2) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		if (!pk_strtoint (sections[1], &percentage)) {
			g_set_error (error, 1, 0, "invalid percentage value %s", sections[1]);
			return FALSE;
		} else if (percentage < 0 || percentage > 100) {
			g_set_error (error, 1, 0, "invalid percentage value %i", percentage);
			return FALSE;
		} else {
			pk_backend_job_set_percentage (job, percentage);
		}
	} else if (g_strcmp0 (command, "item-progress") == 0) {
		if (size != 4) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		if (!pk_package_id_check (sections[1])) {
			g_set_error (error, 1, 0, "invalid package_id");
			return FALSE;
		}
		status_enum = pk_status_enum_from_string (sections[2]);
		if (status_enum == PK_STATUS_ENUM_UNKNOWN) {
			g_set_error (error, 1, 0, "Status enum not recognised, and hence ignored: '%s'", sections[2]);
			return FALSE;
		}
		if (!pk_strtoint (sections[3], &percentage)) {
			g_set_error (error, 1, 0, "invalid item-progress value %s", sections[3]);
			return FALSE;
		}
		if (percentage < 0 || percentage > 100) {
			g_set_error (error, 1, 0, "invalid item-progress value %i", percentage);
			return FALSE;
		}
		pk_backend_job_set_item_progress (job,
						  sections[1],
						  status_enum,
						  percentage);
	} else if (g_strcmp0 (command, "error") == 0) {
		if (size != 3) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		error_enum = pk_error_enum_from_string (sections[1]);
		if (error_enum == PK_ERROR_ENUM_UNKNOWN) {
			g_set_error (error, 1, 0, "Error enum not recognised, and hence ignored: '%s'", sections[1]);
			return FALSE;
		}
		/* convert back all the ;'s to newlines */
		text = g_strdup (sections[2]);

		/* convert ; to \n as we can't emit them on stdout */
		g_strdelimit (text, ";", '\n');

		/* convert % else we try to format them */
		g_strdelimit (text, "%", '$');

		pk_backend_job_error_code (job, error_enum, "%s", text);
		g_free (text);
	} else if (g_strcmp0 (command, "requirerestart") == 0) {
		if (size != 3) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		restart_enum = pk_restart_enum_from_string (sections[1]);
		if (restart_enum == PK_RESTART_ENUM_UNKNOWN) {
			g_set_error (error, 1, 0, "Restart enum not recognised, and hence ignored: '%s'", sections[1]);
			return FALSE;
		}
		if (!pk_package_id_check (sections[2])) {
			g_set_error (error, 1, 0, "invalid package_id");
			return FALSE;
		}
		pk_backend_job_require_restart (job, restart_enum, sections[2]);
	} else if (g_strcmp0 (command, "status") == 0) {
		if (size != 2) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		status_enum = pk_status_enum_from_string (sections[1]);
		if (status_enum == PK_STATUS_ENUM_UNKNOWN) {
			g_set_error (error, 1, 0, "Status enum not recognised, and hence ignored: '%s'", sections[1]);
			return FALSE;
		}
		pk_backend_job_set_status (job, status_enum);
	} else if (g_strcmp0 (command, "speed") == 0) {
		if (size != 2) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		if (!pk_strtouint64 (sections[1], &speed)) {
			g_set_error (error, 1, 0,
				     "failed to parse speed: '%s'",
				     sections[1]);
			return FALSE;
		}
		pk_backend_job_set_speed (job, speed);
	} else if (g_strcmp0 (command, "download-size-remaining") == 0) {
		if (size != 2) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		if (!pk_strtouint64 (sections[1], &download_size_remaining)) {
			g_set_error (error, 1, 0,
				     "failed to parse download_size_remaining: '%s'",
				     sections[1]);
			return FALSE;
		}
		pk_backend_job_set_download_size_remaining (job, download_size_remaining);
	} else if (g_strcmp0 (command, "allow-cancel") == 0) {
		if (size != 2) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		if (g_strcmp0 (sections[1], "true") == 0) {
			pk_backend_job_set_allow_cancel (job, TRUE);
		} else if (g_strcmp0 (sections[1], "false") == 0) {
			pk_backend_job_set_allow_cancel (job, FALSE);
		} else {
			g_set_error (error, 1, 0, "invalid section '%s'", sections[1]);
			return FALSE;
		}
	} else if (g_strcmp0 (command, "no-percentage-updates") == 0) {
		if (size != 1) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);
	} else if (g_strcmp0 (command, "repo-signature-required") == 0) {

		if (size != 9) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}

		sig_type = pk_sig_type_enum_from_string (sections[8]);
		if (sig_type == PK_SIGTYPE_ENUM_UNKNOWN) {
			g_set_error (error, 1, 0, "Sig enum not recognised, and hence ignored: '%s'", sections[8]);
			return FALSE;
		}
		if (pk_strzero (sections[1])) {
			g_set_error (error, 1, 0, "package_id blank, and hence ignored: '%s'", sections[1]);
			return FALSE;
		}
		if (pk_strzero (sections[2])) {
			g_set_error (error, 1, 0, "repository name blank, and hence ignored: '%s'", sections[2]);
			return FALSE;
		}

		/* pass _all_ of the data */
		pk_backend_job_repo_signature_required (job, sections[1],
							  sections[2], sections[3], sections[4],
							  sections[5], sections[6], sections[7], sig_type);
	} else if (g_strcmp0 (command, "eula-required") == 0) {

		if (size != 5) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}

		if (pk_strzero (sections[1])) {
			g_set_error (error, 1, 0, "eula_id blank, and hence ignored: '%s'", sections[1]);
			return FALSE;
		}

		if (pk_strzero (sections[2])) {
			g_set_error (error, 1, 0, "package_id blank, and hence ignored: '%s'", sections[2]);
			return FALSE;
		}

		if (pk_strzero (sections[4])) {
			g_set_error (error, 1, 0, "agreement name blank, and hence ignored: '%s'", sections[4]);
			return FALSE;
		}

		pk_backend_job_eula_required (job, sections[1], sections[2], sections[3], sections[4]);
	} else if (g_strcmp0 (command, "media-change-required") == 0) {

		if (size != 4) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}

		media_type_enum = pk_media_type_enum_from_string (sections[1]);
		if (media_type_enum == PK_MEDIA_TYPE_ENUM_UNKNOWN) {
			g_set_error (error, 1, 0, "media type enum not recognised, and hence ignored: '%s'", sections[1]);
			return FALSE;
		}

		pk_backend_job_media_change_required (job, media_type_enum, sections[2], sections[3]);
	} else if (g_strcmp0 (command, "distro-upgrade") == 0) {

		if (size != 4) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}

		distro_upgrade_enum = pk_distro_upgrade_enum_from_string (sections[1]);
		if (distro_upgrade_enum == PK_DISTRO_UPGRADE_ENUM_UNKNOWN) {
			g_set_error (error, 1, 0, "distro upgrade enum not recognised, and hence ignored: '%s'", sections[1]);
			return FALSE;
		}
		g_strdelimit (sections[3], PK_UNSAFE_DELIMITERS, ' ');
		if (!g_utf8_validate (sections[3], -1, NULL)) {
			g_set_error (error, 1, 0,
				     "text '%s' was not valid UTF8!",
				     sections[3]);
			return FALSE;
		}

		pk_backend_job_distro_upgrade (job, distro_upgrade_enum, sections[2], sections[3]);
	} else if (g_strcmp0 (command, "category") == 0) {

		if (size != 6) {
			g_set_error (error, 1, 0, "invalid command'%s', size %i", command, size);
			return FALSE;
		}
		if (g_strcmp0 (sections[1], sections[2]) == 0) {
			g_set_error_literal (error, 1, 0, "cat_id cannot be the same as parent_id");
			return FALSE;
		}
		if (pk_strzero (sections[2])) {
			g_set_error_literal (error, 1, 0, "cat_id cannot not blank");
			return FALSE;
		}
		if (pk_strzero (sections[3])) {
			g_set_error_literal (error, 1, 0, "name cannot not blank");
			return FALSE;
		}
		g_strdelimit (sections[4], PK_UNSAFE_DELIMITERS, ' ');
		if (!g_utf8_validate (sections[4], -1, NULL)) {
			g_set_error (error, 1, 0,
				     "text '%s' was not valid UTF8!",
				     sections[4]);
			return FALSE;
		}
		if (pk_strzero (sections[5])) {
			g_set_error_literal (error, 1, 0, "icon cannot not blank");
			return FALSE;
		}
		if (g_str_has_prefix (sections[5], "/")) {
			g_set_error (error, 1, 0, "icon '%s' should be a named icon, not a path", sections[5]);
			return FALSE;
		}
		pk_backend_job_category (job, sections[1], sections[2], sections[3], sections[4], sections[5]);
	} else {
		g_set_error (error, 1, 0, "invalid command '%s'", command);
		return FALSE;
	}
	return TRUE;
}

static void
pk_backend_spawn_exit_cb (PkSpawn *spawn, PkSpawnExitType exit_enum, PkBackendSpawn *backend_spawn)
{
	gboolean ret;
	g_return_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn));

	/* reset the busy flag */
	backend_spawn->priv->is_busy = FALSE;

	/* if we force killed the process, set an error */
	if (exit_enum == PK_SPAWN_EXIT_TYPE_SIGKILL) {
		/* we just call this failed, and set an error */
		pk_backend_job_error_code (backend_spawn->priv->job, PK_ERROR_ENUM_PROCESS_KILL,
				       "Process had to be killed to be cancelled");
	}

	if (exit_enum == PK_SPAWN_EXIT_TYPE_DISPATCHER_EXIT ||
	    exit_enum == PK_SPAWN_EXIT_TYPE_DISPATCHER_CHANGED) {
		g_debug ("dispatcher exited, nothing to see here");
		return;
	}

	/* only emit if not finished */
	if (!backend_spawn->priv->finished) {
		g_debug ("script exited without doing finished, tidying up");
		ret = pk_backend_job_has_set_error_code (backend_spawn->priv->job);
		if (!ret) {
			pk_backend_job_error_code (backend_spawn->priv->job,
					       PK_ERROR_ENUM_INTERNAL_ERROR,
					       "The backend exited unexpectedly. "
					       "This is a serious error as the spawned backend did not complete the pending transaction.");
		}
		pk_backend_job_finished (backend_spawn->priv->job);
	}
}

gboolean
pk_backend_spawn_inject_data (PkBackendSpawn *backend_spawn,
			      PkBackendJob *job,
			      const gchar *line,
			      GError **error)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);

	/* do we ignore with a filter func ? */
	if (backend_spawn->priv->stdout_func != NULL) {
		if (!backend_spawn->priv->stdout_func (job, line))
			return TRUE;
	}

	return pk_backend_spawn_parse_stdout (backend_spawn, job, line, error);
}

static void
pk_backend_spawn_stdout_cb (PkBackendSpawn *spawn, const gchar *line, PkBackendSpawn *backend_spawn)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	ret = pk_backend_spawn_inject_data (backend_spawn,
					    backend_spawn->priv->job,
					    line,
					    &error);
	if (!ret)
		g_warning ("failed to parse: %s: %s", line, error->message);
}

static void
pk_backend_spawn_stderr_cb (PkBackendSpawn *spawn, const gchar *line, PkBackendSpawn *backend_spawn)
{
	gboolean ret;
	g_return_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn));

	/* do we ignore with a filter func ? */
	if (backend_spawn->priv->stderr_func != NULL) {
		ret = backend_spawn->priv->stderr_func (backend_spawn->priv->job, line);
		if (!ret)
			return;
	}
	g_warning ("STDERR: %s", line);
}

static gchar **
pk_backend_spawn_get_envp (PkBackendSpawn *backend_spawn)
{
	gchar **envp;
	gchar **env_item;
	gchar *uri;
	const gchar *value;
	guint i;
	guint cache_age;
	GHashTableIter env_iter;
	gchar *env_key;
	gchar *env_value;
	gboolean ret;
	PkBackendSpawnPrivate *priv = backend_spawn->priv;
	gboolean keep_environment;
	g_autofree gchar *eulas = NULL;
	const gchar *locale = NULL;
	const gchar *no_proxy = NULL;
	const gchar *pac = NULL;
	const gchar *proxy_ftp = NULL;
	const gchar *proxy_http = NULL;
	const gchar *proxy_https = NULL;
	const gchar *proxy_socks = NULL;
	g_autofree gchar *transaction_id = NULL;
	g_autoptr(GHashTable) env_table = NULL;

	env_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	keep_environment = g_key_file_get_boolean (backend_spawn->priv->conf,
						   "Daemon",
						   "KeepEnvironment",
						   NULL);
	g_debug ("keep_environment: %i", keep_environment);

	/* copy environment if so specified (for debugging) */
	if (keep_environment) {
		g_auto(GStrv) environ = g_get_environ ();
		for (env_item = environ; env_item && *env_item; env_item++) {
			g_auto(GStrv) env_item_split = NULL;
			env_item_split = g_strsplit (*env_item, "=", 2);
			if (env_item_split && (g_strv_length (env_item_split) == 2))
				g_hash_table_replace (env_table, g_strdup (env_item_split[0]),
						g_strdup (env_item_split[1]));

		}
	}

	/* accepted eulas */
	eulas = pk_backend_get_accepted_eula_string (priv->backend);
	if (eulas != NULL)
		g_hash_table_replace (env_table, g_strdup ("accepted_eulas"), g_strdup (eulas));

	/* http_proxy */
	proxy_http = pk_backend_job_get_proxy_http (priv->job);
	if (!pk_strzero (proxy_http)) {
		uri = pk_backend_convert_uri (proxy_http);
		g_hash_table_replace (env_table, g_strdup ("http_proxy"), uri);
	}

	/* https_proxy */
	proxy_https = pk_backend_job_get_proxy_https (priv->job);
	if (!pk_strzero (proxy_https)) {
		uri = pk_backend_convert_uri (proxy_https);
		g_hash_table_replace (env_table, g_strdup ("https_proxy"), uri);
	}

	/* ftp_proxy */
	proxy_ftp = pk_backend_job_get_proxy_ftp (priv->job);
	if (!pk_strzero (proxy_ftp)) {
		uri = pk_backend_convert_uri (proxy_ftp);
		g_hash_table_replace (env_table, g_strdup ("ftp_proxy"), uri);
	}

	/* socks_proxy */
	proxy_socks = pk_backend_job_get_proxy_socks (priv->job);
	if (!pk_strzero (proxy_socks)) {
		uri = pk_backend_convert_uri_socks (proxy_socks);
		g_hash_table_replace (env_table, g_strdup ("all_proxy"), uri);
	}

	/* no_proxy */
	no_proxy = pk_backend_job_get_no_proxy (priv->job);
	if (!pk_strzero (no_proxy)) {
		g_hash_table_replace (env_table, g_strdup ("no_proxy"),
		                      g_strdup (no_proxy));
	}

	/* pac */
	pac = pk_backend_job_get_pac (priv->job);
	if (!pk_strzero (pac)) {
		uri = pk_backend_convert_uri (pac);
		g_hash_table_replace (env_table, g_strdup ("pac"), uri);
	}

	/* LANG */
	locale = pk_backend_job_get_locale (priv->job);
	if (!pk_strzero (locale))
		g_hash_table_replace (env_table, g_strdup ("LANG"), g_strdup (locale));

	/* FRONTEND SOCKET */
	value = pk_backend_job_get_frontend_socket (priv->job);
	if (!pk_strzero (value))
		g_hash_table_replace (env_table, g_strdup ("FRONTEND_SOCKET"), g_strdup (value));

	/* NETWORK */
	ret = pk_backend_is_online (priv->backend);
	g_hash_table_replace (env_table, g_strdup ("NETWORK"), g_strdup (ret ? "TRUE" : "FALSE"));

	/* BACKGROUND */
	ret = pk_backend_job_get_background (priv->job);
	g_hash_table_replace (env_table, g_strdup ("BACKGROUND"), g_strdup (ret ? "TRUE" : "FALSE"));

	/* INTERACTIVE */
	ret = pk_backend_job_get_interactive (priv->job);
	g_hash_table_replace (env_table, g_strdup ("INTERACTIVE"), g_strdup (ret ? "TRUE" : "FALSE"));

	/* UID */
	ret = pk_backend_job_get_interactive (priv->job);
	g_hash_table_replace (env_table,
			      g_strdup ("UID"),
			      g_strdup_printf ("%u", pk_backend_job_get_uid (priv->job)));

	/* CACHE_AGE */
	cache_age = pk_backend_job_get_cache_age (priv->job);
	if (cache_age == G_MAXUINT) {
		g_hash_table_replace (env_table,
				      g_strdup ("CACHE_AGE"),
				      g_strdup ("-1"));
	} else if (cache_age > 0) {
		g_hash_table_replace (env_table,
				      g_strdup ("CACHE_AGE"),
				      g_strdup_printf ("%u", cache_age));
	}

	/* copy hashed environment key/value pairs to envp */
	envp = g_new0 (gchar *, g_hash_table_size (env_table) + 1);
	g_hash_table_iter_init (&env_iter, env_table);
	i = 0;
	while (g_hash_table_iter_next (&env_iter, (void**)&env_key, (void**)&env_value)) {
		env_key = g_strdup (env_key);
		env_value = g_strdup (env_value);
		if (!keep_environment) {
			/* ensure malicious users can't inject anything from the session,
			 * unless keeping the environment is specified (used for debugging) */
			g_strdelimit (env_key, "\\;{}[]()*?%\n\r\t", '_');
			g_strdelimit (env_value, "\\;{}[]()*?%\n\r\t", '_');
		}
		envp[i] = g_strdup_printf ("%s=%s", env_key, env_value);
		g_debug ("setting envp '%s'", envp[i]);
		g_free (env_key);
		g_free (env_value);
		i++;
	}
	return envp;
}

#ifdef ENABLE_STRACE
 #define PK_BACKEND_SPAWN_ARGV0		4
#else
 #define PK_BACKEND_SPAWN_ARGV0		0
#endif

/**
 * pk_backend_spawn_va_list_to_argv:
 * @string_first: the first string
 * @args: any subsequant string's
 *
 * Form a composite string array of the va_list
 *
 * Return value: the string array, or %NULL if invalid
 **/
static gchar **
pk_backend_spawn_va_list_to_argv (const gchar *string_first, va_list *args)
{
	GPtrArray *ptr_array;
	gchar *value_temp;
	guint i;

	g_return_val_if_fail (args != NULL, NULL);
	g_return_val_if_fail (string_first != NULL, NULL);

	/* find how many elements we have in a temp array */
	ptr_array = g_ptr_array_new ();
#ifdef ENABLE_STRACE
	g_ptr_array_add (ptr_array, g_strdup ("strace"));
	g_ptr_array_add (ptr_array, g_strdup ("-T"));
	g_ptr_array_add (ptr_array, g_strdup ("-tt"));
	g_ptr_array_add (ptr_array, g_strdup_printf ("-o/var/log/PackageKit-strace-%06i",
						     g_random_int_range (1, 999999)));
#endif
	g_ptr_array_add (ptr_array, g_strdup (string_first));

	/* process all the va_list entries */
	for (i = 0;; i++) {
		value_temp = va_arg (*args, gchar *);
		if (value_temp == NULL)
			break;
		g_ptr_array_add (ptr_array, g_strdup (value_temp));
	}

	g_ptr_array_add (ptr_array, NULL);
	return (gchar **) g_ptr_array_free (ptr_array, FALSE);
}

static gboolean
pk_backend_spawn_helper_va_list (PkBackendSpawn *backend_spawn,
				 PkBackendJob *job,
				 const gchar *executable,
				 va_list *args)
{
	gboolean background;
	PkBackendSpawnPrivate *priv = backend_spawn->priv;
	PkSpawnArgvFlags flags = PK_SPAWN_ARGV_FLAGS_NONE;
#ifdef SOURCEROOTDIR
	const gchar *directory;
#endif
	g_autoptr(GError) error = NULL;
	g_autofree gchar *filename = NULL;
	g_auto(GStrv) argv = NULL;
	g_auto(GStrv) envp = NULL;

	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);

	/* convert to a argv */
	argv = pk_backend_spawn_va_list_to_argv (executable, args);
	if (argv == NULL) {
		g_warning ("argv NULL");
		return FALSE;
	}

#ifdef SOURCEROOTDIR
	/* prefer the local version */
	directory = priv->name;
	if (g_str_has_prefix (directory, "test_"))
		directory = "test";

	filename = g_build_filename (SOURCEROOTDIR, "backends", directory, "helpers",
				     argv[PK_BACKEND_SPAWN_ARGV0], NULL);
	if (g_file_test (filename, G_FILE_TEST_EXISTS) == FALSE) {
		g_debug ("local helper not found '%s'", filename);
		g_free (filename);
		filename = g_build_filename (SOURCEROOTDIR, "backends", directory,
					     argv[PK_BACKEND_SPAWN_ARGV0], NULL);
	}
	if (g_file_test (filename, G_FILE_TEST_EXISTS) == FALSE) {
		g_debug ("local helper not found '%s'", filename);
		g_free (filename);
		filename = g_build_filename (DATADIR, "PackageKit", "helpers",
					     priv->name, argv[PK_BACKEND_SPAWN_ARGV0], NULL);
	}
#else
	filename = g_build_filename (DATADIR, "PackageKit", "helpers",
				     priv->name, argv[PK_BACKEND_SPAWN_ARGV0], NULL);
#endif
	g_debug ("using spawn filename %s", filename);

	/* replace the filename with the full path */
	g_free (argv[PK_BACKEND_SPAWN_ARGV0]);
	argv[PK_BACKEND_SPAWN_ARGV0] = g_strdup (filename);

	/* copy idle setting from backend to PkSpawn instance */
	background = pk_backend_job_get_background (job);
	g_object_set (priv->spawn,
		      "background", (background == TRUE),
		      NULL);

#ifdef ENABLE_STRACE
	/* we can't reuse when using strace */
	flags |= PK_SPAWN_ARGV_FLAGS_NEVER_REUSE;
#endif

	priv->finished = FALSE;
	envp = pk_backend_spawn_get_envp (backend_spawn);
	if (!pk_spawn_argv (priv->spawn, argv, envp, flags, &error)) {
		pk_backend_job_error_code (priv->job,
					   PK_ERROR_ENUM_INTERNAL_ERROR,
					   "Spawn of helper '%s' failed: %s",
					   argv[PK_BACKEND_SPAWN_ARGV0],
					   error->message);
		pk_backend_job_finished (priv->job);
		return FALSE;
	}
	return TRUE;
}

const gchar *
pk_backend_spawn_get_name (PkBackendSpawn *backend_spawn)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), NULL);
	return backend_spawn->priv->name;
}

gboolean
pk_backend_spawn_set_name (PkBackendSpawn *backend_spawn, const gchar *name)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	g_free (backend_spawn->priv->name);
	backend_spawn->priv->name = g_strdup (name);
	return TRUE;
}

gboolean
pk_backend_spawn_kill (PkBackendSpawn *backend_spawn)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);

	/* set an error as the script will just exit without doing finished */
	pk_backend_job_error_code (backend_spawn->priv->job,
			       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
			       "the script was killed as the action was cancelled");
	pk_spawn_kill (backend_spawn->priv->spawn);
	return TRUE;
}

gboolean
pk_backend_spawn_is_busy (PkBackendSpawn *backend_spawn)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);
	return backend_spawn->priv->is_busy;
}

gboolean
pk_backend_spawn_exit (PkBackendSpawn *backend_spawn)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);
	pk_spawn_exit (backend_spawn->priv->spawn);
	return TRUE;
}

gboolean
pk_backend_spawn_helper (PkBackendSpawn *backend_spawn,
			 PkBackendJob *job,
			 const gchar *first_element, ...)
{
	gboolean ret = TRUE;
	va_list args;

	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);
	g_return_val_if_fail (first_element != NULL, FALSE);
	g_return_val_if_fail (backend_spawn->priv->name != NULL, FALSE);

	/* save this */
	backend_spawn->priv->is_busy = TRUE;
	backend_spawn->priv->job = job;
	backend_spawn->priv->backend = g_object_ref (pk_backend_job_get_backend (job));

	/* don't auto-kill this */
	if (backend_spawn->priv->kill_id > 0) {
		g_source_remove (backend_spawn->priv->kill_id);
		backend_spawn->priv->kill_id = 0;
	}

	/* get the argument list */
	va_start (args, first_element);
	ret = pk_backend_spawn_helper_va_list (backend_spawn, job, first_element, &args);
	va_end (args);

	return ret;
}

void
pk_backend_spawn_set_allow_sigkill (PkBackendSpawn *backend_spawn, gboolean allow_sigkill)
{
	g_return_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn));
	g_object_set (backend_spawn->priv->spawn,
		      "allow-sigkill", allow_sigkill,
		      NULL);
}

static void
pk_backend_spawn_finalize (GObject *object)
{
	PkBackendSpawn *backend_spawn;

	g_return_if_fail (PK_IS_BACKEND_SPAWN (object));

	backend_spawn = PK_BACKEND_SPAWN (object);

	if (backend_spawn->priv->kill_id > 0)
		g_source_remove (backend_spawn->priv->kill_id);

	g_free (backend_spawn->priv->name);
	g_key_file_unref (backend_spawn->priv->conf);
	g_object_unref (backend_spawn->priv->spawn);
	if (backend_spawn->priv->backend != NULL)
		g_object_unref (backend_spawn->priv->backend);

	G_OBJECT_CLASS (pk_backend_spawn_parent_class)->finalize (object);
}

static void
pk_backend_spawn_class_init (PkBackendSpawnClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_backend_spawn_finalize;
	g_type_class_add_private (klass, sizeof (PkBackendSpawnPrivate));
}

static void
pk_backend_spawn_init (PkBackendSpawn *backend_spawn)
{
	backend_spawn->priv = PK_BACKEND_SPAWN_GET_PRIVATE (backend_spawn);
}

PkBackendSpawn *
pk_backend_spawn_new (GKeyFile *conf)
{
	PkBackendSpawn *backend_spawn;
	backend_spawn = g_object_new (PK_TYPE_BACKEND_SPAWN, NULL);
	backend_spawn->priv->conf = g_key_file_ref (conf);
	backend_spawn->priv->spawn = pk_spawn_new (backend_spawn->priv->conf);
	g_signal_connect (backend_spawn->priv->spawn, "exit",
			  G_CALLBACK (pk_backend_spawn_exit_cb), backend_spawn);
	g_signal_connect (backend_spawn->priv->spawn, "stdout",
			  G_CALLBACK (pk_backend_spawn_stdout_cb), backend_spawn);
	g_signal_connect (backend_spawn->priv->spawn, "stderr",
			  G_CALLBACK (pk_backend_spawn_stderr_cb), backend_spawn);
	return PK_BACKEND_SPAWN (backend_spawn);
}

