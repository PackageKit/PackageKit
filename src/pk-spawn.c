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

#include "config.h"

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

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "pk-debug.h"
#include "pk-spawn.h"
#include "pk-marshal.h"

static void     pk_spawn_class_init	(PkSpawnClass *klass);
static void     pk_spawn_init		(PkSpawn      *spawn);
static void     pk_spawn_finalize	(GObject       *object);

#define PK_SPAWN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_SPAWN, PkSpawnPrivate))

struct PkSpawnPrivate
{
	gint			 child_pid;
	gint			 standard_error;
	gint			 standard_out;
	GString			*stderr_buf;
	GString			*stdout_buf;
};

enum {
	PK_SPAWN_FINISHED,
	PK_SPAWN_STDERR,
	PK_SPAWN_STDOUT,
	PK_SPAWN_LAST_SIGNAL
};

static guint	     signals [PK_SPAWN_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkSpawn, pk_spawn, G_TYPE_OBJECT)

/**
 * pk_spawn_split_lines:
 **/
static void
pk_spawn_split_lines (PkSpawn *spawn, const gchar *data)
{
	guint i;
	char **lines;
	char *line;

	if (data == NULL) {
		pk_warning ("data NULL");
		return;
	}

	/* split output into complete lines */
	lines = g_strsplit (data, "\n", 0);

	for (i=0; lines[i]; i++) {
		line = lines[i];
		pk_debug ("emitting stdout %s", line);
		g_signal_emit (spawn, signals [PK_SPAWN_STDOUT], 0, line);
	}
	g_strfreev (lines);
}

/**
 * pk_spawn_check_child:
 **/
static gboolean
pk_spawn_check_child (gpointer data)
{
	PkSpawn *spawn;
	int status;
	int bytes_read;
	gchar buffer[1024];
	gchar *message_err;
	gchar *message_out;
	spawn  = (PkSpawn *) data;

//bytes_read = read (spawn->priv->standard_error, buffer, 1023);
//buffer[bytes_read] = '\0';
//g_warning ("%s", buffer);

	/* read input from stderr */
	while ((bytes_read = read (spawn->priv->standard_error, buffer, 1023)) > 0) {
		buffer[bytes_read] = '\0';
		g_string_append (spawn->priv->stderr_buf, buffer);
	}

	/* read input from stdout */
	while ((bytes_read = read (spawn->priv->standard_out, buffer, 1023)) > 0) {
		buffer[bytes_read] = '\0';
		g_string_append (spawn->priv->stdout_buf, buffer);
	}

	/* check if the child exited */
	if (waitpid (spawn->priv->child_pid, &status, WNOHANG) != spawn->priv->child_pid)
		return TRUE;

	/* child exited, display some information... */
	close (spawn->priv->standard_error);
	close (spawn->priv->standard_out);

	message_err = g_locale_to_utf8 (spawn->priv->stderr_buf->str, -1, NULL, (gsize *) &bytes_read, NULL);
	message_out = g_locale_to_utf8 (spawn->priv->stdout_buf->str, -1, NULL, (gsize *) &bytes_read, NULL);
	if (WEXITSTATUS (status) > 0) {
		pk_warning ("Running fork failed with return value %d:\nout:'%s'\nerr:'%s'", WEXITSTATUS (status), message_out, message_err);
	} else {
		pk_debug ("Running fork successful:\nout:'%s'\nerr:'%s'", message_out, message_err);
	}

	/* only do at end, TODO: run stderr async for percentage output */
	pk_spawn_split_lines (spawn, message_out);

	g_free (message_err);
	g_free (message_out);

	pk_debug ("emitting finished %i", WEXITSTATUS (status));
	g_signal_emit (spawn, signals [PK_SPAWN_FINISHED], 0, WEXITSTATUS (status));

	return FALSE;
}

/**
 * pk_spawn_command:
 **/
gboolean
pk_spawn_command (PkSpawn *spawn, const gchar *command)
{
	gboolean ret;
	gchar **argv;

	g_return_val_if_fail (spawn != NULL, FALSE);
	g_return_val_if_fail (PK_IS_SPAWN (spawn), FALSE);

	if (command == NULL) {
		pk_warning ("command NULL");
		return FALSE;
	}

	/* split command line */
	argv = g_strsplit (command, " ", 0);

	/* create spawned object for tracking */
	ret = g_spawn_async_with_pipes (NULL, argv, NULL,
				 G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
				 NULL, NULL, &spawn->priv->child_pid,
				 NULL, /* stdin */
				 &spawn->priv->standard_out,
				 &spawn->priv->standard_error,
				 NULL);
	g_strfreev (argv);

	/* we failed to invoke the helper */
	if (ret == FALSE) {
		g_warning ("failed to spawn '%s'", command);
		return FALSE;
	}

	/* install an idle handler to check if the child returnd successfully. */
	fcntl (spawn->priv->standard_out, F_SETFL, O_NONBLOCK);
	fcntl (spawn->priv->standard_error, F_SETFL, O_NONBLOCK);
//	g_idle_add (pk_spawn_check_child, spawn);

	g_timeout_add (250, pk_spawn_check_child, spawn);

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
	signals [PK_SPAWN_STDERR] =
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
	spawn->priv->standard_error = -1;
	spawn->priv->standard_out = -1;

	spawn->priv->stderr_buf = g_string_new ("");
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

	g_string_free (spawn->priv->stderr_buf, TRUE);
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
#ifdef PK_BUILD_TESTS
#include "pk-self-test.h"

void
pk_st_spawn (PkSelfTest *test)
{
	PkSpawn *spawn;
	gboolean ret;

	if (pk_st_start (test, "PkSpawn", CLASS_AUTO) == FALSE) {
		return;
	}

	spawn = pk_spawn_new ();


	/************************************************************/
	pk_st_title (test, "make sure return error for missing file");
	ret = pk_spawn_command (spawn, "../helpers/xxx-yum-refresh-cache.py");
	if (ret == FALSE) {
		pk_st_success (test, "failed to run invalid file");
	} else {
		pk_st_failed (test, "ran incorrect file");
	}

	/************************************************************/
	pk_st_title (test, "make sure run correct helper");
	ret = pk_spawn_command (spawn, "../helpers/yum-refresh-cache.py");
	if (ret == TRUE) {
		pk_st_success (test, "ran correct file");
	} else {
		pk_st_failed (test, "did not run helper");
	}

	g_object_unref (spawn);

	pk_st_end (test);
}
#endif

