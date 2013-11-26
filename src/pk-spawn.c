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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>

#include "pk-spawn.h"
#include "pk-marshal.h"
#include "pk-shared.h"
#include "pk-conf.h"

#include "pk-sysdep.h"

static void     pk_spawn_finalize	(GObject       *object);

#define PK_SPAWN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_SPAWN, PkSpawnPrivate))
#define PK_SPAWN_POLL_DELAY	50 /* ms */
#define PK_SPAWN_SIGKILL_DELAY	2500 /* ms */

struct PkSpawnPrivate
{
	pid_t			 child_pid;
	gint			 stdin_fd;
	gint			 stdout_fd;
	gint			 stderr_fd;
	guint			 poll_id;
	guint			 kill_id;
	gboolean		 finished;
	gboolean		 background;
	gboolean		 is_sending_exit;
	gboolean		 is_changing_dispatcher;
	gboolean		 allow_sigkill;
	PkSpawnExitType		 exit;
	GString			*stdout_buf;
	GString			*stderr_buf;
	gchar			*last_argv0;
	gchar			**last_envp;
	PkConf			*conf;
};

enum {
	SIGNAL_EXIT,
	SIGNAL_STDOUT,
	SIGNAL_STDERR,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_BACKGROUND,
	PROP_ALLOW_SIGKILL,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkSpawn, pk_spawn, G_TYPE_OBJECT)

/**
 * pk_spawn_read_fd_into_buffer:
 **/
static gboolean
pk_spawn_read_fd_into_buffer (gint fd, GString *string)
{
	gint bytes_read;
	gchar buffer[BUFSIZ];

	/* ITS4: ignore, we manually NULL terminate and GString cannot overflow */
	while ((bytes_read = read (fd, buffer, BUFSIZ-1)) > 0) {
		buffer[bytes_read] = '\0';
		g_string_append (string, buffer);
	}

	return TRUE;
}

/**
 * pk_spawn_emit_whole_lines:
 **/
static gboolean
pk_spawn_emit_whole_lines (PkSpawn *spawn, GString *string)
{
	guint i;
	guint size;
	gchar **lines;
	guint bytes_processed;

	/* if nothing then don't emit */
	if (pk_strzero (string->str))
		return FALSE;

	/* split into lines - the last line may be incomplete */
	lines = g_strsplit (string->str, "\n", 0);
	if (lines == NULL)
		return FALSE;

	/* find size */
	size = g_strv_length (lines);

	bytes_processed = 0;
	/* we only emit n-1 strings */
	for (i=0; i<(size-1); i++) {
		g_signal_emit (spawn, signals [SIGNAL_STDOUT], 0, lines[i]);
		/* ITS4: ignore, g_strsplit always NULL terminates */
		bytes_processed += strlen (lines[i]) + 1;
	}

	/* remove the text we've processed */
	g_string_erase (string, 0, bytes_processed);

	g_strfreev (lines);
	return TRUE;
}

/**
 * pk_spawn_exit_type_enum_to_string:
 **/
static const gchar *
pk_spawn_exit_type_enum_to_string (PkSpawnExitType type)
{
	if (type == PK_SPAWN_EXIT_TYPE_SUCCESS)
		return "success";
	if (type == PK_SPAWN_EXIT_TYPE_FAILED)
		return "failed";
	if (type == PK_SPAWN_EXIT_TYPE_DISPATCHER_CHANGED)
		return "dispatcher-changed";
	if (type == PK_SPAWN_EXIT_TYPE_DISPATCHER_EXIT)
		return "dispatcher-exit";
	if (type == PK_SPAWN_EXIT_TYPE_SIGQUIT)
		return "sigquit";
	if (type == PK_SPAWN_EXIT_TYPE_SIGKILL)
		return "sigkill";
	return "unknown";
}

/**
 * pk_spawn_check_child:
 **/
