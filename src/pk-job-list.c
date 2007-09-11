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
#include <signal.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include "pk-debug.h"
#include "pk-job-list.h"

static void     pk_job_list_class_init	(PkJobListClass *klass);
static void     pk_job_list_init	(PkJobList      *job_list);
static void     pk_job_list_finalize	(GObject        *object);

#define PK_JOB_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_JOB_LIST, PkJobListPrivate))
#define PK_JOB_LIST_COUNT_FILE		LOCALSTATEDIR "/run/PackageKit/job_count.dat"

struct PkJobListPrivate
{
	GPtrArray		*array;
	guint			 job_count;
};

enum {
	PK_JOB_LIST_CHANGED,
	PK_JOB_LIST_LAST_SIGNAL
};

static guint signals [PK_JOB_LIST_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkJobList, pk_job_list, G_TYPE_OBJECT)

/**
 * pk_job_list_load_job_count:
 **/
static gboolean
pk_job_list_load_job_count (PkJobList *job_list)
{
	gboolean ret;
	gchar *contents;
	ret = g_file_get_contents (PK_JOB_LIST_COUNT_FILE, &contents, NULL, NULL);
	if (ret == FALSE) {
		pk_warning ("failed to get last job");
		return FALSE;
	}
	job_list->priv->job_count = atoi (contents);
	pk_debug ("job=%i", job_list->priv->job_count);
	g_free (contents);
	return TRUE;
}

/**
 * pk_job_list_save_job_count:
 **/
static gboolean
pk_job_list_save_job_count (PkJobList *job_list)
{
	gboolean ret;
	gchar *contents;

	pk_debug ("saving %i", job_list->priv->job_count);
	contents = g_strdup_printf ("%i", job_list->priv->job_count);
	ret = g_file_set_contents (PK_JOB_LIST_COUNT_FILE, contents, -1, NULL);
	g_free (contents);
	if (ret == FALSE) {
		pk_warning ("failed to set last job");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_job_list_role_present:
 *
 * if there is a queued job with this role, useful to avoid having
 * multiple system updates queued
 **/
gboolean
pk_job_list_role_present (PkJobList *job_list, PkRoleEnum role)
{
	guint i;
	guint length;
	PkRoleEnum role_temp;
	PkJobListItem *item;

	g_return_val_if_fail (job_list != NULL, FALSE);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), FALSE);

	/* check for existing job doing an update */
	length = job_list->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkJobListItem *) g_ptr_array_index (job_list->priv->array, i);
		pk_backend_get_job_role (item->task, &role_temp, NULL);
		if (role_temp == role) {
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * pk_job_list_add:
 **/
 /* create transaction_id, add to array, mark changed */
PkJobListItem *
pk_job_list_add (PkJobList *job_list, PkTask *task)
{
	PkJobListItem *item;

	g_return_val_if_fail (job_list != NULL, NULL);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), NULL);

	/* increment the job number - we never repeat an id */
	job_list->priv->job_count++;

	/* add to the array */
	item = g_new0 (PkJobListItem, 1);
	item->valid = FALSE;
	item->task = task;
	item->job = job_list->priv->job_count;
	g_ptr_array_add (job_list->priv->array, item);

	/* in an ideal world we don't need this, but do it in case the daemon is ctrl-c;d */
	pk_job_list_save_job_count (job_list);
	return item;
}

/**
 * pk_job_list_remove:
 **/
gboolean
pk_job_list_remove (PkJobList *job_list, PkTask *task)
{
	PkJobListItem *item;
	g_return_val_if_fail (job_list != NULL, FALSE);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), FALSE);

	item = pk_job_list_get_item_from_task (job_list, task);
	if (item == NULL) {
		return FALSE;
	}
	g_ptr_array_remove (job_list->priv->array, item);
	g_free (item);
	return TRUE;
}

/**
 * pk_job_list_commit:
 **/
gboolean
pk_job_list_commit (PkJobList *job_list, PkTask *task)
{
	PkJobListItem *item;
	g_return_val_if_fail (job_list != NULL, FALSE);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), FALSE);

	item = pk_job_list_get_item_from_task (job_list, task);
	if (item == NULL) {
		return FALSE;
	}
	pk_debug ("marking job %i as valid", item->job);
	item->valid = TRUE;
	return TRUE;
}

