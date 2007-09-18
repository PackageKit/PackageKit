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
#include "pk-transaction-list.h"

static void     pk_transaction_list_class_init	(PkTransactionListClass *klass);
static void     pk_transaction_list_init	(PkTransactionList      *job_list);
static void     pk_transaction_list_finalize	(GObject        *object);

#define PK_TRANSACTION_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_JOB_LIST, PkTransactionListPrivate))
#define PK_TRANSACTION_LIST_COUNT_FILE		LOCALSTATEDIR "/run/PackageKit/job_count.dat"

struct PkTransactionListPrivate
{
	GPtrArray		*array;
	guint			 job_count;
};

enum {
	PK_TRANSACTION_LIST_CHANGED,
	PK_TRANSACTION_LIST_LAST_SIGNAL
};

static guint signals [PK_TRANSACTION_LIST_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTransactionList, pk_transaction_list, G_TYPE_OBJECT)

/**
 * pk_transaction_list_load_job_count:
 **/
static gboolean
pk_transaction_list_load_job_count (PkTransactionList *job_list)
{
	gboolean ret;
	gchar *contents;
	ret = g_file_get_contents (PK_TRANSACTION_LIST_COUNT_FILE, &contents, NULL, NULL);
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
 * pk_transaction_list_save_job_count:
 **/
static gboolean
pk_transaction_list_save_job_count (PkTransactionList *job_list)
{
	gboolean ret;
	gchar *contents;

	pk_debug ("saving %i", job_list->priv->job_count);
	contents = g_strdup_printf ("%i", job_list->priv->job_count);
	ret = g_file_set_contents (PK_TRANSACTION_LIST_COUNT_FILE, contents, -1, NULL);
	g_free (contents);
	if (ret == FALSE) {
		pk_warning ("failed to set last job");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_transaction_list_role_present:
 *
 * if there is a queued job with this role, useful to avoid having
 * multiple system updates queued
 **/
gboolean
pk_transaction_list_role_present (PkTransactionList *job_list, PkRoleEnum role)
{
	guint i;
	guint length;
	PkRoleEnum role_temp;
	PkTransactionItem *item;

	g_return_val_if_fail (job_list != NULL, FALSE);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), FALSE);

	/* check for existing job doing an update */
	length = job_list->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (job_list->priv->array, i);
		pk_backend_get_role (item->task, &role_temp, NULL);
		if (role_temp == role) {
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * pk_transaction_list_tid_get_random_hex_string:
 **/
static gchar *
pk_transaction_list_tid_get_random_hex_string (guint length)
{
	GRand *rand;
	gint32 num;
	gchar *string;
	guint i;

	rand = g_rand_new ();

	/* allocate a string with the correct size */
	string = g_strnfill (length, 'x');
	for (i=0; i<length; i++) {
		num = g_rand_int_range (rand, (gint32) 'a', (gint32) 'f');
		/* assign a random number as a char */
		string[i] = (gchar) num;
	}
	g_rand_free (rand);
	return string;
}

/**
 * pk_transaction_list_tid_id_generate:
 **/
gchar *
pk_transaction_list_tid_id_generate (void)
{
	gchar *random;
	gchar *job;
	gchar *tid;
	random = pk_transaction_list_tid_get_random_hex_string (8);
	job = g_strdup_printf ("%i", 0);
	tid = g_strjoin (";", job, random, "data", NULL);
	g_free (random);
	g_free (job);
	return tid;
}

/**
 * pk_transaction_list_add:
 **/
PkTransactionItem *
pk_transaction_list_add (PkTransactionList *job_list, PkTask *task)
{
	PkTransactionItem *item;

	g_return_val_if_fail (job_list != NULL, NULL);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), NULL);

	/* increment the job number - we never repeat an id */
	job_list->priv->job_count++;

	/* add to the array */
	item = g_new0 (PkTransactionItem, 1);
	item->valid = FALSE;
	item->task = task;
	item->job = job_list->priv->job_count;
	item->tid = pk_transaction_list_tid_id_generate ();
	g_ptr_array_add (job_list->priv->array, item);

	/* in an ideal world we don't need this, but do it in case the daemon is ctrl-c;d */
	pk_transaction_list_save_job_count (job_list);
	return item;
}

/**
 * pk_transaction_list_remove:
 **/
gboolean
pk_transaction_list_remove (PkTransactionList *job_list, PkTask *task)
{
	PkTransactionItem *item;
	g_return_val_if_fail (job_list != NULL, FALSE);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), FALSE);

	item = pk_transaction_list_get_item_from_task (job_list, task);
	if (item == NULL) {
		return FALSE;
	}
	g_ptr_array_remove (job_list->priv->array, item);
	g_free (item->tid);
	g_free (item);
	return TRUE;
}

