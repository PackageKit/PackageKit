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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <pk-enum.h>
#include <pk-common.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-spawn.h"
#include "pk-marshal.h"

static void     pk_spawn_class_init	(PkSpawnClass *klass);
static void     pk_spawn_init		(PkSpawn      *spawn);
static void     pk_spawn_finalize	(GObject       *object);

#define PK_SPAWN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_SPAWN, PkSpawnPrivate))
#define PK_SPAWN_POLL_DELAY	50 /* ms */
#define PK_SPAWN_SIGKILL_DELAY	500 /* ms */

struct PkSpawnPrivate
{
	gint			 child_pid;
	gint			 stdout_fd;
	guint			 poll_id;
	guint			 kill_id;
	gboolean		 finished;
	PkExitEnum		 exit;
	GString			*stdout_buf;
};

enum {
	PK_SPAWN_FINISHED,
	PK_SPAWN_STDOUT,
	PK_SPAWN_LAST_SIGNAL
};

static guint	     signals [PK_SPAWN_LAST_SIGNAL] = { 0 };

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
	if (egg_strzero (string->str)) {
		return FALSE;
	}

	/* split into lines - the last line may be incomplete */
	lines = g_strsplit (string->str, "\n", 0);
	if (lines == NULL) {
		return FALSE;
	}

	/* find size */
	size = g_strv_length (lines);

	bytes_processed = 0;
	/* we only emit n-1 strings */
	for (i=0; i<(size-1); i++) {
		g_signal_emit (spawn, signals [PK_SPAWN_STDOUT], 0, lines[i]);
		/* ITS4: ignore, g_strsplit always NULL terminates */
		bytes_processed += strlen (lines[i]) + 1;
	}

	/* remove the text we've processed */
	g_string_erase (string, 0, bytes_processed);

	g_strfreev (lines);
	return TRUE;
}

/**
 * pk_spawn_check_child:
 **/
static gboolean
pk_spawn_check_child (PkSpawn *spawn)
{
	int status;

	/* this shouldn't happen */
	if (spawn->priv->finished) {
		egg_error ("finished twice!");
	}

	pk_spawn_read_fd_into_buffer (spawn->priv->stdout_fd, spawn->priv->stdout_buf);
	pk_spawn_emit_whole_lines (spawn, spawn->priv->stdout_buf);

	/* check if the child exited */
	if (waitpid (spawn->priv->child_pid, &status, WNOHANG) != spawn->priv->child_pid)
		return TRUE;

	/* disconnect the poll as there will be no more updates */
	g_source_remove (spawn->priv->poll_id);

	/* child exited, display some information... */
	close (spawn->priv->stdout_fd);

	if (WEXITSTATUS (status) > 0) {
		egg_warning ("Running fork failed with return value %d", WEXITSTATUS (status));
		if (spawn->priv->exit == PK_EXIT_ENUM_UNKNOWN) {
			spawn->priv->exit = PK_EXIT_ENUM_FAILED;
		}
	} else {
		if (spawn->priv->exit == PK_EXIT_ENUM_UNKNOWN) {
			spawn->priv->exit = PK_EXIT_ENUM_SUCCESS;
		}
	}

	/* officially done, although no signal yet */
	spawn->priv->finished = TRUE;

	/* if we are trying to kill this process, cancel the SIGKILL */
	if (spawn->priv->kill_id != 0) {
		g_source_remove (spawn->priv->kill_id);
		spawn->priv->kill_id = 0;
	}

	egg_debug ("emitting finished %i", spawn->priv->exit);
	g_signal_emit (spawn, signals [PK_SPAWN_FINISHED], 0, spawn->priv->exit);

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
		egg_warning ("already finished, ignoring");
		return FALSE;
	}

	/* we won't overwrite this if not unknown */
	spawn->priv->exit = PK_EXIT_ENUM_KILLED;

	egg_debug ("sending SIGKILL %i", spawn->priv->child_pid);
	retval = kill (spawn->priv->child_pid, SIGKILL);
	if (retval == EINVAL) {
		egg_warning ("The signum argument is an invalid or unsupported number");
		return FALSE;
	} else if (retval == EPERM) {
		egg_warning ("You do not have the privilege to send a signal to the process");
		return FALSE;
	}

	return FALSE;
}

/**
 * pk_spawn_kill:
 *
 * THIS IS A VERY DANGEROUS THING TO DO!
 *
 * We send SIGQUIT and after a few ms SIGKILL
 *
 **/
