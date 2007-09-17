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
#include "pk-thread-list.h"

static void     pk_thread_list_class_init	(PkThreadListClass *klass);
static void     pk_thread_list_init	(PkThreadList      *tlist);
static void     pk_thread_list_finalize	(GObject        *object);

#define PK_THREAD_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_THREAD_LIST, PkThreadListPrivate))
#define PK_THREAD_LIST_COUNT_FILE		LOCALSTATEDIR "/run/PackageKit/thread_count.dat"

struct PkThreadListPrivate
{
	GPtrArray		*thread_list;
};

G_DEFINE_TYPE (PkThreadList, pk_thread_list, G_TYPE_OBJECT)

typedef struct
{
	GThread			*thread;
	gpointer		 param;
	gpointer		 data;
	gboolean		 running;
	PkThreadFunc		 func;
} PkThreadListItem;

/**
 * pk_thread_list_item_new:
 **/
static void *
pk_thread_list_item_new (gpointer data)
{
	PkThreadListItem *item = (PkThreadListItem *) data;
	gboolean ret;
	pk_debug ("running %p", item->func);
	ret = item->func (item->param, item->data);
	pk_debug ("finished %p, ret is %i", item->func, ret);
	item->running = FALSE;
	return NULL;
}

/**
 * pk_thread_list_create:
 **/
gboolean
pk_thread_list_create (PkThreadList *tlist, PkThreadFunc func, gpointer param, gpointer data)
{
	PkThreadListItem *item;

	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_THREAD_LIST (tlist), FALSE);

	item = g_new0 (PkThreadListItem, 1);

	/* create a new thread object */
	item->func = func;
	item->param = param;
	item->data = data;
	item->running = TRUE;
	item->thread = g_thread_create (pk_thread_list_item_new, item, TRUE, NULL);

	/* add to list */
	g_ptr_array_add (tlist->priv->thread_list, item);
	pk_debug ("created thread %p", item->thread);
	return TRUE;
}

/**
 * pk_thread_list_wait:
 **/
gboolean
pk_thread_list_wait (PkThreadList *tlist)
{
	guint i;
	guint length;
	PkThreadListItem *item;

	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_THREAD_LIST (tlist), FALSE);

	/* wait for all the threads to finish */
	length = tlist->priv->thread_list->len;
	for (i=0; i<length; i++) {
		item = (PkThreadListItem *) g_ptr_array_index (tlist->priv->thread_list, i);
		if (item->running == TRUE) {
			pk_debug ("joining thread %p", item->thread);
			g_thread_join (item->thread);
		} else {
			pk_debug ("ignoring exited thread %p", item->thread);
		}
	}
	return TRUE;
}

/**
 * pk_thread_list_class_init:
 * @klass: The PkThreadListClass
 **/
static void
pk_thread_list_class_init (PkThreadListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_thread_list_finalize;
	g_type_class_add_private (klass, sizeof (PkThreadListPrivate));
}

/**
 * pk_thread_list_init:
 * @thread_list: This class instance
 **/
static void
pk_thread_list_init (PkThreadList *tlist)
{
	tlist->priv = PK_THREAD_LIST_GET_PRIVATE (tlist);
	tlist->priv->thread_list = g_ptr_array_new ();
}

/**
 * pk_thread_list_finalize:
 * @object: The object to finalize
 **/
static void
pk_thread_list_finalize (GObject *object)
{
	PkThreadList *tlist;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_THREAD_LIST (object));

	tlist = PK_THREAD_LIST (object);
	g_return_if_fail (tlist->priv != NULL);
	g_ptr_array_free (tlist->priv->thread_list, TRUE);
	G_OBJECT_CLASS (pk_thread_list_parent_class)->finalize (object);
}

/**
 * pk_thread_list_new:
 *
 * Return value: a new PkThreadList object.
 **/
PkThreadList *
pk_thread_list_new (void)
{
	PkThreadList *tlist;
	tlist = g_object_new (PK_TYPE_THREAD_LIST, NULL);
	return PK_THREAD_LIST (tlist);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

gboolean done_func1 = FALSE;
gboolean done_func2 = FALSE;

static gboolean
test_func1 (PkThreadList *tlist, gpointer data)
{
	GTimer *timer;
	gdouble elapsed;

	if (tlist != GINT_TO_POINTER(0x01) || data != GINT_TO_POINTER(0x02)) {
		pk_debug ("WRONG PARAMS (%p, %p)", tlist, data);
		return FALSE;
	}
	pk_debug ("started task (%p,%p)", tlist, data);
	timer = g_timer_new ();
	do {
		g_usleep (1000*100);
		g_thread_yield ();
		elapsed = g_timer_elapsed (timer, NULL);
		pk_debug ("elapsed task (%p,%p) = %f", tlist, data, elapsed);
	} while (elapsed < 2.0);
	g_timer_destroy (timer);
	pk_debug ("exited task (%p,%p)", tlist, data);
	done_func1 = TRUE;
	return TRUE;
}

static gboolean
test_func2 (PkThreadList *tlist, gpointer data)
{
	GTimer *timer;
	gdouble elapsed;

	if (tlist != GINT_TO_POINTER(0x02) || data != GINT_TO_POINTER(0x03)) {
		pk_debug ("WRONG PARAMS (%p, %p)", tlist, data);
		return FALSE;
	}
	pk_debug ("started task (%p,%p)", tlist, data);
	timer = g_timer_new ();
	do {
		g_usleep (1000*100);
		elapsed = g_timer_elapsed (timer, NULL);
		pk_debug ("elapsed task (%p,%p) = %f", tlist, data, elapsed);
	} while (elapsed < 1.0);
	g_timer_destroy (timer);
	pk_debug ("exited task (%p,%p)", tlist, data);
	done_func2 = TRUE;
	return TRUE;
}

void
libst_thread_list (LibSelfTest *test)
{
	PkThreadList *tlist;
	gboolean ret;

	if (libst_start (test, "PkThreadList", CLASS_AUTO) == FALSE) {
		return;
	}

	tlist = pk_thread_list_new ();

	/************************************************************/
	libst_title (test, "create task 1");
	ret = pk_thread_list_create (tlist, test_func1, GINT_TO_POINTER(0x01), GINT_TO_POINTER(0x02));
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed to create task1");
	}

	/************************************************************/
	libst_title (test, "create task 2");
	ret = pk_thread_list_create (tlist, test_func2, GINT_TO_POINTER(0x02), GINT_TO_POINTER(0x03));
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed to create task2");
	}

	/************************************************************/
	libst_title (test, "wait for finish");
	ret = pk_thread_list_wait (tlist);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed to wait for task");
	}

	/************************************************************/
	libst_title (test, "ran func1 to completion");
	if (done_func1 == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed to run func1");
	}

	/************************************************************/
	libst_title (test, "ran func2 to completion");
	if (done_func2 == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed to run func2");
	}

	g_object_unref (tlist);

	libst_end (test);
}
#endif

