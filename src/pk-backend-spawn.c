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
#include <glib/gprintf.h>

#include <gmodule.h>
#include <libgbus.h>

#include <pk-common.h>
#include <pk-package-id.h>
#include <pk-enum.h>
#include <pk-network.h>

#include "pk-debug.h"
#include "pk-backend-internal.h"
#include "pk-backend-spawn.h"
#include "pk-marshal.h"
#include "pk-enum.h"
#include "pk-spawn.h"
#include "pk-time.h"
#include "pk-inhibit.h"

#define PK_BACKEND_SPAWN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND_SPAWN, PkBackendSpawnPrivate))
#define PK_BACKEND_SPAWN_PERCENTAGE_INVALID	101

struct PkBackendSpawnPrivate
{
	PkSpawn			*spawn;
	PkBackend		*backend;
	gchar			*name;
	gulong			 signal_finished;
	gulong			 signal_stdout;
};

G_DEFINE_TYPE (PkBackendSpawn, pk_backend_spawn, G_TYPE_OBJECT)

/**
 * pk_backend_spawn_parse_stdout:
 *
 * If you are editing this function creating a new backend,
 * then you are probably doing something wrong.
 **/
G_GNUC_WARN_UNUSED_RESULT static gboolean
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
	PkErrorCodeEnum error_enum;
	PkStatusEnum status_enum;
	PkMessageEnum message_enum;
	PkRestartEnum restart_enum;
	PkSigTypeEnum sig_type;

	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);

	/* check if output line */
	if (line == NULL)
		return FALSE;

	/* split by tab */
	sections = g_strsplit (line, "\t", 0);
	command = sections[0];

	/* get size */
	size = g_strv_length (sections);

	if (pk_strequal (command, "package")) {
		if (size != 4) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		if (pk_package_id_check (sections[2]) == FALSE) {
			pk_warning ("invalid package_id");
			ret = FALSE;
			goto out;
		}
		info = pk_info_enum_from_text (sections[1]);
		if (info == PK_INFO_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_DAEMON,
					    "Info enum not recognised, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
		pk_backend_package (backend_spawn->priv->backend, info, sections[2], sections[3]);
	} else if (pk_strequal (command, "details")) {
		if (size != 7) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		group = pk_group_enum_from_text (sections[3]);

		/* ITS4: ignore, checked for overflow */
		package_size = atol (sections[6]);
		if (package_size > 1073741824) {
			pk_warning ("package size cannot be larger than one Gb");
			ret = FALSE;
			goto out;
		}
		text = g_strdup (sections[4]);
		/* convert ; to \n as we can't emit them on stdout */
		g_strdelimit (text, ";", '\n');
		pk_backend_details (backend_spawn->priv->backend, sections[1], sections[2],
					group, text, sections[5], package_size);
		g_free (text);
	} else if (pk_strequal (command, "files")) {
		if (size != 3) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		pk_backend_files (backend_spawn->priv->backend, sections[1], sections[2]);
	} else if (pk_strequal (command, "repo-detail")) {
		if (size != 4) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		if (pk_strequal (sections[3], "true")) {
			pk_backend_repo_detail (backend_spawn->priv->backend, sections[1], sections[2], TRUE);
		} else if (pk_strequal (sections[3], "false")) {
			pk_backend_repo_detail (backend_spawn->priv->backend, sections[1], sections[2], FALSE);
		} else {
			pk_warning ("invalid qualifier '%s'", sections[3]);
			ret = FALSE;
			goto out;
		}
	} else if (pk_strequal (command, "updatedetail")) {
		if (size != 9) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		restart = pk_restart_enum_from_text (sections[7]);
		if (restart == PK_RESTART_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_DAEMON,
					    "Restart enum not recognised, and hence ignored: '%s'", sections[7]);
			ret = FALSE;
			goto out;
		}

		text = g_strdup (sections[8]);
		/* convert ; to \n as we can't emit them on stdout */
		g_strdelimit (text, ";", '\n');
		pk_backend_update_detail (backend_spawn->priv->backend, sections[1], sections[2],
					  sections[3], sections[4], sections[5],
					  sections[6], restart, text);
		g_free (text);
	} else if (pk_strequal (command, "percentage")) {
		if (size != 2) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		ret = pk_strtoint (sections[1], &percentage);
		if (!ret) {
			pk_warning ("invalid percentage value %s", sections[1]);
		} else if (percentage < 0 || percentage > 100) {
			pk_warning ("invalid percentage value %i", percentage);
			ret = FALSE;
		} else {
			pk_backend_set_percentage (backend_spawn->priv->backend, percentage);
		}
	} else if (pk_strequal (command, "subpercentage")) {
		if (size != 2) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		ret = pk_strtoint (sections[1], &percentage);
		if (!ret) {
			pk_warning ("invalid subpercentage value %s", sections[1]);
		} else if (percentage < 0 || percentage > 100) {
			pk_warning ("invalid subpercentage value %i", percentage);
			ret = FALSE;
		} else {
			pk_backend_set_sub_percentage (backend_spawn->priv->backend, percentage);
		}
	} else if (pk_strequal (command, "error")) {
		if (size != 3) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		error_enum = pk_error_enum_from_text (sections[1]);
		if (error_enum == PK_ERROR_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_DAEMON,
					    "Error enum not recognised, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
		/* convert back all the ;'s to newlines */
		text = g_strdup (sections[2]);
		/* convert ; to \n as we can't emit them on stdout */
		g_strdelimit (text, ";", '\n');
		pk_backend_error_code (backend_spawn->priv->backend, error_enum, text);
		g_free (text);
	} else if (pk_strequal (command, "requirerestart")) {
		if (size != 3) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		restart_enum = pk_restart_enum_from_text (sections[1]);
		if (restart_enum == PK_RESTART_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_DAEMON,
					    "Restart enum not recognised, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
		pk_backend_require_restart (backend_spawn->priv->backend, restart_enum, sections[2]);
	} else if (pk_strequal (command, "message")) {
		if (size != 3) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		message_enum = pk_message_enum_from_text (sections[1]);
		if (message_enum == PK_MESSAGE_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_DAEMON,
					    "Message enum not recognised, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
		text = g_strdup (sections[2]);
		/* convert ; to \n as we can't emit them on stdout */
		g_strdelimit (text, ";", '\n');
		pk_backend_message (backend_spawn->priv->backend, message_enum, text);
		g_free (text);
	} else if (pk_strequal (command, "change-transaction-data")) {
		if (size != 2) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		pk_backend_set_transaction_data (backend_spawn->priv->backend, sections[1]);
	} else if (pk_strequal (command, "status")) {
		if (size != 2) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		status_enum = pk_status_enum_from_text (sections[1]);
		if (status_enum == PK_STATUS_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_DAEMON,
					    "Status enum not recognised, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
		pk_backend_set_status (backend_spawn->priv->backend, status_enum);
	} else if (pk_strequal (command, "allow-cancel")) {
		if (size != 2) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		if (pk_strequal (sections[1], "true")) {
			pk_backend_set_allow_cancel (backend_spawn->priv->backend, TRUE);
		} else if (pk_strequal (sections[1], "false")) {
			pk_backend_set_allow_cancel (backend_spawn->priv->backend, FALSE);
		} else {
			pk_warning ("invalid section '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
	} else if (pk_strequal (command, "no-percentage-updates")) {
		if (size != 1) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		pk_backend_set_percentage (backend_spawn->priv->backend, PK_BACKEND_PERCENTAGE_INVALID);
	} else if (pk_strequal (command, "repo-signature-required")) {

		if (size != 9) {
			pk_warning ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}

		sig_type = pk_sig_type_enum_from_text (sections[8]);
		if (sig_type == PK_SIGTYPE_ENUM_UNKNOWN) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_DAEMON,
					    "Sig enum not recognised, and hence ignored: '%s'", sections[8]);
			ret = FALSE;
			goto out;
		}
		if (pk_strzero (sections[1])) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_DAEMON,
					    "package_id blank, and hence ignored: '%s'", sections[1]);
			ret = FALSE;
			goto out;
		}
		if (pk_strzero (sections[2])) {
			pk_backend_message (backend_spawn->priv->backend, PK_MESSAGE_ENUM_DAEMON,
					    "repository name blank, and hence ignored: '%s'", sections[2]);
			ret = FALSE;
			goto out;
		}

		/* pass _all_ of the data */
		ret = pk_backend_repo_signature_required (backend_spawn->priv->backend, sections[1],
							  sections[2], sections[3], sections[4],
							  sections[5], sections[6], sections[7], sig_type);
		goto out;
	} else {
		pk_warning ("invalid command '%s'", command);
	}
out:
	g_strfreev (sections);
	return ret;
}

/**
 * pk_backend_spawn_helper_delete:
 **/
static gboolean
pk_backend_spawn_helper_delete (PkBackendSpawn *backend_spawn)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);
	if (backend_spawn->priv->spawn == NULL) {
		pk_warning ("spawn object not in use");
		return FALSE;
	}
	pk_debug ("deleting spawn %p", backend_spawn->priv->spawn);
	g_signal_handler_disconnect (backend_spawn->priv->spawn, backend_spawn->priv->signal_finished);
	g_signal_handler_disconnect (backend_spawn->priv->spawn, backend_spawn->priv->signal_stdout);
	g_object_unref (backend_spawn->priv->spawn);
	backend_spawn->priv->spawn = NULL;
	return TRUE;
}

