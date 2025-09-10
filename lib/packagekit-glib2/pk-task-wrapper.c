/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-package-id.h>

#include "pk-task-wrapper.h"

struct _PkTaskWrapper
{
 PkTask parent;
};

G_DEFINE_TYPE (PkTaskWrapper, pk_task_wrapper, PK_TYPE_TASK)

/*
 * pk_task_wrapper_untrusted_question:
 **/
static void
pk_task_wrapper_untrusted_question (PkTask *task, guint request, PkResults *results)
{
	g_print ("UNTRUSTED\n");

	/* just accept without asking */
	pk_task_user_accepted (task, request);
}

/*
 * pk_task_wrapper_key_question:
 **/
static void
pk_task_wrapper_key_question (PkTask *task, guint request, PkResults *results)
{
	/* just accept without asking */
	pk_task_user_accepted (task, request);
}

/*
 * pk_task_wrapper_eula_question:
 **/
static void
pk_task_wrapper_eula_question (PkTask *task, guint request, PkResults *results)
{
	/* just accept without asking */
	pk_task_user_accepted (task, request);
}

/*
 * pk_task_wrapper_media_change_question:
 **/
static void
pk_task_wrapper_media_change_question (PkTask *task, guint request, PkResults *results)
{
	/* just accept without asking */
	pk_task_user_accepted (task, request);
}

/*
 * pk_task_wrapper_simulate_question:
 **/
static void
pk_task_wrapper_simulate_question (PkTask *task, guint request, PkResults *results)
{
	guint i;
	const gchar *package_id;
	PkPackage *package;
	g_autoptr(PkPackageSack) sack = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* get data */
	sack = pk_results_get_package_sack (results);

	/* print data */
	array = pk_package_sack_get_array (sack);
	for (i = 0; i < array->len; i++) {
		g_autofree gchar *printable = NULL;
		package = g_ptr_array_index (array, i);
		package_id = pk_package_get_id (package);
		printable = pk_package_id_to_printable (package_id);
		g_print ("%s\t%s\t%s\n", pk_info_enum_to_string (pk_package_get_info (package)),
			 printable, pk_package_get_summary (package));
	}

	/* just accept without asking */
	pk_task_user_accepted (task, request);
}

/*
 * pk_task_wrapper_class_init:
 **/
static void
pk_task_wrapper_class_init (PkTaskWrapperClass *klass)
{
	PkTaskClass *task_class = PK_TASK_CLASS (klass);

	task_class->untrusted_question = pk_task_wrapper_untrusted_question;
	task_class->key_question = pk_task_wrapper_key_question;
	task_class->eula_question = pk_task_wrapper_eula_question;
	task_class->media_change_question = pk_task_wrapper_media_change_question;
	task_class->simulate_question = pk_task_wrapper_simulate_question;
}

/*
 * pk_task_wrapper_init:
 * @task_wrapper: This class instance
 **/
static void
pk_task_wrapper_init (PkTaskWrapper *task)
{
}

/**
 * pk_task_wrapper_new:
 *
 * Return value: a new #PkTaskWrapper object.
 **/
PkTaskWrapper *
pk_task_wrapper_new (void)
{
	PkTaskWrapper *task;
	task = g_object_new (PK_TYPE_TASK_WRAPPER, NULL);
	return PK_TASK_WRAPPER (task);
}