/**
 * pk_transaction_list_commit:
 **/
gboolean
pk_transaction_list_commit (PkTransactionList *job_list, PkTask *task)
{
	PkTransactionItem *item;
	g_return_val_if_fail (job_list != NULL, FALSE);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), FALSE);

	item = pk_transaction_list_get_item_from_task (job_list, task);
	if (item == NULL) {
		return FALSE;
	}
	pk_debug ("marking job %i as valid", item->job);
	item->valid = TRUE;
	return TRUE;
}

/**
 * pk_transaction_list_get_array:
 **/
GArray *
pk_transaction_list_get_array (PkTransactionList *job_list)
{
	guint i;
	guint length;
	GArray *array;
	PkTransactionItem *item;

	g_return_val_if_fail (job_list != NULL, NULL);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), NULL);

	/* create new list */
	array = g_array_new (FALSE, FALSE, sizeof (guint));

	/* find all the jobs in progress */
	length = job_list->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (job_list->priv->array, i);
		/* only return in the list if it worked */
		if (item->valid == TRUE) {
			array = g_array_append_val (array, item->job);
		}
	}
	return array;
}

/**
 * pk_transaction_list_get_size:
 **/
guint
pk_transaction_list_get_size (PkTransactionList *job_list)
{
	g_return_val_if_fail (job_list != NULL, 0);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), 0);
	return job_list->priv->array->len;
}

/**
 * pk_transaction_list_get_item_from_job:
 **/
PkTransactionItem *
pk_transaction_list_get_item_from_job (PkTransactionList *job_list, guint job)
{
	guint i;
	guint length;
	PkTransactionItem *item;

	g_return_val_if_fail (job_list != NULL, NULL);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), NULL);

	/* find the task with the job ID */
	length = job_list->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (job_list->priv->array, i);
		if (item->job == job) {
			return item;
		}
	}
	return NULL;
}

/**
 * pk_transaction_list_get_item_from_task:
 **/
PkTransactionItem *
pk_transaction_list_get_item_from_task (PkTransactionList *job_list, PkTask *task)
{
	guint i;
	guint length;
	PkTransactionItem *item;

	g_return_val_if_fail (job_list != NULL, NULL);
	g_return_val_if_fail (PK_IS_JOB_LIST (job_list), NULL);

	/* find the task with the job ID */
	length = job_list->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (job_list->priv->array, i);
		if (item->task == task) {
			return item;
		}
	}
	return NULL;
}

/**
 * pk_transaction_list_class_init:
 * @klass: The PkTransactionListClass
 **/
static void
pk_transaction_list_class_init (PkTransactionListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_transaction_list_finalize;

	signals [PK_TRANSACTION_LIST_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkTransactionListPrivate));
}

/**
 * pk_transaction_list_init:
 * @job_list: This class instance
 **/
static void
pk_transaction_list_init (PkTransactionList *job_list)
{
	job_list->priv = PK_TRANSACTION_LIST_GET_PRIVATE (job_list);
	job_list->priv->array = g_ptr_array_new ();
	job_list->priv->job_count = pk_transaction_list_load_job_count (job_list);
}

/**
 * pk_transaction_list_finalize:
 * @object: The object to finalize
 **/
static void
pk_transaction_list_finalize (GObject *object)
{
	PkTransactionList *job_list;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_JOB_LIST (object));

	job_list = PK_TRANSACTION_LIST (object);

	g_return_if_fail (job_list->priv != NULL);

	g_ptr_array_free (job_list->priv->array, TRUE);
	/* save last job id so we don't ever repeat */
	pk_transaction_list_save_job_count (job_list);

	G_OBJECT_CLASS (pk_transaction_list_parent_class)->finalize (object);
}

/**
 * pk_transaction_list_new:
 *
 * Return value: a new PkTransactionList object.
 **/
PkTransactionList *
pk_transaction_list_new (void)
{
	PkTransactionList *job_list;
	job_list = g_object_new (PK_TYPE_JOB_LIST, NULL);
	return PK_TRANSACTION_LIST (job_list);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_transaction_list (LibSelfTest *test)
{
	PkTransactionList *job_list;
	gchar *tid;

	if (libst_start (test, "PkTransactionList", CLASS_AUTO) == FALSE) {
		return;
	}

	job_list = pk_transaction_list_new ();

	/************************************************************/
	libst_title (test, "make sure we get a valid tid");
	tid = pk_transaction_list_tid_id_generate ();
	if (tid != NULL) {
		libst_success (test, "got tid %s", tid);
	} else {
		libst_failed (test, "failed to get tid");
	}
	g_free (tid);

	g_object_unref (job_list);

	libst_end (test);
}
#endif