static gboolean
pk_spawn_check_child (PkSpawn *spawn)
{
	pid_t pid;
	int status;
	gint retval;
	static guint limit_printing = 0;

	/* this shouldn't happen */
	if (spawn->priv->finished) {
		g_warning ("finished twice!");
		spawn->priv->poll_id = 0;
		return FALSE;
	}

	pk_spawn_read_fd_into_buffer (spawn->priv->stdout_fd, spawn->priv->stdout_buf);
	pk_spawn_read_fd_into_buffer (spawn->priv->stderr_fd, spawn->priv->stderr_buf);

	/* emit all lines on standard out in one callback, as it's all probably
	* related to the error that just happened */
	if (spawn->priv->stderr_buf->len != 0) {
		g_signal_emit (spawn, signals [SIGNAL_STDERR], 0, spawn->priv->stderr_buf->str);
		g_string_set_size (spawn->priv->stderr_buf, 0);
	}

	/* all usual output goes on standard out, only bad libraries bitch to stderr */
	pk_spawn_emit_whole_lines (spawn, spawn->priv->stdout_buf);

	/* Only print one in twenty times to avoid filling the screen */
	if (limit_printing++ % 20 == 0)
		g_debug ("polling child_pid=%ld (1/20)", (long)spawn->priv->child_pid);

	/* check if the child exited */
	pid = waitpid (spawn->priv->child_pid, &status, WNOHANG);
	if (pid == -1) {
		g_warning ("failed to get the child PID data for %ld", (long)spawn->priv->child_pid);
		return TRUE;
	}
	if (pid == 0) {
		/* process still exist, but has not changed state */
		return TRUE;
	}
	if (pid != spawn->priv->child_pid) {
		g_warning ("some other process id was returned: got %ld and wanted %ld",
			     (long)pid, (long)spawn->priv->child_pid);
		return TRUE;
	}

	/* disconnect the poll as there will be no more updates */
	if (spawn->priv->poll_id > 0) {
		g_source_remove (spawn->priv->poll_id);
		spawn->priv->poll_id = 0;
	}

	/* child exited, close resources */
	close (spawn->priv->stdin_fd);
	close (spawn->priv->stdout_fd);
	close (spawn->priv->stderr_fd);
	spawn->priv->stdin_fd = -1;
	spawn->priv->stdout_fd = -1;
	spawn->priv->stderr_fd = -1;
	spawn->priv->child_pid = -1;

	/* use this to detect SIGKILL and SIGQUIT */
	if (WIFSIGNALED (status)) {
		retval = WTERMSIG (status);
		if (retval == SIGQUIT) {
			g_debug ("the child process was terminated by SIGQUIT");
			spawn->priv->exit = PK_SPAWN_EXIT_TYPE_SIGQUIT;
		} else if (retval == SIGKILL) {
			g_debug ("the child process was terminated by SIGKILL");
			spawn->priv->exit = PK_SPAWN_EXIT_TYPE_SIGKILL;
		} else {
			g_warning ("the child process was terminated by signal %i", WTERMSIG (status));
			spawn->priv->exit = PK_SPAWN_EXIT_TYPE_SIGKILL;
		}
	} else {
		/* check we are dead and buried */
		if (!WIFEXITED (status)) {
			g_warning ("the process did not exit, but waitpid() returned!");
			return TRUE;
		}

		/* get the exit code */
		retval = WEXITSTATUS (status);
		if (retval == 0) {
			g_debug ("the child exited with success");
			if (spawn->priv->exit == PK_SPAWN_EXIT_TYPE_UNKNOWN)
				spawn->priv->exit = PK_SPAWN_EXIT_TYPE_SUCCESS;
		} else if (retval == 254) {
			g_debug ("backend was exited rather than finished");
			spawn->priv->exit = PK_SPAWN_EXIT_TYPE_FAILED;
		} else {
			g_warning ("the child exited with return code %i", retval);
			if (spawn->priv->exit == PK_SPAWN_EXIT_TYPE_UNKNOWN)
				spawn->priv->exit = PK_SPAWN_EXIT_TYPE_FAILED;
		}
	}

	/* officially done, although no signal yet */
	spawn->priv->finished = TRUE;

	/* if we are trying to kill this process, cancel the SIGKILL */
	if (spawn->priv->kill_id != 0) {
		g_source_remove (spawn->priv->kill_id);
		spawn->priv->kill_id = 0;
	}

	/* are we doing pk_spawn_exit for a good reason? */
	if (spawn->priv->is_changing_dispatcher)
		spawn->priv->exit = PK_SPAWN_EXIT_TYPE_DISPATCHER_CHANGED;
	else if (spawn->priv->is_sending_exit)
		spawn->priv->exit = PK_SPAWN_EXIT_TYPE_DISPATCHER_EXIT;

	/* don't emit if we just closed an invalid dispatcher */
	g_debug ("emitting exit %s", pk_spawn_exit_type_enum_to_string (spawn->priv->exit));
	g_signal_emit (spawn, signals [SIGNAL_EXIT], 0, spawn->priv->exit);

	spawn->priv->poll_id = 0;
	return FALSE;
}