/**
 * pk_backend_spawn_finished_cb:
 **/
static void
pk_backend_spawn_finished_cb (PkSpawn *spawn, PkExitEnum exit, PkBackendSpawn *backend_spawn)
{
	g_return_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn));

	pk_debug ("deleting spawn %p, exit %s", backend_spawn, pk_exit_enum_to_text (exit));
	pk_backend_spawn_helper_delete (backend_spawn);

	/* if we killed the process, set an error */
	if (exit == PK_EXIT_ENUM_KILLED) {
		/* we just call this failed, and set an error */
		pk_backend_error_code (backend_spawn->priv->backend, PK_ERROR_ENUM_PROCESS_KILL,
				       "Process had to be killed to be cancelled");
	}

	if (FALSE && /*TODO: backend_spawn->priv->set_error == FALSE*/
	    exit == PK_EXIT_ENUM_FAILED) {
		pk_backend_error_code (backend_spawn->priv->backend, PK_ERROR_ENUM_INTERNAL_ERROR,
				       "Helper returned non-zero return value but did not set error");
	}
	pk_backend_finished (backend_spawn->priv->backend);
}

/**
 * pk_backend_spawn_stdout_cb:
 **/
static void
pk_backend_spawn_stdout_cb (PkBackendSpawn *spawn, const gchar *line, PkBackendSpawn *backend_spawn)
{
	gboolean ret;
	g_return_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn));

	pk_debug ("stdout from %p = '%s'", spawn, line);
	ret = pk_backend_spawn_parse_stdout (backend_spawn, line);
	if (!ret) {
		pk_debug ("failed to parse '%s'", line);
	}
}