/**
 * pk_job_list_get_array:
 **/
GArray *
pk_job_list_get_array (PkJobList *job_list)
{
	guint i;
	guint length;
	GArray *array;
	PkJobListItem *item;

	g_return_val_if_fail (job_list != NULL, NULL);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), NULL);

	/* create new list */
	array = g_array_new (FALSE, FALSE, sizeof (guint));

	/* find all the jobs in progress */
	length = job_list->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkJobListItem *) g_ptr_array_index (job_list->priv->array, i);
		/* only return in the list if it worked */
		if (item->valid == TRUE) {
			array = g_array_append_val (array, item->job);
		}
	}
	return array;
}

/**
 * pk_job_list_get_size:
 **/
guint
pk_job_list_get_size (PkJobList *job_list)
{
	g_return_val_if_fail (job_list != NULL, 0);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), 0);
	return job_list->priv->array->len;
}

/**
 * pk_job_list_get_item_from_job:
 **/
PkJobListItem *
pk_job_list_get_item_from_job (PkJobList *job_list, guint job)
{
	guint i;
	guint length;
	PkJobListItem *item;

	g_return_val_if_fail (job_list != NULL, NULL);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), NULL);

	/* find the task with the job ID */
	length = job_list->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkJobListItem *) g_ptr_array_index (job_list->priv->array, i);
		if (item->job == job) {
			return item;
		}
	}
	return NULL;
}

/**
 * pk_job_list_get_item_from_task:
 **/
PkJobListItem *
pk_job_list_get_item_from_task (PkJobList *job_list, PkTask *task)
{
	guint i;
	guint length;
	PkJobListItem *item;

	g_return_val_if_fail (job_list != NULL, NULL);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), NULL);

	/* find the task with the job ID */
	length = job_list->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkJobListItem *) g_ptr_array_index (job_list->priv->array, i);
		if (item->task == task) {
			return item;
		}
	}
	return NULL;
}

/**
 * pk_job_list_class_init:
 * @klass: The PkJobListClass
 **/
static void
pk_job_list_class_init (PkJobListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_job_list_finalize;

	signals [PK_JOB_LIST_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkJobListPrivate));
}

/**
 * pk_job_list_init:
 * @job_list: This class instance
 **/
static void
pk_job_list_init (PkJobList *job_list)
{
	job_list->priv = PK_JOB_LIST_GET_PRIVATE (job_list);
	job_list->priv->array = g_ptr_array_new ();
	job_list->priv->job_count = pk_job_list_load_job_count (job_list);
}

/**
 * pk_job_list_finalize:
 * @object: The object to finalize
 **/
static void
pk_job_list_finalize (GObject *object)
{
	PkJobList *job_list;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_JOB_LIST (object));

	job_list = PK_JOB_LIST (object);

	g_return_if_fail (job_list->priv != NULL);

	g_ptr_array_free (job_list->priv->array, TRUE);
	/* save last job id so we don't ever repeat */
	pk_job_list_save_job_count (job_list);

	G_OBJECT_CLASS (pk_job_list_parent_class)->finalize (object);
}

/**
 * pk_job_list_new:
 *
 * Return value: a new PkJobList object.
 **/
PkJobList *
pk_job_list_new (void)
{
	PkJobList *job_list;
	job_list = g_object_new (PK_TYPE_JOB_LIST, NULL);
	return PK_JOB_LIST (job_list);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

static GMainLoop *loop;

void
libst_job_list (LibSelfTest *test)
{
	PkJobList *job_list;
	gboolean ret;

	if (libst_start (test, "PkJobList", CLASS_AUTO) == FALSE) {
		return;
	}

	job_list = pk_job_list_new ();

	/************************************************************/
	libst_title (test, "make sure return error for missing file");
	ret = pk_job_list_foo (job_list, " ");
	if (ret == FALSE) {
		libst_success (test, "failed to run invalid file");
	} else {
		libst_failed (test, "ran incorrect file");
	}

	g_object_unref (job_list);

	libst_end (test);
}
#endif