/**
 * pk_spawn_sigkill_cb:
 **/
static gboolean
pk_spawn_sigkill_cb (PkSpawn *spawn)
{
	gint retval;

	/* check if process has already gone */
	if (spawn->priv->finished) {
		g_debug ("already finished, ignoring");
		goto out;
	}

	/* set this in case the script catches the signal and exits properly */
	spawn->priv->exit = PK_SPAWN_EXIT_TYPE_SIGKILL;

	g_debug ("sending SIGKILL %ld", (long)spawn->priv->child_pid);
	retval = kill (spawn->priv->child_pid, SIGKILL);
	if (retval == EINVAL) {
		g_warning ("The signum argument is an invalid or unsupported number");
		goto out;
	} else if (retval == EPERM) {
		g_warning ("You do not have the privilege to send a signal to the process");
		goto out;
	}
out:
	/* never repeat */
	spawn->priv->kill_id = 0;
	return FALSE;
}

/**
 * pk_spawn_is_running:
 *
 * Is this instance controlling a script?
 *
 **/
gboolean
pk_spawn_is_running (PkSpawn *spawn)
{
	return (spawn->priv->child_pid != -1);
}

/**
 * pk_spawn_kill:
 *
 * We send SIGQUIT and after a few ms SIGKILL (if allowed)
 *
 * IMPORTANT: This is not a syncronous operation, and client programs will need
 * to wait for the ::exit signal.
 **/
gboolean
pk_spawn_kill (PkSpawn *spawn)
{
	gint retval;

	g_return_val_if_fail (PK_IS_SPAWN (spawn), FALSE);
	g_return_val_if_fail (spawn->priv->kill_id == 0, FALSE);

	/* is there a process running? */
	if (spawn->priv->child_pid == -1) {
		g_warning ("no child pid to kill!");
		return FALSE;
	}

	/* check if process has already gone */
	if (spawn->priv->finished) {
		g_debug ("already finished, ignoring");
		return FALSE;
	}

	/* set this in case the script catches the signal and exits properly */
	spawn->priv->exit = PK_SPAWN_EXIT_TYPE_SIGQUIT;

	g_debug ("sending SIGQUIT %ld", (long)spawn->priv->child_pid);
	retval = kill (spawn->priv->child_pid, SIGQUIT);
	if (retval == EINVAL) {
		g_warning ("The signum argument is an invalid or unsupported number");
		return FALSE;
	} else if (retval == EPERM) {
		g_warning ("You do not have the privilege to send a signal to the process");
		return FALSE;
	}

	/* the program might not be able to handle SIGQUIT, give it a few seconds and then SIGKILL it */
	if (spawn->priv->allow_sigkill) {
		spawn->priv->kill_id = g_timeout_add (PK_SPAWN_SIGKILL_DELAY, (GSourceFunc) pk_spawn_sigkill_cb, spawn);
		g_source_set_name_by_id (spawn->priv->kill_id, "[PkSpawn] sigkill");
	}
	return TRUE;
}

/**
 * pk_spawn_send_stdin:
 *
 * Send new comands to a running (but idle) dispatcher script
 *
 **/
