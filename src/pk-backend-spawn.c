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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <glib/gprintf.h>
#include <gmodule.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-package-id.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-backend-internal.h"
#include "pk-backend-spawn.h"
#include "pk-marshal.h"
#include "pk-spawn.h"
#include "pk-time.h"
#include "pk-inhibit.h"
#include "pk-conf.h"

extern char **environ;

#define PK_BACKEND_SPAWN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND_SPAWN, PkBackendSpawnPrivate))
#define PK_BACKEND_SPAWN_PERCENTAGE_INVALID	101

struct PkBackendSpawnPrivate
{
	PkSpawn			*spawn;
	PkBackend		*backend;
	gchar			*name;
	guint			 kill_id;
	PkConf			*conf;
	gboolean		 finished;
	gboolean		 allow_sigkill;
	PkBackendSpawnFilterFunc stdout_func;
	PkBackendSpawnFilterFunc stderr_func;
};

G_DEFINE_TYPE (PkBackendSpawn, pk_backend_spawn, G_TYPE_OBJECT)

/**
 * pk_backend_spawn_set_filter_stdout:
 **/
gboolean
pk_backend_spawn_set_filter_stdout (PkBackendSpawn *backend_spawn, PkBackendSpawnFilterFunc func)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);
	backend_spawn->priv->stdout_func = func;
	return TRUE;
}

/**
 * pk_backend_spawn_set_filter_stderr:
 **/
gboolean
pk_backend_spawn_set_filter_stderr (PkBackendSpawn *backend_spawn, PkBackendSpawnFilterFunc func)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);
	backend_spawn->priv->stderr_func = func;
	return TRUE;
}

/**
 * pk_backend_spawn_exit_timeout_cb:
 **/
static gboolean
pk_backend_spawn_exit_timeout_cb (PkBackendSpawn *backend_spawn)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);

	/* only try to close if running */
	ret = pk_spawn_is_running (backend_spawn->priv->spawn);
	if (ret) {
		egg_debug ("closing dispatcher as running and is idle");
		pk_spawn_exit (backend_spawn->priv->spawn);
	}
	return FALSE;
}

/**
 * pk_backend_spawn_start_kill_timer:
 **/
static void
pk_backend_spawn_start_kill_timer (PkBackendSpawn *backend_spawn)
{
	gint timeout;
	PkBackendSpawnPrivate *priv = backend_spawn->priv;

	/* we finished okay, so we don't need to emulate Finished() for a crashing script */
	priv->finished = TRUE;
	egg_debug ("backend marked as finished, so starting kill timer");

	if (priv->kill_id > 0)
		g_source_remove (priv->kill_id);

	/* get policy timeout */
	timeout = pk_conf_get_int (priv->conf, "BackendShutdownTimeout");
	if (timeout == PK_CONF_VALUE_INT_MISSING) {
		egg_warning ("using built in default value");
		timeout = 5;
	}

	/* close down the dispatcher if it is still open after this much time */
	priv->kill_id = g_timeout_add_seconds (timeout, (GSourceFunc) pk_backend_spawn_exit_timeout_cb, backend_spawn);
}

/**
 * pk_backend_spawn_parse_stdout:
 *
 * If you are editing this function creating a new backend,
 * then you are probably doing something wrong.
 **/
