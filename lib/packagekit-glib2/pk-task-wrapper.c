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

#include "config.h"

#include <packagekit-glib2/pk-task-wrapper.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>

#include "egg-debug.h"

static void     pk_task_wrapper_finalize	(GObject     *object);

#define PK_TASK_WRAPPER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK_WRAPPER, PkTaskWrapperPrivate))

/**
 * PkTaskWrapperPrivate:
 *
 * Private #PkTaskWrapper data
 **/
struct _PkTaskWrapperPrivate
{
	gpointer		 user_data;
};

G_DEFINE_TYPE (PkTaskWrapper, pk_task_wrapper, PK_TYPE_TASK)

/**
 * pk_task_wrapper_untrusted_question:
 **/
static void
dkp_task_wrapper_untrusted_question (PkTask *task, guint request, const PkResults *results)
{
	PkTaskWrapperPrivate *priv = PK_TASK_WRAPPER(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	g_print ("UNTRUSTED\n");

	/* just accept without asking */
	pk_task_user_acceptance (task, request);
}

/**
 * pk_task_wrapper_key_question:
 **/
static void
dkp_task_wrapper_key_question (PkTask *task, guint request, const PkResults *results)
{
	guint i;
	GPtrArray *array;
	PkResultItemRepoSignatureRequired *item;
	PkTaskWrapperPrivate *priv = PK_TASK_WRAPPER(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	/* get data */
	array = pk_results_get_repo_signature_required_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_print ("KEY:\n");
		g_print (" Package: %s\n", item->package_id);
		g_print (" Name: %s\n", item->repository_name);
		g_print (" URL: %s\n", item->key_url);
		g_print (" User: %s\n", item->key_userid);
		g_print (" ID: %s\n", item->key_id);
		g_print (" Fingerprint: %s\n", item->key_fingerprint);
		g_print (" Timestamp: %s\n", item->key_timestamp);
	}

	/* just accept without asking */
	pk_task_user_acceptance (task, request);

	g_ptr_array_unref (array);
}

/**
 * pk_task_wrapper_eula_question:
 **/
static void
dkp_task_wrapper_eula_question (PkTask *task, guint request, const PkResults *results)
{
	guint i;
	GPtrArray *array;
	PkResultItemEulaRequired *item;
	PkTaskWrapperPrivate *priv = PK_TASK_WRAPPER(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	/* get data */
	array = pk_results_get_eula_required_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_print ("EULA:\n");
		g_print (" Eula ID: %s\n", item->eula_id);
		g_print (" Package: %s\n", item->package_id);
		g_print (" Vendor: %s\n", item->vendor_name);
		g_print (" Agreement: %s\n", item->license_agreement);
	}

	/* just accept without asking */
	pk_task_user_acceptance (task, request);

	g_ptr_array_unref (array);
}

/**
 * pk_task_wrapper_media_change_question:
 **/
static void
dkp_task_wrapper_media_change_question (PkTask *task, guint request, const PkResults *results)
{
	guint i;
	GPtrArray *array;
	PkResultItemMediaChangeRequired *item;
	PkTaskWrapperPrivate *priv = PK_TASK_WRAPPER(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	/* get data */
	array = pk_results_get_media_change_required_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_print ("MEDIA CHANGE:\n");
		g_print (" Media type: %s\n", pk_media_type_enum_to_text (item->media_type));
		g_print (" ID: %s\n", item->media_id);
		g_print (" Text: %s\n", item->media_text);
	}

	/* just accept without asking */
	pk_task_user_acceptance (task, request);

	g_ptr_array_unref (array);
}

/**
 * pk_task_wrapper_class_init:
 **/
static void
pk_task_wrapper_class_init (PkTaskWrapperClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	PkTaskClass *task_class = PK_TASK_CLASS (klass);

	object_class->finalize = pk_task_wrapper_finalize;
	task_class->untrusted_question = dkp_task_wrapper_untrusted_question;
	task_class->key_question = dkp_task_wrapper_key_question;
	task_class->eula_question = dkp_task_wrapper_eula_question;
	task_class->media_change_question = dkp_task_wrapper_media_change_question;

	g_type_class_add_private (klass, sizeof (PkTaskWrapperPrivate));
}

/**
 * pk_task_wrapper_init:
 * @task_wrapper: This class instance
 **/
static void
pk_task_wrapper_init (PkTaskWrapper *task)
{
	task->priv = PK_TASK_WRAPPER_GET_PRIVATE (task);
	task->priv->user_data = NULL;
}

/**
 * pk_task_wrapper_finalize:
 * @object: The object to finalize
 **/
static void
pk_task_wrapper_finalize (GObject *object)
{
	PkTaskWrapper *task = PK_TASK_WRAPPER (object);
	task->priv->user_data = NULL;
	G_OBJECT_CLASS (pk_task_wrapper_parent_class)->finalize (object);
}

/**
 * pk_task_wrapper_new:
 *
 * Return value: a new PkTaskWrapper object.
 **/
PkTaskWrapper *
pk_task_wrapper_new (void)
{
	PkTaskWrapper *task;
	task = g_object_new (PK_TYPE_TASK_WRAPPER, NULL);
	return PK_TASK_WRAPPER (task);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static void
pk_task_wrapper_test_install_packages_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkTaskWrapper *task = PK_TASK_WRAPPER (object);
	GError *error = NULL;
	const PkResults *results = NULL;
	PkExitEnum exit_enum;
	GPtrArray *packages;
	const PkResultItemPackage *item;
	guint i;

	/* get the results */
	results = pk_task_generic_finish (PK_TASK (task), res, &error);
	if (results == NULL) {
		egg_test_failed (test, "failed to resolve: %s", error->message);
		g_error_free (error);
		return;
	}

	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		egg_test_failed (test, "failed to resolve success: %s", pk_exit_enum_to_text (exit_enum));

	packages = pk_results_get_package_array (results);
	if (packages == NULL)
		egg_test_failed (test, "no packages!");

	/* list, just for shits and giggles */
	for (i=0; i<packages->len; i++) {
		item = g_ptr_array_index (packages, i);
		egg_debug ("%s\t%s\t%s", pk_info_enum_to_text (item->info_enum), item->package_id, item->summary);
	}

	if (packages->len != 5)
		egg_test_failed (test, "invalid number of packages: %i", packages->len);

	g_ptr_array_unref (packages);

	egg_debug ("results exit enum = %s", pk_exit_enum_to_text (exit_enum));
	egg_test_loop_quit (test);
}

static void
pk_task_wrapper_test_progress_cb (PkProgress *progress, PkProgressType type, EggTest *test)
{
	PkStatusEnum status;
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
			      "status", &status,
			      NULL);
		egg_debug ("now %s", pk_status_enum_to_text (status));
	}
}

void
pk_task_wrapper_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	PkTaskWrapper *task;
	gchar **package_ids;

	if (!egg_test_start (test, "PkTaskWrapper"))
		return;

	/************************************************************/
	egg_test_title (test, "get task_wrapper");
	task = pk_task_wrapper_new ();
	egg_test_assert (test, task != NULL);

	/************************************************************/
	egg_test_title (test, "install package");
	package_ids = g_strsplit ("glib2;2.14.0;i386;fedora", ",", -1);
	pk_task_install_packages_async (PK_TASK (task), package_ids, NULL,
				        (PkProgressCallback) pk_task_wrapper_test_progress_cb, test,
				        (GAsyncReadyCallback) pk_task_wrapper_test_install_packages_cb, test);
	g_strfreev (package_ids);
	egg_test_loop_wait (test, 150000);
	egg_test_success (test, "installed in %i", egg_test_elapsed (test));

	g_object_unref (task);
	egg_test_end (test);
}
#endif

