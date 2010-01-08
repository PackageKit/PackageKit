/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include <string.h>
#include <stdlib.h>
#include <glib.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-proc.h"

#define PK_PROC_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PROC, PkProcPrivate))

struct PkProcPrivate
{
	GPtrArray		*list_data;
};

G_DEFINE_TYPE (PkProc, pk_proc, G_TYPE_OBJECT)

typedef struct {
	gchar			*cmdline;
	guint			 pid;
	guint			 uid;
} PkProcData;

/**
 * pk_proc_data_free:
 **/
static void
pk_proc_data_free (PkProcData *proc)
{
	g_free (proc->cmdline);
	g_free (proc);
}

/**
 * pk_proc_data_new:
 **/
static PkProcData *
pk_proc_data_new (const gchar *cmdline, guint pid, guint uid)
{
	PkProcData *data;
	data = g_new0 (PkProcData, 1);
	data->pid = pid;
	data->uid = uid;
	data->cmdline = g_strdup (cmdline);
	return data;
}

/**
 * pk_proc_refresh_find_file:
 **/
static gchar *
pk_proc_refresh_find_file (const gchar *filename)
{
	gchar *path;
	gboolean ret;
	guint i;
	const gchar *paths[] = { "/usr/bin", "/usr/sbin", "/bin", "/sbin", "/usr/libexec", "/usr/lib/vte/", NULL };

	/* try each one */
	for (i=0; paths[i] != NULL; i++) {
		path = g_build_filename (paths[i], filename, NULL);
		ret = g_file_test (path, G_FILE_TEST_IS_REGULAR);
		if (ret)
			goto out;
		g_free (path);
	}

	/* we failed */
	path = NULL;
out:
	return path;
}

/**
 * pk_proc_refresh_add_file:
 **/