gboolean
pk_spawn_kill (PkSpawn *spawn)
{
	gint retval;

	g_return_val_if_fail (PK_IS_SPAWN (spawn), FALSE);

	/* check if process has already gone */
	if (spawn->priv->finished) {
		egg_warning ("already finished, ignoring");
		return FALSE;
	}

	/* we won't overwrite this if not unknown */
	spawn->priv->exit = PK_EXIT_ENUM_CANCELLED;

	egg_debug ("sending SIGQUIT %i", spawn->priv->child_pid);
	retval = kill (spawn->priv->child_pid, SIGQUIT);
	if (retval == EINVAL) {
		egg_warning ("The signum argument is an invalid or unsupported number");
		return FALSE;
	} else if (retval == EPERM) {
		egg_warning ("You do not have the privilege to send a signal to the process");
		return FALSE;
	}

	/* the program might not be able to handle SIGQUIT, give it a few seconds and then SIGKILL it */
	spawn->priv->kill_id = g_timeout_add (PK_SPAWN_SIGKILL_DELAY, (GSourceFunc) pk_spawn_sigkill_cb, spawn);

	return TRUE;
}

/**
 * pk_spawn_argv:
 * @argv: Can be generated using g_strsplit (command, " ", 0)
 * if there are no spaces in the filename
 *
 **/
gboolean
pk_spawn_argv (PkSpawn *spawn, gchar **argv, gchar **envp)
{
	gboolean ret;
	guint i;
	guint len;

	g_return_val_if_fail (PK_IS_SPAWN (spawn), FALSE);
	g_return_val_if_fail (argv != NULL, FALSE);

	len = g_strv_length (argv);
	if (len > 5) {
		egg_debug ("limiting debugging to 5 entries");
		len = 5;
	}
	for (i=0; i<len; i++) {
		egg_debug ("argv[%i] '%s'", i, argv[i]);
	}
	spawn->priv->finished = FALSE;

	/* create spawned object for tracking */
	ret = g_spawn_async_with_pipes (NULL, argv, envp,
				 G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
				 NULL, NULL, &spawn->priv->child_pid,
				 NULL, /* stdin */
				 &spawn->priv->stdout_fd,
				 NULL,
				 NULL);

	/* we failed to invoke the helper */
	if (ret == FALSE) {
		egg_warning ("failed to spawn '%s'", argv[0]);
		return FALSE;
	}

	/* install an idle handler to check if the child returnd successfully. */
	fcntl (spawn->priv->stdout_fd, F_SETFL, O_NONBLOCK);

	/* poll quickly */
	spawn->priv->poll_id = g_timeout_add (PK_SPAWN_POLL_DELAY, (GSourceFunc) pk_spawn_check_child, spawn);

	return TRUE;
}

/**
 * pk_spawn_class_init:
 * @klass: The PkSpawnClass
 **/