static gboolean
pk_backend_spawn_parse_stdout (PkBackendSpawn *backend_spawn, const gchar *line)
{
	gchar **sections;
	guint size;
	gchar *command;
	gchar *text;
	gboolean ret = TRUE;
	PkInfoEnum info;
	PkRestartEnum restart;
	PkGroupEnum group;
	gulong package_size;
	gint percentage;
	PkErrorEnum error_enum;
	PkStatusEnum status_enum;
	PkMessageEnum message_enum;
	PkRestartEnum restart_enum;
	PkSigTypeEnum sig_type;
	PkUpdateStateEnum update_state_enum;
	PkMediaTypeEnum media_type_enum;
	PkDistroUpgradeEnum distro_upgrade_enum;

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
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		if (pk_package_id_check (sections[2]) == FALSE) {
			egg_warning ("invalid package_id");
			ret = FALSE;
			goto out;
		}
		info = pk_info_enum_from_text (sections[1]);
		if (info == PK_INFO_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "Info enum not recognised, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
		pk_backend_package (backend_spawn->priv->backend, info, sections[2], sections[3]);
	} else if (g_strcmp0 (command, "details") == 0) {
		if (size != 7) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		group = pk_group_enum_from_text (sections[3]);

		/* ITS4: ignore, checked for overflow */
		package_size = atol (sections[6]);
		if (package_size > 1073741824) {
			egg_warning ("package size cannot be larger than one Gb");
			ret = FALSE;
			goto out;
		}
		text = g_strdup (sections[4]);
		/* convert ; to \n as we can't emit them on stdout */
		g_strdelimit (text, ";", '\n');
		pk_backend_details (backend_spawn->priv->backend, sections[1], sections[2],
					group, text, sections[5], package_size);
		g_free (text);
	} else if (g_strcmp0 (command, "finished") == 0) {
		if (size != 1) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}

		pk_backend_finished (backend_spawn->priv->backend);

		/* from this point on, we can start the kill timer */
		pk_backend_spawn_start_kill_timer (backend_spawn);

	} else if (g_strcmp0 (command, "files") == 0) {
		if (size != 3) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		pk_backend_files (backend_spawn->priv->backend, sections[1], sections[2]);
	} else if (g_strcmp0 (command, "repo-detail") == 0) {
		if (size != 4) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		if (g_strcmp0 (sections[3], "true") == 0) {
			pk_backend_repo_detail (backend_spawn->priv->backend, sections[1], sections[2], TRUE);
		} else if (g_strcmp0 (sections[3], "false") == 0) {
			pk_backend_repo_detail (backend_spawn->priv->backend, sections[1], sections[2], FALSE);
		} else {
			egg_warning ("invalid qualifier '%s'", sections[3]);
			ret = FALSE;
			goto out;
		}
	} else if (g_strcmp0 (command, "updatedetail") == 0) {
		if (size != 13) {
			egg_warning ("invalid command '%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		restart = pk_restart_enum_from_text (sections[7]);
		if (restart == PK_RESTART_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "Restart enum not recognised, and hence ignored: '%s'", sections[7]);
			ret = FALSE;
			goto out;
		}
		update_state_enum = pk_update_state_enum_from_text (sections[10]);
		/* convert ; to \n as we can't emit them on stdout */
		g_strdelimit (sections[8], ";", '\n');
		g_strdelimit (sections[9], ";", '\n');
		pk_backend_update_detail (backend_spawn->priv->backend, sections[1],
					  sections[2], sections[3], sections[4],
					  sections[5], sections[6], restart, sections[8],
					  sections[9], update_state_enum,
					  sections[11], sections[12]);
	} else if (g_strcmp0 (command, "percentage") == 0) {
		if (size != 2) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		ret = egg_strtoint (sections[1], &percentage);
		if (!ret) {
			egg_warning ("invalid percentage value %s", sections[1]);
		} else if (percentage < 0 || percentage > 100) {
			egg_warning ("invalid percentage value %i", percentage);
			ret = FALSE;
		} else {
			pk_backend_set_percentage (backend_spawn->priv->backend, percentage);
		}
	} else if (g_strcmp0 (command, "subpercentage") == 0) {
		if (size != 2) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		ret = egg_strtoint (sections[1], &percentage);
		if (!ret) {
			egg_warning ("invalid subpercentage value %s", sections[1]);
		} else if (percentage < 0 || percentage > 100) {
			egg_warning ("invalid subpercentage value %i", percentage);
			ret = FALSE;
		} else {
			pk_backend_set_sub_percentage (backend_spawn->priv->backend, percentage);
		}
	} else if (g_strcmp0 (command, "error") == 0) {
		if (size != 3) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		error_enum = pk_error_enum_from_text (sections[1]);
		if (error_enum == PK_ERROR_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "Error enum not recognised, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
		/* convert back all the ;'s to newlines */
		text = g_strdup (sections[2]);

		/* convert ; to \n as we can't emit them on stdout */
		g_strdelimit (text, ";", '\n');

		/* convert % else we try to format them */
		g_strdelimit (text, "%", '$');

		pk_backend_error_code (backend_spawn->priv->backend, error_enum, text);
		g_free (text);
	} else if (g_strcmp0 (command, "requirerestart") == 0) {
		if (size != 3) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		restart_enum = pk_restart_enum_from_text (sections[1]);
		if (restart_enum == PK_RESTART_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "Restart enum not recognised, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
		if (!pk_package_id_check (sections[2])) {
			egg_warning ("invalid package_id");
			ret = FALSE;
			goto out;
		}
		pk_backend_require_restart (backend_spawn->priv->backend, restart_enum, sections[2]);
	} else if (g_strcmp0 (command, "message") == 0) {
		if (size != 3) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		message_enum = pk_message_enum_from_text (sections[1]);
		if (message_enum == PK_MESSAGE_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "Message enum not recognised, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
		text = g_strdup (sections[2]);
		/* convert ; to \n as we can't emit them on stdout */
		g_strdelimit (text, ";", '\n');
		pk_backend_message (backend_spawn->priv->backend, message_enum, text);
		g_free (text);
	} else if (g_strcmp0 (command, "change-transaction-data") == 0) {
		if (size != 2) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		pk_backend_set_transaction_data (backend_spawn->priv->backend, sections[1]);
	} else if (g_strcmp0 (command, "status") == 0) {
		if (size != 2) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		status_enum = pk_status_enum_from_text (sections[1]);
		if (status_enum == PK_STATUS_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "Status enum not recognised, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
		pk_backend_set_status (backend_spawn->priv->backend, status_enum);
	} else if (g_strcmp0 (command, "allow-cancel") == 0) {
		if (size != 2) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		if (g_strcmp0 (sections[1], "true") == 0) {
			pk_backend_set_allow_cancel (backend_spawn->priv->backend, TRUE);
		} else if (g_strcmp0 (sections[1], "false") == 0) {
			pk_backend_set_allow_cancel (backend_spawn->priv->backend, FALSE);
		} else {
			egg_warning ("invalid section '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
	} else if (g_strcmp0 (command, "no-percentage-updates") == 0) {
		if (size != 1) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		pk_backend_set_percentage (backend_spawn->priv->backend, PK_BACKEND_PERCENTAGE_INVALID);
	} else if (g_strcmp0 (command, "repo-signature-required") == 0) {

		if (size != 9) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}

		sig_type = pk_sig_type_enum_from_text (sections[8]);
		if (sig_type == PK_SIGTYPE_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "Sig enum not recognised, and hence ignored: '%s'", sections[8]);
			ret = FALSE;
			goto out;
		}
		if (egg_strzero (sections[1])) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "package_id blank, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
		if (egg_strzero (sections[2])) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "repository name blank, and hence ignored: '%s'", sections[2]);
			ret = FALSE;
			goto out;
		}

		/* pass _all_ of the data */
		ret = pk_backend_repo_signature_required (backend_spawn->priv->backend, sections[1],
							  sections[2], sections[3], sections[4],
							  sections[5], sections[6], sections[7], sig_type);
		goto out;
	} else if (g_strcmp0 (command, "media-change-required") == 0) {

		if (size != 4) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}

		media_type_enum = pk_media_type_enum_from_text (sections[1]);
		if (media_type_enum == PK_MEDIA_TYPE_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "media type enum not recognised, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}

		ret = pk_backend_media_change_required (backend_spawn->priv->backend, media_type_enum, sections[2], sections[3]);
		goto out;
	} else if (g_strcmp0 (command, "distro-upgrade") == 0) {

		if (size != 4) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}

		distro_upgrade_enum = pk_distro_upgrade_enum_from_text (sections[1]);
		if (distro_upgrade_enum == PK_DISTRO_UPGRADE_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "distro upgrade enum not recognised, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}

		ret = pk_backend_distro_upgrade (backend_spawn->priv->backend, distro_upgrade_enum, sections[2], sections[3]);
		goto out;
	} else if (g_strcmp0 (command, "category") == 0) {

		if (size != 6) {
			egg_warning ("invalid command'%s', size %i", command, size);
			ret = FALSE;
			goto out;
		}
		if (g_strcmp0 (sections[1], sections[2]) == 0) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR, "cat_id cannot be the same as parent_id");
			ret = FALSE;
			goto out;
		}
		if (egg_strzero (sections[2])) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR, "cat_id cannot not blank");
			ret = FALSE;
			goto out;
		}
		if (egg_strzero (sections[3])) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR, "name cannot not blank");
			ret = FALSE;
			goto out;
		}
		if (egg_strzero (sections[5])) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR, "icon cannot not blank");
			ret = FALSE;
			goto out;
		}
		if (g_str_has_prefix (sections[5], "/")) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR, "icon '%s' should be a named icon, not a path", sections[5]);
			ret = FALSE;
			goto out;
		}
		ret = pk_backend_category (backend_spawn->priv->backend, sections[1], sections[2], sections[3], sections[4], sections[5]);
		goto out;
	} else {
		egg_warning ("invalid command '%s'", command);
	}