/**
 * pk_backend_spawn_helper_new:
 **/
static gboolean
pk_backend_spawn_helper_new (PkBackendSpawn *backend_spawn)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);

	if (backend_spawn->priv->spawn != NULL) {
		pk_warning ("spawn object already in use");
		return FALSE;
	}
	backend_spawn->priv->spawn = pk_spawn_new ();
	pk_debug ("allocating spawn %p", backend_spawn->priv->spawn);
	backend_spawn->priv->signal_finished =
		g_signal_connect (backend_spawn->priv->spawn, "finished",
				  G_CALLBACK (pk_backend_spawn_finished_cb), backend_spawn);
	backend_spawn->priv->signal_stdout =
		g_signal_connect (backend_spawn->priv->spawn, "stdout",
				  G_CALLBACK (pk_backend_spawn_stdout_cb), backend_spawn);
	return TRUE;
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
	gchar *value;
	gchar *line;
	GPtrArray *array;

	array = g_ptr_array_new ();

	/* http_proxy */
	value = pk_backend_get_proxy_http (backend_spawn->priv->backend);
	if (!pk_strzero (value)) {
		line = g_strdup_printf ("%s=%s", "http_proxy", value);
		pk_debug ("setting evp '%s'", line);
		g_ptr_array_add (array, line);
	}
	g_free (value);

	/* ftp_proxy */
	value = pk_backend_get_proxy_ftp (backend_spawn->priv->backend);
	if (!pk_strzero (value)) {
		line = g_strdup_printf ("%s=%s", "ftp_proxy", value);
		pk_debug ("setting evp '%s'", line);
		g_ptr_array_add (array, line);
	}
	g_free (value);

	envp = pk_ptr_array_to_argv (array);
	g_ptr_array_free (array, TRUE);
	return envp;
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

	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (backend_spawn), FALSE);

	/* convert to a argv */
	argv = pk_va_list_to_argv (executable, args);
	if (argv == NULL) {
		pk_warning ("argv NULL");
		return FALSE;
	}