static gboolean
pk_proc_refresh_add_file (PkProc *proc, const gchar *pid_text, const gchar *path)
{
	gboolean ret = FALSE;
	GError *error = NULL;
	gchar *cmdline = NULL;
	gint pid = -1;
	guint uid;
	PkProcData *data;
	gchar *cmdline_full = NULL;
	gchar *offset;
	gchar *uid_file = NULL;
	gchar *contents = NULL;

	/* get cmdline */
	ret = g_file_get_contents (path, &cmdline, NULL, &error);
	if (!ret) {
		egg_warning ("failed to get cmdline: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* remove prelink junk */
	offset = g_strrstr (cmdline, ".#prelink#.");
	if (offset != NULL)
		*(offset) = '\0';

	/* remove added junk */
	g_strdelimit (cmdline, " \t:;", '\0');

	/* do we have anything left? */
	if (cmdline[0] == '\0') {
		ret = FALSE;
		goto out;
	}

	/* prepend path if it does not already exist */
	if (cmdline[0] == '/') {
		cmdline_full = g_strdup (cmdline);
	} else {
		cmdline_full = pk_proc_refresh_find_file (cmdline);
		if (cmdline_full == NULL) {
			egg_warning ("cannot find in any bin dir: %s", cmdline);
			ret = FALSE;
			goto out;
		}
	}

	/* check if path exists */
	ret = g_file_test (cmdline_full, G_FILE_TEST_IS_REGULAR);
	if (!ret) {
		egg_warning ("cmdline does not exist: %s", cmdline_full);
		goto out;
	}

	/* parse PID */
	ret = egg_strtoint (pid_text, &pid);
	if (!ret)
		goto out;

	/* get UID */
	uid_file = g_build_filename ("/proc", pid_text, "loginuid", NULL);

	/* is a process file */
	if (!g_file_test (uid_file, G_FILE_TEST_EXISTS))
		goto out;

	/* able to get contents */
	ret = g_file_get_contents (uid_file, &contents, 0, NULL);
	if (!ret)
		goto out;

	/* parse UID */
	ret = egg_strtouint (contents, &uid);
	if (!ret)
		goto out;

	/* add data to array */
	data = pk_proc_data_new (cmdline_full, pid, uid);
	g_ptr_array_add (proc->priv->list_data, data);
	egg_debug ("adding %s pid:%i uid:%i", data->cmdline, data->pid, data->uid);
out:
	g_free (cmdline_full);
	g_free (cmdline);
	g_free (contents);
	g_free (uid_file);
	return ret;
}

/**
 * pk_proc_refresh:
 **/
gboolean
pk_proc_refresh (PkProc *proc)
{
	gboolean ret = FALSE;
	GError *error = NULL;
	GDir *dir;
	const gchar *filename;
	gchar *path;

	g_return_val_if_fail (PK_IS_PROC (proc), FALSE);

	/* open directory */
	dir = g_dir_open ("/proc", 0, &error);
	if (dir == NULL) {
		egg_warning ("failed to open directory: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* clear */
	g_ptr_array_set_size (proc->priv->list_data, 0);

	/* find all files */
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		/* this is Linux specific, but #ifdef code welcome */
		path = g_build_filename ("/proc", filename, "cmdline", NULL);

		/* only process files that exist */
		ret = g_file_test (path, G_FILE_TEST_IS_REGULAR);
		if (ret)
			pk_proc_refresh_add_file (proc, filename, path);

		/* get next entry */
		filename = g_dir_read_name (dir);
		g_free (path);
	}

	/* success */
	ret = TRUE;

out:
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * pk_proc_find_exec:
 **/
gboolean
pk_proc_find_exec (PkProc *proc, const gchar *filename)
{
	guint j;
	gboolean ret = FALSE;
	GPtrArray *list_data;
	const PkProcData *data;

	g_return_val_if_fail (PK_IS_PROC (proc), FALSE);

	/* setup state */
	list_data = proc->priv->list_data;

	/* find executable that matches the pattern */
	for (j=0; j < list_data->len; j++) {
		data = g_ptr_array_index (list_data, j);
		ret = g_pattern_match_simple (filename, data->cmdline);
		if (ret)
			break;
	}

	return ret;
}

/**
 * pk_proc_find_execs:
 **/
gboolean
pk_proc_find_execs (PkProc *proc, gchar **filenames)
{
	guint i;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_PROC (proc), FALSE);

	/* find executable that matches the pattern */
	for (i=0; filenames[i] != NULL; i++) {
		ret = pk_proc_find_exec (proc, filenames[i]);
		if (ret)
			break;
	}

	return ret;
}

/**
 * pk_proc_get_process_for_cmdlines:
 **/
gchar *
pk_proc_get_process_for_cmdlines (PkProc *proc, gchar **filenames)
{
	guint i;
	guint j;
	gboolean ret;
	gchar *process = NULL;
	GPtrArray *list_data;
	const PkProcData *data;

	g_return_val_if_fail (PK_IS_PROC (proc), NULL);

	/* get local copy */
	list_data = proc->priv->list_data;

	/* find executable that matches the pattern */
	for (i=0; filenames[i] != NULL; i++) {
		for (j=0; j < list_data->len; j++) {
			data = g_ptr_array_index (list_data, j);
			ret = g_pattern_match_simple (filenames[i], data->cmdline);
			if (ret) {
				process = g_strdup (data->cmdline);
				goto out;
			}
		}
	}
out:
	return process;
}

/**
 * pk_proc_finalize:
 **/
static void
pk_proc_finalize (GObject *object)
{
	PkProc *proc;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_PROC (object));
	proc = PK_PROC (object);

	g_ptr_array_unref (proc->priv->list_data);

	G_OBJECT_CLASS (pk_proc_parent_class)->finalize (object);
}

/**
 * pk_proc_class_init:
 **/
static void
pk_proc_class_init (PkProcClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_proc_finalize;
	g_type_class_add_private (klass, sizeof (PkProcPrivate));
}

/**
 * pk_proc_init:
 *
 * initializes the proc class. NOTE: We expect proc objects
 * to *NOT* be removed or added during the session.
 * We only control the first proc object if there are more than one.
 **/
static void
pk_proc_init (PkProc *proc)
{
	proc->priv = PK_PROC_GET_PRIVATE (proc);
	proc->priv->list_data = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_proc_data_free);
}

/**
 * pk_proc_new:
 * Return value: A new proc class instance.
 **/
PkProc *
pk_proc_new (void)
{
	PkProc *proc;
	proc = g_object_new (PK_TYPE_PROC, NULL);
	return PK_PROC (proc);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_proc_test (EggTest *test)
{
	gboolean ret;
	PkProc *proc;
	gchar *pids;
	gchar *files[] = { "/*/bash", NULL };

	if (!egg_test_start (test, "PkProc"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	proc = pk_proc_new ();
	egg_test_assert (test, proc != NULL);

	/************************************************************/
	egg_test_title (test, "refresh proc data");
	ret = pk_proc_refresh (proc);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get process for cmdline");
	pids = pk_proc_get_process_for_cmdlines (proc, files);
	egg_test_assert (test, pids != NULL);
	g_free (pids);

	g_object_unref (proc);

	egg_test_end (test);
}
#endif