out:
	g_strfreev (sections);
	return ret;
}

/**
 * pk_backend_spawn_exit_cb:
 **/
static void
pk_backend_spawn_exit_cb (PkSpawn *spawn, PkSpawnExitType exit_enum, PkBackendSpawn *backend_spawn)
{
	g_return_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn));

	/* if we force killed the process, set an error */
	if (exit_enum == PK_SPAWN_EXIT_TYPE_SIGKILL) {
		/* we just call this failed, and set an error */
		pk_backend_error_code (backend_spawn->priv->backend, PK_ERROR_ENUM_PROCESS_KILL,
				       "Process had to be killed to be cancelled");
	}

	/* only emit if not finished */
	if (!backend_spawn->priv->finished &&
	    exit_enum != PK_SPAWN_EXIT_TYPE_DISPATCHER_CHANGED) {
		egg_warning ("script exited without doing finished");
		pk_backend_finished (backend_spawn->priv->backend);
	}
}

/**
 * pk_backend_spawn_stdout_cb:
 **/
static void
pk_backend_spawn_stdout_cb (PkBackendSpawn *spawn, const gchar *line, PkBackendSpawn *backend_spawn)
{
	gboolean ret;
	g_return_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn));

	/* do we ignore with a filter func ? */
	if (backend_spawn->priv->stdout_func != NULL) {
		ret = backend_spawn->priv->stdout_func (backend_spawn->priv->backend, line);
		if (!ret)
			return;
	}

	ret = pk_backend_spawn_parse_stdout (backend_spawn, line);
	if (!ret)
		egg_debug ("failed to parse '%s'", line);
}