static gboolean
pk_spawn_send_stdin (PkSpawn *spawn, const gchar *command)
{
	gint wrote;
	gint length;
	gchar *buffer = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (PK_IS_SPAWN (spawn), FALSE);

	/* check if process has already gone */
	if (spawn->priv->finished) {
		g_debug ("already finished, ignoring");
		ret = FALSE;
		goto out;
	}

	/* is there a process running? */
	if (spawn->priv->child_pid == -1) {
		g_debug ("no child pid");
		ret = FALSE;
		goto out;
	}

	/* buffer always has to have trailing newline */
	g_debug ("sending '%s'", command);
	buffer = g_strdup_printf ("%s\n", command);

	/* ITS4: ignore, we generated this */
	length = strlen (buffer);

	/* write to the waiting process */
	wrote = write (spawn->priv->stdin_fd, buffer, length);
	if (wrote != length) {
		g_warning ("wrote %i/%i bytes on fd %i (%s)", wrote, length, spawn->priv->stdin_fd, strerror (errno));
		ret = FALSE;
	}
out:
	g_free (buffer);
	return ret;
}

/**
 * pk_spawn_exit:
 *
 * Just write "exit" into the open fd and wait for backend to close
 *
 **/
gboolean
pk_spawn_exit (PkSpawn *spawn)
{
	gboolean ret;
	guint count = 0;

	g_return_val_if_fail (PK_IS_SPAWN (spawn), FALSE);

	/* check if already sending exit */
	if (spawn->priv->is_sending_exit) {
		g_warning ("already sending exit, ignoring");
		return FALSE;
	}

	/* send command */
	spawn->priv->is_sending_exit = TRUE;
	ret = pk_spawn_send_stdin (spawn, "exit");
	if (!ret) {
		g_debug ("failed to send exit");
		goto out;
	}

	/* block until the previous script exited */
	do {
		g_debug ("waiting for exit");
		/* Usleep rather than g_main_loop_run -- we have to block.
		 * If we run the loop, other idle events can be processed,
		 * and this includes sending data to a new instance,
		 * which of course will fail as the 'old' script is exiting */
		g_usleep (10*1000); /* 10 ms */
		ret = pk_spawn_check_child (spawn);
	} while (ret && count++ < 500);

	/* the script exited okay */
	if (count < 500)
		ret = TRUE;
	else
		g_warning ("failed to exit script");
out:
	spawn->priv->is_sending_exit = FALSE;
	return ret;
}

/**
 * pk_strvequal:
 **/
static gboolean
pk_strvequal (gchar **id1, gchar **id2)
{
	guint i;
	guint length1;
	guint length2;

	if (id1 == NULL && id2 == NULL)
		return TRUE;

	if (id1 == NULL || id2 == NULL) {
		g_debug ("GStrv compare invalid '%p' and '%p'", id1, id2);
		return FALSE;
	}

	/* check different sizes */
	length1 = g_strv_length (id1);
	length2 = g_strv_length (id2);
	if (length1 != length2)
		return FALSE;

	/* text equal each one */
	for (i=0; i<length1; i++) {
		if (g_strcmp0 (id1[i], id2[i]) != 0)
			return FALSE;
	}

	return TRUE;
}

/**
 * pk_spawn_argv:
 * @argv: Can be generated using g_strsplit (command, " ", 0)
 * if there are no spaces in the filename
 *
 **/
