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

typedef enum {
	PK_LSOF_TYPE_MEM,
	PK_LSOF_TYPE_DEL,
	PK_LSOF_TYPE_TXT,
	PK_LSOF_TYPE_UNKNOWN
} PkLsofType;

/**
 * pk_lsof_type_to_string:
 **/
static const gchar *
pk_lsof_type_to_string (PkLsofType type)
{
	if (type == PK_LSOF_TYPE_MEM)
		return "mem";
	if (type == PK_LSOF_TYPE_TXT)
		return "txt";
	if (type == PK_LSOF_TYPE_DEL)
		return "del";
	return "unknown";
}

/**
 * pk_lsof_type_from_string:
 **/
static PkLsofType
pk_lsof_type_from_string (const gchar *type)
{
	if (g_ascii_strcasecmp (type, "mem") == 0)
		return PK_LSOF_TYPE_MEM;
	if (g_ascii_strcasecmp (type, "txt") == 0)
		return PK_LSOF_TYPE_TXT;
	if (g_ascii_strcasecmp (type, "del") == 0)
		return PK_LSOF_TYPE_DEL;
	return PK_LSOF_TYPE_UNKNOWN;
}

/**
 * pk_lsof_strtoint:
 **/
static gboolean
pk_lsof_strtoint (const gchar *text, gint *value)
{
	gchar *endptr = NULL;
	gint64 value_raw;

	/* invalid */
	if (text == NULL)
		return FALSE;

	/* parse */
	value_raw = g_ascii_strtoll (text, &endptr, 10);

	/* parsing error */
	if (endptr == text)
		return FALSE;

	/* out of range */
	if (value_raw > G_MAXINT || value_raw < G_MININT)
		return FALSE;

	/* cast back down to value */
	*value = (gint) value_raw;
	return TRUE;
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
	const gchar *lsof_name;
	gchar *lsof_cmd = NULL;
	PkLsofData *data;
	gchar **lines = NULL;
	guint i;
	const gchar *value;
	gchar mode;
	gint pid = -1;
	PkLsofType type = PK_LSOF_TYPE_UNKNOWN;

	g_return_val_if_fail (PK_IS_LSOF (lsof), FALSE);

	/* try to find lsof */
	lsof_name = "/usr/sbin/lsof";
	ret = g_file_test (lsof_name, G_FILE_TEST_EXISTS);
	if (!ret) {
		lsof_name = "/usr/bin/lsof";
		ret = g_file_test (lsof_name, G_FILE_TEST_EXISTS);
		if (!ret) {
			g_warning ("lsof not found, cannot continue");
			goto out;
		}
	}

	/* run lsof to get all data */
	lsof_cmd = g_strjoin (" ", lsof_name, "-Fpfn", "-n", NULL);
	ret = g_spawn_command_line_sync (lsof_cmd, &stdout, &stderr, NULL, &error);
	if (!ret) {
		g_warning ("failed to get pids: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* clear */
	g_ptr_array_set_size (lsof->priv->list_data, 0);

	/* split into lines */
	lines = g_strsplit (stdout, "\n", -1);
	for (i=0; lines[i] != NULL; i++) {

		/* get mode */
		mode = lines[i][0];
		if (mode == '\0')
			continue;

		value = &lines[i][1];
		switch (mode) {
		case 'p':

			/* parse PID */
			ret = pk_lsof_strtoint (value, &pid);
			if (!ret) {
				g_warning ("failed to parse pid: '%s'", value);
				pid = -1;
				goto out;
			}
			break;
		case 'f':
			type = pk_lsof_type_from_string (value);
			break;
		case 'n':
			if (type == PK_LSOF_TYPE_DEL ||
			    type == PK_LSOF_TYPE_MEM) {

				/* no valid pid found */
				if (pid == -1)
					break;

				/* not a system library */
				if (strstr (value, "/lib/") == NULL)
					break;

				/* not a shared object */
				if (strstr (value, ".so") == NULL)
					break;

				/* add to array */
				data = pk_lsof_data_new (pid, value);
				g_ptr_array_add (lsof->priv->list_data, data);
			}
			break;
		default:
			g_debug ("ignoring %c=%s (type=%s)", mode, value, pk_lsof_type_to_string (type));
			break;
		}
	}
	ret = TRUE;
out:
	g_strfreev (lines);
	g_free (lsof_cmd);
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
			g_warning ("failed to refresh");
			goto out;
		}
	}

	/* create array of pids that are using this library */
	pids = g_ptr_array_new ();
	for (i=0; filenames[i] != NULL; i++) {
		for (j=0; j < list_data->len; j++) {
			data = g_ptr_array_index (list_data, j);
			g_debug ("got %s", data->filename);
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

	g_ptr_array_unref (lsof->priv->list_data);

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
	lsof->priv->list_data = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_lsof_data_free);
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