/**
 * pk_backend_spawn_stderr_cb:
 **/
static void
pk_backend_spawn_stderr_cb (PkBackendSpawn *spawn, const gchar *line, PkBackendSpawn *backend_spawn)
{
	gboolean ret;
	g_return_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn));

	/* do we ignore with a filter func ? */
	if (backend_spawn->priv->stderr_func != NULL) {
		ret = backend_spawn->priv->stderr_func (backend_spawn->priv->backend, line);
		if (!ret)
			return;
	}

	/* send warning up to session, this is never going to be pretty... */
	egg_warning ("STDERR: %s", line);
	pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR, "%s", line);
}

/**
 * pk_backend_spawn_convert_uri:
 *
 * Our proxy variable is typically 'username:password@server:port'
 * but http_proxy expects 'http://username:password@server:port/'
 **/
static gchar *
pk_backend_spawn_convert_uri (const gchar *proxy)
{
	GString *string;
	string = g_string_new (proxy);

	/* if we didn't specify a prefix, add a default one */
	if (!g_str_has_prefix (proxy, "http://") &&
	    !g_str_has_prefix (proxy, "https://") &&
	    !g_str_has_prefix (proxy, "ftp://")) {
		g_string_prepend (string, "http://");
	}

	/* if we didn't specify a trailing slash, add one */
	if (!g_str_has_suffix (proxy, "/")) {
		g_string_append_c (string, '/');
	}

	return g_string_free (string, FALSE);
}

/**
 * pk_backend_spawn_get_envp:
 *
 * Return all the environment variables the script will need
 **/