static void
pk_spawn_class_init (PkSpawnClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_spawn_finalize;

	signals [PK_SPAWN_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
	signals [PK_SPAWN_STDOUT] =
		g_signal_new ("stdout",
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
	spawn->priv->poll_id = 0;
	spawn->priv->kill_id = 0;
	spawn->priv->finished = FALSE;
	spawn->priv->exit = PK_EXIT_ENUM_UNKNOWN;

	spawn->priv->stdout_buf = g_string_new ("");
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
	}

	/* disconnect the SIGKILL check */
	if (spawn->priv->kill_id != 0) {
		g_source_remove (spawn->priv->kill_id);
	}

	/* free the buffers */
	g_string_free (spawn->priv->stdout_buf, TRUE);

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

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"
#define BAD_EXIT 999

PkExitEnum mexit = BAD_EXIT;
guint stdout_count = 0;
guint finished_count = 0;

/**
 * pk_test_finished_cb:
 **/
static void
pk_test_finished_cb (PkSpawn *spawn, PkExitEnum exit, EggTest *test)
{
	egg_debug ("spawn exit=%i", exit);
	mexit = exit;
	finished_count++;
	egg_test_loop_quit (test);
}

/**
 * pk_test_stdout_cb:
 **/
static void
pk_test_stdout_cb (PkSpawn *spawn, const gchar *line, EggTest *test)
{
	egg_debug ("stdout '%s'", line);
	stdout_count++;
}

static gboolean
cancel_cb (gpointer data)
{
	PkSpawn *spawn = PK_SPAWN(data);
	pk_spawn_kill (spawn);
	return FALSE;
}

static void
new_spawn_object (EggTest *test, PkSpawn **pspawn)
{
	if (*pspawn != NULL) {
		g_object_unref (*pspawn);
	}
	*pspawn = pk_spawn_new ();
	g_signal_connect (*pspawn, "finished",
			  G_CALLBACK (pk_test_finished_cb), test);
	g_signal_connect (*pspawn, "stdout",
			  G_CALLBACK (pk_test_stdout_cb), test);
}

void
pk_spawn_test (EggTest *test)
{
	PkSpawn *spawn = NULL;
	gboolean ret;
	gchar *path;
	gchar **argv;
	gchar **envp;

	if (!egg_test_start (test, "PkSpawn"))
		return;

	/* get new object */
	new_spawn_object (test, &spawn);

	/************************************************************/
	egg_test_title (test, "make sure return error for missing file");
	mexit = BAD_EXIT;
	argv = g_strsplit ("pk-spawn-test-xxx.sh", " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL);
	g_strfreev (argv);
	if (ret == FALSE) {
		egg_test_success (test, "failed to run invalid file");
	} else {
		egg_test_failed (test, "ran incorrect file");
	}

	/************************************************************/
	egg_test_title (test, "make sure finished wasn't called");
	if (mexit == BAD_EXIT)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "Called finish for bad file!");
	}

	/************************************************************/
	egg_test_title (test, "make sure run correct helper");
	mexit = -1;
	path = egg_test_get_data_file ("pk-spawn-test.sh");
	argv = g_strsplit (path, " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL);
	g_free (path);
	g_strfreev (argv);
	if (ret) {
		egg_test_success (test, "ran correct file");
	} else {
		egg_test_failed (test, "did not run helper");
	}

	/* wait for finished */
	egg_test_loop_wait (test, 10000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "make sure finished okay");
	if (mexit == PK_EXIT_ENUM_SUCCESS)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "finish was okay!");
	}

	/************************************************************/
	egg_test_title (test, "make sure finished was called only once");
	if (finished_count == 1)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "finish was called %i times!", finished_count);
	}

	/************************************************************/
	egg_test_title (test, "make sure we got the right stdout data");
	if (stdout_count == 4+11) {
		egg_test_success (test, "correct stdout count");
	} else {
		egg_test_failed (test, "wrong stdout count %i", stdout_count);
	}

	/* get new object */
	new_spawn_object (test, &spawn);

	/************************************************************/
	egg_test_title (test, "make sure we set the proxy");
	mexit = -1;
	path = egg_test_get_data_file ("pk-spawn-proxy.sh");
	argv = g_strsplit (path, " ", 0);
	envp = g_strsplit ("http_proxy=username:password@server:port "
			   "ftp_proxy=username:password@server:port", " ", 0);
	ret = pk_spawn_argv (spawn, argv, envp);
	g_free (path);
	g_strfreev (argv);
	if (ret) {
		egg_test_success (test, "ran correct file");
	} else {
		egg_test_failed (test, "did not run helper");
	}

	/* wait for finished */
	egg_test_loop_wait (test, 10000);
	egg_test_loop_check (test);

	/* get new object */
	new_spawn_object (test, &spawn);

	/************************************************************/
	egg_test_title (test, "make sure run correct helper, and kill it");
	mexit = BAD_EXIT;
	path = egg_test_get_data_file ("pk-spawn-test.sh");
	argv = g_strsplit (path, " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL);
	g_free (path);
	g_strfreev (argv);
	if (ret)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "did not run helper");
	}

	g_timeout_add_seconds (1, cancel_cb, spawn);
	/* wait for finished */
	egg_test_loop_wait (test, 5000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "make sure finished in SIGKILL");
	if (mexit == PK_EXIT_ENUM_KILLED)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "finish %i!", mexit);
	}

	/* get new object */
	new_spawn_object (test, &spawn);

	/************************************************************/
	egg_test_title (test, "make sure run correct helper, and quit it");
	mexit = BAD_EXIT;
	path = egg_test_get_data_file ("pk-spawn-test-sigquit.sh");
	argv = g_strsplit (path, " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL);
	g_free (path);
	g_strfreev (argv);
	if (ret)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "did not run helper");
	}

	g_timeout_add_seconds (1, cancel_cb, spawn);
	/* wait for finished */
	egg_test_loop_wait (test, 2000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "make sure finished in SIGQUIT");
	if (mexit == PK_EXIT_ENUM_CANCELLED)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "finish %i!", mexit);
	}

	/************************************************************/
	egg_test_title (test, "run lots of data for profiling");
	path = egg_test_get_data_file ("pk-spawn-test-profiling.sh");
	argv = g_strsplit (path, " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL);
	g_free (path);
	g_strfreev (argv);
	if (ret)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "did not run profiling helper");
	}

	g_object_unref (spawn);

	egg_test_end (test);
}
#endif