gboolean
pk_spawn_argv (PkSpawn *spawn, gchar **argv, gchar **envp,
	       PkSpawnArgvFlags flags, GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	guint i;
	guint len;
	gint nice_value;
	gchar *command;
	const gchar *key;
	gint rc;

	g_return_val_if_fail (PK_IS_SPAWN (spawn), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (argv != NULL, FALSE);

	len = g_strv_length (argv);
	for (i=0; i<len; i++)
		g_debug ("argv[%i] '%s'", i, argv[i]);
	if (envp != NULL) {
		len = g_strv_length (envp);
		for (i=0; i<len; i++)
			g_debug ("envp[%i] '%s'", i, envp[i]);
	}

	/* check we are not using a closing instance */
	if (spawn->priv->is_sending_exit) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0, "trying to use instance that is in the process of exiting");
		goto out;
	}

	/* we can reuse the dispatcher if:
	 *  - it's still running
	 *  - argv[0] (executable name is the same)
	 *  - all of envp are the same (proxy and locale settings) */
	if (spawn->priv->stdin_fd != -1) {
		if (g_strcmp0 (spawn->priv->last_argv0, argv[0]) != 0) {
			g_debug ("argv did not match, not reusing");
		} else if (!pk_strvequal (spawn->priv->last_envp, envp)) {
			g_debug ("envp did not match, not reusing");
		} else if ((flags & PK_SPAWN_ARGV_FLAGS_NEVER_REUSE) > 0) {
			g_debug ("not re-using instance due to policy");
		} else {
			/* join with tabs, as spaces could be in file name */
			command = g_strjoinv ("\t", &argv[1]);

			/* reuse instance */
			g_debug ("reusing instance");
			ret = pk_spawn_send_stdin (spawn, command);
			g_free (command);
			if (ret)
				goto out;

			/* so fall on through to kill and respawn */
			g_warning ("failed to write, so trying to kill and respawn");
		}

		/* kill off existing instance */
		g_debug ("changing dispatcher (exit old instance)");
		spawn->priv->is_changing_dispatcher = TRUE;
		ret = pk_spawn_exit (spawn);
		if (!ret) {
			g_warning ("failed to exit previous instance");
			/* remove poll, as we can't reply on pk_spawn_check_child() */
			if (spawn->priv->poll_id != 0) {
				g_source_remove (spawn->priv->poll_id);
				spawn->priv->poll_id = 0;
			}
		}
		spawn->priv->is_changing_dispatcher = FALSE;
	}

	/* create spawned object for tracking */
	spawn->priv->finished = FALSE;
	g_debug ("creating new instance of %s", argv[0]);
	ret = g_spawn_async_with_pipes (NULL, argv, envp,
				 G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
				 NULL, NULL, &spawn->priv->child_pid,
				 &spawn->priv->stdin_fd,
				 &spawn->priv->stdout_fd,
				 &spawn->priv->stderr_fd,
				 &error_local);
	/* we failed to invoke the helper */
	if (!ret) {
		g_set_error (error, 1, 0, "failed to spawn %s: %s", argv[0], error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the nice value and ensure we are in the valid range */
	key = "BackendSpawnNiceValue";
	if (spawn->priv->background)
		key = "BackendSpawnNiceValueBackground";
	nice_value = pk_conf_get_int (spawn->priv->conf, key);
	nice_value = CLAMP(nice_value, -20, 19);

#if HAVE_SETPRIORITY
	/* don't completely bog the system down */
	if (nice_value != 0) {
		g_debug ("renice to %i", nice_value);
		setpriority (PRIO_PROCESS, spawn->priv->child_pid, nice_value);
	}
#endif

	/* set idle IO priority */
	if (spawn->priv->background) {
		g_debug ("setting ioprio class to idle");
		pk_ioprio_set_idle (spawn->priv->child_pid);
	}

	/* save this so we can check the dispatcher name */
	g_free (spawn->priv->last_argv0);
	spawn->priv->last_argv0 = g_strdup (argv[0]);

	/* save this in case the proxy or locale changes */
	g_strfreev (spawn->priv->last_envp);
	spawn->priv->last_envp = g_strdupv (envp);

	/* install an idle handler to check if the child returnd successfully. */
	rc = fcntl (spawn->priv->stdout_fd, F_SETFL, O_NONBLOCK);
	if (rc < 0) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0, "stdout fcntl failed");
		goto out;
	}
	rc = fcntl (spawn->priv->stderr_fd, F_SETFL, O_NONBLOCK);
	if (rc < 0) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0, "stderr fcntl failed");
		goto out;
	}

	/* sanity check */
	if (spawn->priv->poll_id != 0) {
		g_warning ("trying to set timeout when already set");
		g_source_remove (spawn->priv->poll_id);
	}

	/* poll quickly */
	spawn->priv->poll_id = g_timeout_add (PK_SPAWN_POLL_DELAY, (GSourceFunc) pk_spawn_check_child, spawn);
	g_source_set_name_by_id (spawn->priv->poll_id, "[PkSpawn] main poll");
out:
	return ret;
}

/**
 * pk_spawn_get_property:
 **/
