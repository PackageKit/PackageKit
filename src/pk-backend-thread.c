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
#include "pk-backend-thread.h"
#include "pk-marshal.h"
#include "pk-enum.h"
#include "pk-spawn.h"
#include "pk-time.h"
#include "pk-inhibit.h"
#include "pk-thread-list.h"

#define PK_BACKEND_THREAD_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND_THREAD, PkBackendThreadPrivate))

struct PkBackendThreadPrivate
{
	PkThreadList		*thread_list;
	PkBackend		*backend;
};

G_DEFINE_TYPE (PkBackendThread, pk_backend_thread, G_TYPE_OBJECT)

/**
 * pk_backend_thread_create:
 **/
gboolean
pk_backend_thread_create (PkBackendThread *backend_thread, PkBackendThreadFunc func, gpointer data)
{
	g_return_val_if_fail (PK_IS_BACKEND_THREAD (backend_thread), FALSE);
	return pk_thread_list_create (backend_thread->priv->thread_list, (PkThreadFunc) func, backend_thread, data);
}

/**
 * pk_backend_thread_get_backend:
 * Convenience function.
 **/
PkBackend *
pk_backend_thread_get_backend (PkBackendThread *backend_thread)
{
	g_return_val_if_fail (PK_IS_BACKEND_THREAD (backend_thread), NULL);
	return backend_thread->priv->backend;
}

/**
 * pk_backend_thread_finalize:
 **/
static void
pk_backend_thread_finalize (GObject *object)
{
	PkBackendThread *backend_thread;
	g_return_if_fail (PK_IS_BACKEND_THREAD (object));

	backend_thread = PK_BACKEND_THREAD (object);

	g_object_unref (backend_thread->priv->thread_list);
	g_object_unref (backend_thread->priv->backend);

	G_OBJECT_CLASS (pk_backend_thread_parent_class)->finalize (object);
}

/**
 * pk_backend_thread_class_init:
 **/
static void
pk_backend_thread_class_init (PkBackendThreadClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_backend_thread_finalize;
	g_type_class_add_private (klass, sizeof (PkBackendThreadPrivate));
}

/**
 * pk_backend_thread_init:
 **/
static void
pk_backend_thread_init (PkBackendThread *backend_thread)
{
	backend_thread->priv = PK_BACKEND_THREAD_GET_PRIVATE (backend_thread);
	backend_thread->priv->thread_list = pk_thread_list_new ();
	backend_thread->priv->backend = pk_backend_new ();
}

/**
 * pk_backend_thread_new:
 **/
PkBackendThread *
pk_backend_thread_new (void)
{
	PkBackendThread *backend_thread;
	backend_thread = g_object_new (PK_TYPE_BACKEND_THREAD, NULL);
	return PK_BACKEND_THREAD (backend_thread);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

static gboolean
pk_backend_thread_test_func_true (PkBackendThread *backend_thread, gpointer data)
{
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (backend_thread);

	g_usleep (1000*1000);
	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
pk_backend_thread_test_func_false (PkBackendThread *backend_thread, gpointer data)
{
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (backend_thread);

	g_usleep (1000*1000);
	pk_backend_finished (backend);
	return FALSE;
}

static gboolean
pk_backend_thread_test_func_immediate_false (PkBackendThread *backend_thread, gpointer data)
{
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (backend_thread);

	pk_backend_finished (backend);
	return FALSE;
}

void
libst_backend_thread (LibSelfTest *test)
{
	PkBackendThread *backend_thread;
	PkBackend *backend;
	gboolean ret;
	guint elapsed;

	if (libst_start (test, "PkBackendThread", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get an backend_thread");
	backend_thread = pk_backend_thread_new ();
	if (backend_thread != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get a backend");
	backend = pk_backend_thread_get_backend (backend_thread);
	if (backend != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/* needed to call initialize and destroy */
	ret = pk_backend_set_name (backend, "dummy");
	ret = pk_backend_lock (backend);

	/************************************************************/
	libst_title (test, "wait for a thread to return true");
	ret = pk_backend_thread_create (backend_thread, pk_backend_thread_test_func_true, NULL);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "wait for a thread failed");
	}

	/* wait */
	pk_thread_list_wait (backend_thread->priv->thread_list);
	elapsed = libst_elapsed (test);

	/************************************************************/
	libst_title (test, "did we wait the correct time?");
	if (elapsed < 1100 && elapsed > 900) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not wait for thread timeout %ims", elapsed);
	}

	/* reset the backend */
	g_object_unref (backend_thread);
	backend_thread = pk_backend_thread_new ();
	backend = pk_backend_thread_get_backend (backend_thread);

	/* needed to call initialize and destroy */
	ret = pk_backend_set_name (backend, "dummy");
	ret = pk_backend_lock (backend);

	/************************************************************/
	libst_title (test, "wait for a thread to return false");
	ret = pk_backend_thread_create (backend_thread, pk_backend_thread_test_func_false, NULL);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "wait for a thread failed");
	}

	/* wait */
	pk_thread_list_wait (backend_thread->priv->thread_list);
	elapsed = libst_elapsed (test);

	/************************************************************/
	libst_title (test, "did we wait the correct time2?");
	if (elapsed < 1100 && elapsed > 900) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not wait for thread timeout %ims", elapsed);
	}

	/* reset the backend */
	g_object_unref (backend_thread);
	backend_thread = pk_backend_thread_new ();
	backend = pk_backend_thread_get_backend (backend_thread);

	/* needed to call initialize and destroy */
	ret = pk_backend_set_name (backend, "dummy");
	ret = pk_backend_lock (backend);

	/************************************************************/
	libst_title (test, "wait for a thread to return false (straight away)");
	ret = pk_backend_thread_create (backend_thread, pk_backend_thread_test_func_immediate_false, NULL);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "returned false!");
	}
	elapsed = libst_elapsed (test);

	/************************************************************/
	libst_title (test, "did we wait the correct time2?");
	if (elapsed < 100) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "did not wait for thread timeout2");
	}

	g_object_unref (backend_thread);

	libst_end (test);
}
#endif