#if PK_BUILD_LOCAL
	/* prefer the local version */
	filename = g_build_filename ("..", "backends", backend_spawn->priv->name, "helpers", argv[0], NULL);
	if (g_file_test (filename, G_FILE_TEST_EXISTS) == FALSE) {
		pk_debug ("local helper not found '%s'", filename);
		g_free (filename);
		filename = g_build_filename (DATADIR, "PackageKit", "helpers", backend_spawn->priv->name, argv[0], NULL);
	}
#else
	filename = g_build_filename (DATADIR, "PackageKit", "helpers", backend_spawn->priv->name, argv[0], NULL);
#endif
	pk_debug ("using spawn filename %s", filename);

	/* replace the filename with the full path */
	g_free (argv[0]);
	argv[0] = g_strdup (filename);

	pk_backend_spawn_helper_new (backend_spawn);
	envp = pk_backend_spawn_get_envp (backend_spawn);
	ret = pk_spawn_argv (backend_spawn->priv->spawn, argv, envp);
	if (!ret) {
		pk_backend_spawn_helper_delete (backend_spawn);
		pk_backend_error_code (backend_spawn->priv->backend, PK_ERROR_ENUM_INTERNAL_ERROR,
				       "Spawn of helper '%s' failed", argv[0]);
		pk_backend_finished (backend_spawn->priv->backend);
	}
	g_free (filename);
	g_strfreev (argv);
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

	if (backend_spawn->priv->spawn == NULL) {
		pk_warning ("cannot kill missing process");
		return FALSE;
	}
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

	/* get the argument list */
	va_start (args, first_element);
	ret = pk_backend_spawn_helper_va_list (backend_spawn, first_element, &args);
	va_end (args);

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
	if (backend_spawn->priv->spawn != NULL) {
		pk_backend_spawn_helper_delete (backend_spawn);
	}
	g_free (backend_spawn->priv->name);
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
	backend_spawn->priv->spawn = NULL;
	backend_spawn->priv->name = NULL;
	backend_spawn->priv->backend = pk_backend_new ();
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
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

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