static void
pk_spawn_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkSpawn *spawn = PK_SPAWN (object);
	PkSpawnPrivate *priv = spawn->priv;

	switch (prop_id) {
	case PROP_BACKGROUND:
		g_value_set_boolean (value, priv->background);
		break;
	case PROP_ALLOW_SIGKILL:
		g_value_set_boolean (value, priv->allow_sigkill);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_spawn_set_property:
 **/
static void
pk_spawn_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkSpawn *spawn = PK_SPAWN (object);
	PkSpawnPrivate *priv = spawn->priv;

	switch (prop_id) {
	case PROP_BACKGROUND:
		priv->background = g_value_get_boolean (value);
		break;
	case PROP_ALLOW_SIGKILL:
		priv->allow_sigkill = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_spawn_class_init:
 * @klass: The PkSpawnClass
 **/
static void
pk_spawn_class_init (PkSpawnClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_spawn_finalize;
	object_class->get_property = pk_spawn_get_property;
	object_class->set_property = pk_spawn_set_property;

	/**
	 * PkSpawn:background:
	 */
	pspec = g_param_spec_boolean ("background", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_BACKGROUND, pspec);

	/**
	 * PkSpawn:allow-sigkill:
	 * Set whether the spawned backends are allowed to be SIGKILLed if they do not
	 * respond to SIGQUIT. This ensures that Cancel() works as expected, but
	 * sometimes can corrupt databases if they are open.
	 */
	pspec = g_param_spec_boolean ("allow-sigkill", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ALLOW_SIGKILL, pspec);

	signals [SIGNAL_EXIT] =
		g_signal_new ("exit",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
	signals [SIGNAL_STDOUT] =
		g_signal_new ("stdout",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
	signals [SIGNAL_STDERR] =
		g_signal_new ("stderr",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (PkSpawnPrivate));
}

/**
 * pk_spawn_init:
 * @spawn: This class instance
 **/
static void
pk_spawn_init (PkSpawn *spawn)
{
	spawn->priv = PK_SPAWN_GET_PRIVATE (spawn);

	spawn->priv->child_pid = -1;
	spawn->priv->stdout_fd = -1;
	spawn->priv->stderr_fd = -1;
	spawn->priv->stdin_fd = -1;
	spawn->priv->poll_id = 0;
	spawn->priv->kill_id = 0;
	spawn->priv->finished = FALSE;
	spawn->priv->is_sending_exit = FALSE;
	spawn->priv->is_changing_dispatcher = FALSE;
	spawn->priv->allow_sigkill = TRUE;
	spawn->priv->last_argv0 = NULL;
	spawn->priv->last_envp = NULL;
	spawn->priv->background = FALSE;
	spawn->priv->exit = PK_SPAWN_EXIT_TYPE_UNKNOWN;

	spawn->priv->stdout_buf = g_string_new ("");
	spawn->priv->stderr_buf = g_string_new ("");
	spawn->priv->conf = pk_conf_new ();
}

/**
 * pk_spawn_finalize:
 * @object: The object to finalize
 **/
static void
pk_spawn_finalize (GObject *object)
{
	PkSpawn *spawn;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_SPAWN (object));

	spawn = PK_SPAWN (object);

	g_return_if_fail (spawn->priv != NULL);

	/* disconnect the poll in case we were cancelled before completion */
	if (spawn->priv->poll_id != 0) {
		g_source_remove (spawn->priv->poll_id);
		spawn->priv->poll_id = 0;
	}

	/* disconnect the SIGKILL check */
	if (spawn->priv->kill_id != 0) {
		g_source_remove (spawn->priv->kill_id);
		spawn->priv->kill_id = 0;
	}

	/* still running? */
	if (spawn->priv->stdin_fd != -1) {
		g_debug ("killing as still running in finalize");
		pk_spawn_kill (spawn);
		/* just hope the script responded to SIGQUIT */
		if (spawn->priv->kill_id != 0)
			g_source_remove (spawn->priv->kill_id);
	}

	/* free the buffers */
	g_string_free (spawn->priv->stdout_buf, TRUE);
	g_string_free (spawn->priv->stderr_buf, TRUE);
	g_free (spawn->priv->last_argv0);
	g_strfreev (spawn->priv->last_envp);
	g_object_unref (spawn->priv->conf);

	G_OBJECT_CLASS (pk_spawn_parent_class)->finalize (object);
}

/**
 * pk_spawn_new:
 *
 * Return value: a new PkSpawn object.
 **/
PkSpawn *
pk_spawn_new (void)
{
	PkSpawn *spawn;
	spawn = g_object_new (PK_TYPE_SPAWN, NULL);
	return PK_SPAWN (spawn);
}