static gchar **
pk_backend_spawn_get_envp (PkBackendSpawn *backend_spawn)
{
	gchar **envp;
	gchar **env_item;
	gchar **env_item_split;
	gchar *value;
	gchar *uri;
	guint i;
	GHashTable *env_table;
	GHashTableIter env_iter;
	gchar *env_key;
	gchar *env_value;
	gboolean ret;

	gboolean keep_environment =
		pk_backend_get_keep_environment (backend_spawn->priv->backend);

	env_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	egg_debug ("keep_environment: %i", keep_environment);

	if (keep_environment) {
		/* copy environment if so specified (for debugging) */
		for (env_item = environ; env_item && *env_item; env_item++) {
			env_item_split = g_strsplit (*env_item, "=", 2);

			if (env_item_split && (g_strv_length (env_item_split) == 2))
				g_hash_table_replace (env_table, g_strdup (env_item_split[0]),
						g_strdup (env_item_split[1]));

			g_strfreev (env_item_split);
		}
	}

	/* http_proxy */
	value = pk_backend_get_proxy_http (backend_spawn->priv->backend);
	if (!egg_strzero (value)) {
		uri = pk_backend_spawn_convert_uri (value);
		g_hash_table_replace (env_table, g_strdup ("http_proxy"), uri);
	}
	g_free (value);

	/* ftp_proxy */
	value = pk_backend_get_proxy_ftp (backend_spawn->priv->backend);
	if (!egg_strzero (value)) {
		uri = pk_backend_spawn_convert_uri (value);
		g_hash_table_replace (env_table, g_strdup ("ftp_proxy"), uri);
	}
	g_free (value);

	/* LANG */
	value = pk_backend_get_locale (backend_spawn->priv->backend);
	if (!egg_strzero (value))
		g_hash_table_replace (env_table, g_strdup ("LANG"), value);
	else
		g_free (value);

	/* NETWORK */
	ret = pk_backend_is_online (backend_spawn->priv->backend);
	g_hash_table_replace (env_table, g_strdup ("NETWORK"), g_strdup(ret ? "TRUE" : "FALSE"));

	/* IDLE */
	ret = pk_backend_use_idle_bandwidth (backend_spawn->priv->backend);
	g_hash_table_replace (env_table, g_strdup ("IDLE"), g_strdup(ret ? "TRUE" : "FALSE"));

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
		egg_debug ("setting envp '%s'", envp[i]);
		g_free (env_key);
		g_free (env_value);
		i++;
	}

	g_hash_table_destroy (env_table);

	return envp;
}

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
	gchar **array;
	gchar *value_temp;
	guint i;

	g_return_val_if_fail (args != NULL, NULL);
	g_return_val_if_fail (string_first != NULL, NULL);

	/* find how many elements we have in a temp array */
	ptr_array = g_ptr_array_new ();
	g_ptr_array_add (ptr_array, g_strdup (string_first));

	/* process all the va_list entries */
	for (i=0;; i++) {
		value_temp = va_arg (*args, gchar *);
		if (value_temp == NULL)
			break;
		g_ptr_array_add (ptr_array, g_strdup (value_temp));
	}

	/* convert the array to a strv type */
	array = pk_ptr_array_to_strv (ptr_array);

	/* get rid of the array, and free the contents */
	g_ptr_array_foreach (ptr_array, (GFunc) g_free, NULL);
	g_ptr_array_free (ptr_array, TRUE);
	return array;
}

/**
 * pk_backend_spawn_helper_va_list:
 **/
static gboolean
pk_backend_spawn_helper_va_list (PkBackendSpawn *backend_spawn, const gchar *executable, va_list *args)
{
	gboolean ret;
	gchar *filename;
	gchar **argv;
	gchar **envp;
	PkTristate background;
#if PK_BUILD_LOCAL
	const gchar *directory;
#endif

	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);

	/* convert to a argv */
	argv = pk_backend_spawn_va_list_to_argv (executable, args);
	if (argv == NULL) {
		egg_warning ("argv NULL");
		return FALSE;
	}

#if PK_BUILD_LOCAL
	/* prefer the local version */
	directory = backend_spawn->priv->name;
	if (g_str_has_prefix (directory, "test_"))
		directory = "test";

	filename = g_build_filename ("..", "backends", directory, "helpers", argv[0], NULL);
	if (g_file_test (filename, G_FILE_TEST_EXISTS) == FALSE) {
		egg_debug ("local helper not found '%s'", filename);
		g_free (filename);
		filename = g_build_filename ("..", "backends", directory, argv[0], NULL);
	}
	if (g_file_test (filename, G_FILE_TEST_EXISTS) == FALSE) {
		egg_debug ("local helper not found '%s'", filename);
		g_free (filename);
		filename = g_build_filename (DATADIR, "PackageKit", "helpers", backend_spawn->priv->name, argv[0], NULL);
	}
#else
	filename = g_build_filename (DATADIR, "PackageKit", "helpers", backend_spawn->priv->name, argv[0], NULL);
#endif
	egg_debug ("using spawn filename %s", filename);

	/* replace the filename with the full path */
	g_free (argv[0]);
	argv[0] = g_strdup (filename);

	/* copy idle setting from backend to PkSpawn instance */
	g_object_get (backend_spawn->priv->backend, "background", &background, NULL);
	g_object_set (backend_spawn->priv->spawn, "background", (background == PK_TRISTATE_TRUE), NULL);

	backend_spawn->priv->finished = FALSE;
	envp = pk_backend_spawn_get_envp (backend_spawn);
	ret = pk_spawn_argv (backend_spawn->priv->spawn, argv, envp);
	if (!ret) {
		pk_backend_error_code (backend_spawn->priv->backend, PK_ERROR_ENUM_INTERNAL_ERROR,
				       "Spawn of helper '%s' failed", argv[0]);
		pk_backend_finished (backend_spawn->priv->backend);
	}
	g_free (filename);
	g_strfreev (argv);
	g_strfreev (envp);
	return ret;
}