void
libst_backend_spawn (LibSelfTest *test)
{
	PkBackendSpawn *backend_spawn;
	PkBackend *backend;
	const gchar *text;
	guint refcount;
	gboolean ret;

	loop = g_main_loop_new (NULL, FALSE);

	if (libst_start (test, "PkBackendSpawn", CLASS_AUTO) == FALSE) {
		return;
	}

	/* don't do these when doing make distcheck */
#ifndef PK_IS_DEVELOPER
	libst_end (test);
	return;
#endif

	/************************************************************/
	libst_title (test, "get an backend_spawn");
	backend_spawn = pk_backend_spawn_new ();
	if (backend_spawn != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/* private copy for unref testing */
	backend = backend_spawn->priv->backend;
	/* incr ref count so we don't kill the object */
	g_object_ref (backend);

	/************************************************************/
	libst_title (test, "get backend name");
	text = pk_backend_spawn_get_name (backend_spawn);
	if (text == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid name %s", text);
	}

	/************************************************************/
	libst_title (test, "set backend name");
	ret = pk_backend_spawn_set_name (backend_spawn, "test_spawn");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid set name");
	}

	/************************************************************/
	libst_title (test, "get backend name");
	text = pk_backend_spawn_get_name (backend_spawn);
	if (pk_strequal(text, "test_spawn")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid name %s", text);
	}

	/* needed to avoid an error */
	ret = pk_backend_set_name (backend_spawn->priv->backend, "test_spawn");
	ret = pk_backend_lock (backend_spawn->priv->backend);

	/************************************************************
	 **********       Check parsing common error      ***********
	 ************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout Percentage1");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "percentage\t0");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not validate correctly");
	}

	/************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout Percentage2");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "percentage\tbrian");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not validate correctly");
	}

	/************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout Percentage3");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "percentage\t12345");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not validate correctly");
	}

	/************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout Percentage4");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "percentage\t");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not validate correctly");
	}

	/************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout Percentage5");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "percentage");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not validate correctly");
	}

	/************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout Subpercentage");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "subpercentage\t17");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not validate correctly");
	}

	/************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout NoPercentageUpdates");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "no-percentage-updates");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not validate correctly");
	}

	/************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout failure");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "error\tnot-present-woohoo\tdescription text");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not detect incorrect enum");
	}

	/************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout Status");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "status\tquery");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not validate correctly");
	}

	/************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout RequireRestart");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "requirerestart\tsystem\tdetails about the restart");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not validate correctly");
	}

	/************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout RequireRestart");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "requirerestart\tmooville\tdetails about the restart");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not detect incorrect enum");
	}

	/************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout AllowUpdate1");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "allow-cancel\ttrue");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not validate correctly");
	}

	/************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_stdout AllowUpdate2");
	ret = pk_backend_spawn_parse_stdout (backend_spawn, "allow-cancel\tbrian");
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not validate correctly");
	}

	/************************************************************
	 **********        Check parsing common out       ***********
	 ************************************************************/
	libst_title (test, "test pk_backend_spawn_parse_common_out Package");
	ret = pk_backend_spawn_parse_stdout (backend_spawn,
		"package\tinstalled\tgnome-power-manager;0.0.1;i386;data\tMore useless software");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not validate correctly");
	}

	/************************************************************/
	libst_title (test, "manually unlock as we have no engine");
	ret = pk_backend_unlock (backend_spawn->priv->backend);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not unlock");
	}

	/* reset */
	g_object_unref (backend_spawn);

	/************************************************************/
	libst_title (test, "test we unref'd all but one of the PkBackend instances");
	refcount = G_OBJECT(backend)->ref_count;
	if (refcount == 1) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "refcount invalid %i", refcount);
	}

	/* new */
	backend_spawn = pk_backend_spawn_new ();

	/************************************************************/
	libst_title (test, "set backend name");
	ret = pk_backend_spawn_set_name (backend_spawn, "test_spawn");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "invalid set name");
	}

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
	libst_title (test, "test search-name.sh running");
	ret = pk_backend_spawn_helper (backend_spawn, "search-name.sh", "none", "bar", NULL);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "cannot spawn search-name.sh");
	}

	/* wait for finished */
	g_main_loop_run (loop);

	/************************************************************/
	libst_title (test, "test number of packages");
	if (number_packages == 2) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "wrong number of packages %i", number_packages);
	}

	/************************************************************/
	libst_title (test, "manually unlock as we have no engine");
	ret = pk_backend_unlock (backend_spawn->priv->backend);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not unlock");
	}

	/* done */
	g_object_unref (backend_spawn);

	/************************************************************/
	libst_title (test, "test we unref'd all but one of the PkBackend instances");
	refcount = G_OBJECT(backend)->ref_count;
	if (refcount == 1) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "refcount invalid %i", refcount);
	}

	/* we ref'd it manually for checking, so we need to unref it */
	g_object_unref (backend);
	g_main_loop_unref (loop);

	libst_end (test);
}
#endif

