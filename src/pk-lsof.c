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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <glib.h>

#include "egg-debug.h"

#include "pk-lsof.h"

#define PK_LSOF_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_LSOF, PkLsofPrivate))

struct PkLsofPrivate
{
	GPtrArray		*list_data;
};

G_DEFINE_TYPE (PkLsof, pk_lsof, G_TYPE_OBJECT)

typedef struct {
	guint			 pid;
	gchar			*filename;
} PkLsofData;

/**
 * pk_lsof_add_pid:
 **/
static gboolean
pk_lsof_add_pid (GPtrArray *array, guint pid)
{
	guint i;
	guint pid_tmp;
	gboolean found = FALSE;

	/* search already list */
	for (i=0; i<array->len; i++) {
		pid_tmp = GPOINTER_TO_INT (g_ptr_array_index (array, i));
		if (pid_tmp == pid) {
			found = TRUE;
			break;
		}
	}

	/* not found, so add */
	if (!found) {
		g_ptr_array_add (array, GINT_TO_POINTER (pid));
	}
	return !found;
}

/**
 * pk_lsof_data_free:
 **/
static void
pk_lsof_data_free (PkLsofData *lsof)
{
	g_free (lsof->filename);
	g_free (lsof);
}

/**
 * pk_lsof_data_new:
 **/
static PkLsofData *
pk_lsof_data_new (guint pid, const gchar *filename)
{
	PkLsofData *data;
	data = g_new0 (PkLsofData, 1);
	data->pid = pid;
	data->filename = g_strdup (filename);
	return data;
}

/**
 * pk_lsof_refresh:
 **/
gboolean
pk_lsof_refresh (PkLsof *lsof)
{
	gboolean ret;
	GError *error = NULL;
	gchar *stdout = NULL;
	gchar *stderr = NULL;
	PkLsofData *data;
	gchar **lines = NULL;
	guint i;
	const gchar *pid_text;
	const gchar *type;
	const gchar *filename;

	g_return_val_if_fail (PK_IS_LSOF (lsof), FALSE);

	/* run lsof to get all data */
	ret = g_spawn_command_line_sync ("/usr/sbin/lsof", &stdout, &stderr, NULL, &error);
	if (!ret) {
		egg_warning ("failed to get pids: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* clear */
	g_ptr_array_foreach (lsof->priv->list_data, (GFunc) pk_lsof_data_free, NULL);
	g_ptr_array_set_size (lsof->priv->list_data, 0);

	/* split into lines */
	lines = g_strsplit (stdout, "\n", -1);
	for (i=0; lines[i] != NULL; i++) {

		/* parse: devhelp   11328   hughsie  mem       REG        8,2    65840  161702 /usr/lib/gio/modules/libgioremote-volume-monitor.so */
		lines[i][17] = '\0';
		lines[i][36] = '\0';
		pid_text = &lines[i][10];
		type = &lines[i][27];
		filename = &lines[i][69+3];

		/* only add memory mapped entries */
		if (!g_str_has_prefix (type, "mem"))
			continue;

		/* not a system library */
		if (strstr (filename, "/lib/") == NULL)
			continue;

		/* not a shared object */
		if (strstr (filename, ".so") == NULL)
			continue;

		/* add to array */
		data = pk_lsof_data_new (atoi (pid_text), filename);
		g_ptr_array_add (lsof->priv->list_data, data);
	}
out:
	g_strfreev (lines);
	g_free (stdout);
	g_free (stderr);
	return ret;
}

/**
 * pk_lsof_get_pids_for_filenames:
 **/
GPtrArray *
pk_lsof_get_pids_for_filenames (PkLsof *lsof, gchar **filenames)
{
	guint i;
	guint j;
	gboolean ret;
	GPtrArray *list_data;
	GPtrArray *pids = NULL;
	const PkLsofData *data;

	g_return_val_if_fail (PK_IS_LSOF (lsof), NULL);

	/* might not have been refreshed ever */
	list_data = lsof->priv->list_data;
	if (list_data->len == 0) {
		ret = pk_lsof_refresh (lsof);
		if (!ret) {
			egg_warning ("failed to refresh");
			goto out;
		}
	}

	/* create array of pids that are using this library */
	pids = g_ptr_array_new ();
	for (i=0; filenames[i] != NULL; i++) {
		for (j=0; j < list_data->len; j++) {
			data = g_ptr_array_index (list_data, j);
			if (g_strcmp0 (filenames[i], data->filename) == 0) {
				pk_lsof_add_pid (pids, data->pid);
			}
		}
	}
out:
	return pids;
}

/**
 * pk_lsof_finalize:
 **/
static void
pk_lsof_finalize (GObject *object)
{
	PkLsof *lsof;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_LSOF (object));
	lsof = PK_LSOF (object);

	g_ptr_array_foreach (lsof->priv->list_data, (GFunc) pk_lsof_data_free, NULL);
	g_ptr_array_free (lsof->priv->list_data, TRUE);

	G_OBJECT_CLASS (pk_lsof_parent_class)->finalize (object);
}

/**
 * pk_lsof_class_init:
 **/
static void
pk_lsof_class_init (PkLsofClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_lsof_finalize;
	g_type_class_add_private (klass, sizeof (PkLsofPrivate));
}

/**
 * pk_lsof_init:
 *
 * initializes the lsof class. NOTE: We expect lsof objects
 * to *NOT* be removed or added during the session.
 * We only control the first lsof object if there are more than one.
 **/
static void
pk_lsof_init (PkLsof *lsof)
{
	lsof->priv = PK_LSOF_GET_PRIVATE (lsof);
	lsof->priv->list_data = g_ptr_array_new ();
}

/**
 * pk_lsof_new:
 * Return value: A new lsof class instance.
 **/
PkLsof *
pk_lsof_new (void)
{
	PkLsof *lsof;
	lsof = g_object_new (PK_TYPE_LSOF, NULL);
	return PK_LSOF (lsof);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_lsof_test (EggTest *test)
{
	gboolean ret;
	PkLsof *lsof;
	GPtrArray *pids;
	gchar *files[] = { "/lib/libssl3.so", NULL };

	if (!egg_test_start (test, "PkLsof"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	lsof = pk_lsof_new ();
	egg_test_assert (test, lsof != NULL);

	/************************************************************/
	egg_test_title (test, "refresh lsof data");
	ret = pk_lsof_refresh (lsof);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get pids for files");
	pids = pk_lsof_get_pids_for_filenames (lsof, files);
	egg_test_assert (test, pids->len > 0);
	g_ptr_array_free (pids, TRUE);

	g_object_unref (lsof);

	egg_test_end (test);
}
#endif