/**
 * pk_backend_spawn_get_name:
 **/
const gchar *
pk_backend_spawn_get_name (PkBackendSpawn *backend_spawn)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), NULL);
	return backend_spawn->priv->name;
}

/**
 * pk_backend_spawn_set_name:
 **/
gboolean
pk_backend_spawn_set_name (PkBackendSpawn *backend_spawn, const gchar *name)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	g_free (backend_spawn->priv->name);
	backend_spawn->priv->name = g_strdup (name);
	return TRUE;
}

/**
 * pk_backend_spawn_kill:
 **/
gboolean
pk_backend_spawn_kill (PkBackendSpawn *backend_spawn)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);
	pk_spawn_kill (backend_spawn->priv->spawn);
	return TRUE;
}

/**
 * pk_backend_spawn_helper:
 **/
gboolean
pk_backend_spawn_helper (PkBackendSpawn *backend_spawn, const gchar *first_element, ...)
{
	gboolean ret;
	va_list args;

	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);
	g_return_val_if_fail (first_element != NULL, FALSE);
	g_return_val_if_fail (backend_spawn->priv->name != NULL, FALSE);

	/* don't auto-kill this */
	if (backend_spawn->priv->kill_id > 0) {
		g_source_remove (backend_spawn->priv->kill_id);
		backend_spawn->priv->kill_id = 0;
	}

	/* get the argument list */
	va_start (args, first_element);
	ret = pk_backend_spawn_helper_va_list (backend_spawn, first_element, &args);
	va_end (args);

	return ret;
}

/**
 * pk_backend_spawn_set_allow_sigkill:
 **/
gboolean
pk_backend_spawn_set_allow_sigkill (PkBackendSpawn *backend_spawn, gboolean allow_sigkill)
{
	gboolean ret = TRUE;

	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);

	/* have we banned this in the config ile */
	if (!backend_spawn->priv->allow_sigkill && allow_sigkill) {
		egg_warning ("cannot set allow_cancel TRUE as BackendSpawnAllowSIGKILL is set to FALSE in PackageKit.conf");
		ret = FALSE;
		goto out;
	}

	/* set this property */
	g_object_set (backend_spawn->priv->spawn,
		      "allow-sigkill", allow_sigkill,
		      NULL);
out:
	return ret;
}

/**
 * pk_backend_spawn_finalize:
 **/
static void
pk_backend_spawn_finalize (GObject *object)
{
	PkBackendSpawn *backend_spawn;

	g_return_if_fail (PK_IS_BACKEND_SPAWN (object));

	backend_spawn = PK_BACKEND_SPAWN (object);

	if (backend_spawn->priv->kill_id > 0)
		g_source_remove (backend_spawn->priv->kill_id);

	g_free (backend_spawn->priv->name);
	g_object_unref (backend_spawn->priv->conf);
	g_object_unref (backend_spawn->priv->spawn);
	g_object_unref (backend_spawn->priv->backend);

	G_OBJECT_CLASS (pk_backend_spawn_parent_class)->finalize (object);
}

/**
 * pk_backend_spawn_class_init:
 **/
static void
pk_backend_spawn_class_init (PkBackendSpawnClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_backend_spawn_finalize;
	g_type_class_add_private (klass, sizeof (PkBackendSpawnPrivate));
}

/**
 * pk_backend_spawn_init:
 **/
static void
pk_backend_spawn_init (PkBackendSpawn *backend_spawn)
{
	backend_spawn->priv = PK_BACKEND_SPAWN_GET_PRIVATE (backend_spawn);
	backend_spawn->priv->kill_id = 0;
	backend_spawn->priv->name = NULL;
	backend_spawn->priv->stdout_func = NULL;
	backend_spawn->priv->stderr_func = NULL;
	backend_spawn->priv->finished = FALSE;
	backend_spawn->priv->conf = pk_conf_new ();
	backend_spawn->priv->backend = pk_backend_new ();
	backend_spawn->priv->spawn = pk_spawn_new ();
	g_signal_connect (backend_spawn->priv->spawn, "exit",
			  G_CALLBACK (pk_backend_spawn_exit_cb), backend_spawn);
	g_signal_connect (backend_spawn->priv->spawn, "stdout",
			  G_CALLBACK (pk_backend_spawn_stdout_cb), backend_spawn);
	g_signal_connect (backend_spawn->priv->spawn, "stderr",
			  G_CALLBACK (pk_backend_spawn_stderr_cb), backend_spawn);

	/* set if SIGKILL is allowed */
	backend_spawn->priv->allow_sigkill = pk_conf_get_bool (backend_spawn->priv->conf, "BackendSpawnAllowSIGKILL");
	g_object_set (backend_spawn->priv->spawn,
		      "allow-sigkill", backend_spawn->priv->allow_sigkill,
		      NULL);
}

/**
 * pk_backend_spawn_new:
 **/
PkBackendSpawn *
pk_backend_spawn_new (void)
{
	PkBackendSpawn *backend_spawn;
	backend_spawn = g_object_new (PK_TYPE_BACKEND_SPAWN, NULL);
	return PK_BACKEND_SPAWN (backend_spawn);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static GMainLoop *loop;
static guint number_packages = 0;

/**
 * pk_backend_spawn_test_finished_cb:
 **/
static void
pk_backend_spawn_test_finished_cb (PkBackend *backend, PkExitEnum exit, PkBackendSpawn *backend_spawn)
{
	g_main_loop_quit (loop);
}

/**
 * pk_backend_spawn_test_package_cb:
 **/
static void
pk_backend_spawn_test_package_cb (PkBackend *backend, PkInfoEnum info,
				  const gchar *package_id, const gchar *summary,
				  PkBackendSpawn *backend_spawn)
{
	number_packages++;
}

static gchar **
pk_backend_spawn_va_list_to_argv_test (const gchar *first_element, ...)
{
	va_list args;
	gchar **array;

	/* get the argument list */
	va_start (args, first_element);
	array = pk_backend_spawn_va_list_to_argv (first_element, &args);
	va_end (args);

	return array;
}

void
pk_backend_test_spawn (EggTest *test)
{
	PkBackendSpawn *backend_spawn;
	PkBackend *backend;
	const gchar *text;
	guint refcount;
	gboolean ret;
	gchar *uri;
	gchar **array;

	loop = g_main_loop_new (NULL, FALSE);

	if (!egg_test_start (test, "PkBackendSpawn"))
		return;

	/************************************************************
	 ****************      splitting va_list       **************
	 ************************************************************/
	egg_test_title (test, "va_list_to_argv single");
	array = pk_backend_spawn_va_list_to_argv_test ("richard", NULL);
	if (g_strcmp0 (array[0], "richard") == 0 &&
	    array[1] == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect array '%s'", array[0]);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "va_list_to_argv triple");
	array = pk_backend_spawn_va_list_to_argv_test ("richard", "phillip", "hughes", NULL);
	if (g_strcmp0 (array[0], "richard") == 0 &&
	    g_strcmp0 (array[1], "phillip") == 0 &&
	    g_strcmp0 (array[2], "hughes") == 0 &&
	    array[3] == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect array '%s','%s','%s'", array[0], array[1], array[2]);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "get an backend_spawn");
	backend_spawn = pk_backend_spawn_new ();
	egg_test_assert (test, backend_spawn != NULL);

	/* private copy for unref testing */
	backend = backend_spawn->priv->backend;
	/* incr ref count so we don't kill the object */
	g_object_ref (backend);

	/************************************************************/
	egg_test_title (test, "get backend name");
	text = pk_backend_spawn_get_name (backend_spawn);
	if (text == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid name %s", text);

	/************************************************************/
	egg_test_title (test, "set backend name");
	ret = pk_backend_spawn_set_name (backend_spawn, "test_spawn");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid set name");

	/************************************************************/
	egg_test_title (test, "get backend name");
	text = pk_backend_spawn_get_name (backend_spawn);
	if (g_strcmp0 (text, "test_spawn") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid name %s", text);

	/* needed to avoid an error */
	ret = pk_backend_set_name (backend_spawn->priv->backend, "test_spawn");
	ret = pk_backend_lock (backend_spawn->priv->backend);

	/************************************************************
	 **********       Check parsing common error      ***********
	 ************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout Percentage1");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "percentage\t0");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout Percentage2");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "percentage\tbrian");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout Percentage3");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "percentage\t12345");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout Percentage4");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "percentage\t");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout Percentage5");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "percentage");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout Subpercentage");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "subpercentage\t17");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout NoPercentageUpdates");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "no-percentage-updates");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout failure");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "error\tnot-present-woohoo\tdescription text");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "did not detect incorrect enum");

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout Status");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "status\tquery");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout RequireRestart");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "requirerestart\tsystem\tgnome-power-manager;0.0.1;i386;data");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout RequireRestart invalid enum");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "requirerestart\tmooville\tgnome-power-manager;0.0.1;i386;data");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "did not detect incorrect enum");

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout RequireRestart invalid PackageId");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "requirerestart\tsystem\tdetails about the restart");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "did not detect incorrect package id");

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout AllowUpdate1");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "allow-cancel\ttrue");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_stdout AllowUpdate2");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "allow-cancel\tbrian");
	egg_test_assert (test, !ret);

	/************************************************************
	 **********         Check uri conversion          ***********
	 ************************************************************/
	egg_test_title (test, "convert proxy uri (bare)");
	uri = pk_backend_spawn_convert_uri ("username:password@server:port");
	egg_test_assert (test, (g_strcmp0 (uri, "http://username:password@server:port/") == 0));
	g_free (uri);

	/************************************************************/
	egg_test_title (test, "convert proxy uri (full)");
	uri = pk_backend_spawn_convert_uri ("http://username:password@server:port/");
	egg_test_assert (test, (g_strcmp0 (uri, "http://username:password@server:port/") == 0));
	g_free (uri);

	/************************************************************/
	egg_test_title (test, "convert proxy uri (partial)");
	uri = pk_backend_spawn_convert_uri ("ftp://username:password@server:port");
	egg_test_assert (test, (g_strcmp0 (uri, "ftp://username:password@server:port/") == 0));
	g_free (uri);

	/************************************************************
	 **********        Check parsing common out       ***********
	 ************************************************************/
	egg_test_title (test, "test pk_backend_spawn_parse_common_out Package");
	ret = pk_backend_spawn_parse_stdout (backend_spawn,
		"package\tinstalled\tgnome-power-manager;0.0.1;i386;data\tMore useless software");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "manually unlock as we have no engine");
	ret = pk_backend_unlock (backend_spawn->priv->backend);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "did not unlock");

	/* reset */
	g_object_unref (backend_spawn);

	/************************************************************/
	egg_test_title (test, "test we unref'd all but one of the PkBackend instances");
	refcount = G_OBJECT(backend)->ref_count;
	if (refcount == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "refcount invalid %i", refcount);

	/* new */
	backend_spawn = pk_backend_spawn_new ();

	/************************************************************/
	egg_test_title (test, "set backend name");
	ret = pk_backend_spawn_set_name (backend_spawn, "test_spawn");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid set name");

	/* so we can spin until we finish */
	g_signal_connect (backend_spawn->priv->backend, "finished",
			  G_CALLBACK (pk_backend_spawn_test_finished_cb), backend_spawn);
	/* so we can count the returned packages */
	g_signal_connect (backend_spawn->priv->backend, "package",
			  G_CALLBACK (pk_backend_spawn_test_package_cb), backend_spawn);

	/* needed to avoid an error */
	ret = pk_backend_lock (backend_spawn->priv->backend);

	/************************************************************
	 **********          Use a spawned helper         ***********
	 ************************************************************/
	egg_test_title (test, "test search-name.sh running");
	ret = pk_backend_spawn_helper (backend_spawn, "search-name.sh", "none", "bar", NULL);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "cannot spawn search-name.sh");

	/* wait for finished */
	g_main_loop_run (loop);

	/************************************************************/
	egg_test_title (test, "test number of packages");
	if (number_packages == 2)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong number of packages %i", number_packages);

	/************************************************************/
	egg_test_title (test, "manually unlock as we have no engine");
	ret = pk_backend_unlock (backend_spawn->priv->backend);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "did not unlock");

	/* done */
	g_object_unref (backend_spawn);

	/************************************************************/
	egg_test_title (test, "test we unref'd all but one of the PkBackend instances");
	refcount = G_OBJECT(backend)->ref_count;
	if (refcount == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "refcount invalid %i", refcount);

	/* we ref'd it manually for checking, so we need to unref it */
	g_object_unref (backend);
	g_main_loop_unref (loop);

	egg_test_end (test);
}
#endif

