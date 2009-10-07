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

#include <glib/gi18n.h>
#include <packagekit-glib2/pk-task.h>
#include <packagekit-glib2/pk-item.h>
#include <packagekit-glib2/pk-package-id.h>

#include "egg-debug.h"

#include "pk-task-text.h"
#include "pk-console-shared.h"

static void     pk_task_text_finalize	(GObject     *object);

#define PK_TASK_TEXT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK_TEXT, PkTaskTextPrivate))

/**
 * PkTaskTextPrivate:
 *
 * Private #PkTaskText data
 **/
struct _PkTaskTextPrivate
{
	gpointer		 user_data;
};

G_DEFINE_TYPE (PkTaskText, pk_task_text, PK_TYPE_TASK)

/**
 * pk_task_text_untrusted_question:
 **/
static void
pk_task_text_untrusted_question (PkTask *task, guint request, PkResults *results)
{
	gboolean ret;
	PkTaskTextPrivate *priv = PK_TASK_TEXT(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	/* clear new line */
	g_print ("\n");

	/* TRANSLATORS: ask the user if they are comfortable installing insecure packages */
	ret = pk_console_get_prompt (_("Do you want to allow installing of unsigned software?"), FALSE);
	if (ret) {
		pk_task_user_accepted (task, request);
	} else {
		/* TRANSLATORS: tell the user we've not done anything */
		g_print ("%s\n", _("The unsigned software will not be installed."));
		pk_task_user_declined (task, request);
	}
}

/**
 * pk_task_text_key_question:
 **/
static void
pk_task_text_key_question (PkTask *task, guint request, PkResults *results)
{
	guint i;
	gboolean ret;
	GPtrArray *array;
	gchar *package = NULL;
	PkItemRepoSignatureRequired *item;
	PkTaskTextPrivate *priv = PK_TASK_TEXT(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	/* clear new line */
	g_print ("\n");

	/* get data */
	array = pk_results_get_repo_signature_required_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* create printable */
		package = pk_package_id_to_printable (item->package_id);

		/* TRANSLATORS: the package repository is signed by a key that is not recognised */
		g_print ("%s\n", _("Software source signature required"));

		/* TRANSLATORS: the package that is not signed by a known key */
		g_print (" %s: %s\n", _("Package"), package);

		/* TRANSLATORS: the package repository name */
		g_print (" %s: %s\n", _("Software source name"), item->repository_name);

		/* TRANSLATORS: the key URL */
		g_print (" %s: %s\n", _("Key URL"), item->key_url);

		/* TRANSLATORS: the username of the key */
		g_print (" %s: %s\n", _("Key user"), item->key_userid);

		/* TRANSLATORS: the key ID, usually a few hex digits */
		g_print (" %s: %s\n", _("Key ID"), item->key_id);

		/* TRANSLATORS: the key fingerprint, again, yet more hex */
		g_print (" %s: %s\n", _("Key fingerprint"), item->key_fingerprint);

		/* TRANSLATORS: the timestamp (a bit like a machine readable time) */
		g_print (" %s: %s\n", _("Key Timestamp"), item->key_timestamp);

		g_free (package);
	}

	/* TRANSLATORS: ask the user if they want to import */
	ret = pk_console_get_prompt (_("Do you accept this signature?"), FALSE);
	if (ret) {
		pk_task_user_accepted (task, request);
	} else {
		/* TRANSLATORS: tell the user we've not done anything */
		g_print ("%s\n", _("The signature was not accepted."));
		pk_task_user_declined (task, request);
	}

	g_ptr_array_unref (array);
}

/**
 * pk_task_text_eula_question:
 **/
static void
pk_task_text_eula_question (PkTask *task, guint request, PkResults *results)
{
	guint i;
	gboolean ret;
	gchar *package = NULL;
	GPtrArray *array;
	PkItemEulaRequired *item;
	PkTaskTextPrivate *priv = PK_TASK_TEXT(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	/* clear new line */
	g_print ("\n");

	/* get data */
	array = pk_results_get_eula_required_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* create printable */
		package = pk_package_id_to_printable (item->package_id);

		/* TRANSLATORS: this is another name for a software licence that has to be read before installing */
		g_print ("%s\n", _("End user licence agreement required"));

		/* TRANSLATORS: the package name that was trying to be installed */
		g_print (" %s: %s\n", _("Package"), package);

		/* TRANSLATORS: the vendor (e.g. vmware) that is providing the EULA */
		g_print (" %s: %s\n", _("Vendor"), item->vendor_name);

		/* TRANSLATORS: the EULA text itself (long and boring) */
		g_print (" %s: %s\n", _("Agreement"), item->license_agreement);

		g_free (package);
	}

	/* TRANSLATORS: ask the user if they've read and accepted the EULA */
	ret = pk_console_get_prompt (_("Do you accept this agreement?"), FALSE);
	if (ret) {
		pk_task_user_accepted (task, request);
	} else {
		/* TRANSLATORS: tell the user we've not done anything */
		g_print ("%s\n", _("The agreement was not accepted."));
		pk_task_user_declined (task, request);
	}

	g_ptr_array_unref (array);
}

/**
 * pk_task_text_media_change_question:
 **/
static void
pk_task_text_media_change_question (PkTask *task, guint request, PkResults *results)
{
	guint i;
	gboolean ret;
	GPtrArray *array;
	PkItemMediaChangeRequired *item;
	PkTaskTextPrivate *priv = PK_TASK_TEXT(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	/* clear new line */
	g_print ("\n");

	/* get data */
	array = pk_results_get_media_change_required_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		/* TRANSLATORS: the user needs to change media inserted into the computer */
		g_print ("%s\n", _("Media change required"));

		/* TRANSLATORS: the type, e.g. DVD, CD, etc */
		g_print (" %s: %s\n", _("Media type"), pk_media_type_enum_to_text (item->media_type));

		/* TRANSLATORS: the media label, usually like 'disk-1of3' */
		g_print (" %s: %s\n", _("Media label"), item->media_id);

		/* TRANSLATORS: the media description, usually like 'Fedora 12 disk 5' */
		g_print (" %s: %s\n", _("Text"), item->media_text);
	}

	/* TRANSLATORS: ask the user to insert the media */
	ret = pk_console_get_prompt (_("Please insert the correct media"), FALSE);
	if (ret) {
		pk_task_user_accepted (task, request);
	} else {
		/* TRANSLATORS: tell the user we've not done anything as they are lazy */
		g_print ("%s\n", _("The correct media was not inserted."));
		pk_task_user_declined (task, request);
	}

	g_ptr_array_unref (array);
}

/**
 * pk_task_text_simulate_question_type_to_text:
 **/
static const gchar *
pk_task_text_simulate_question_type_to_text (PkInfoEnum info)
{
	if (info == PK_INFO_ENUM_REMOVING) {
		/* TRANSLATORS: When processing, we might have to remove other dependencies */
		return _("The following packages have to be removed:");
	}

	if (info == PK_INFO_ENUM_INSTALLING) {
		/* TRANSLATORS: When processing, we might have to install other dependencies */
		return _("The following packages have to be installed:");
	}

	if (info == PK_INFO_ENUM_UPDATING) {
		/* TRANSLATORS: When processing, we might have to update other dependencies */
		return _("The following packages have to be updated:");
	}

	if (info == PK_INFO_ENUM_REINSTALLING) {
		/* TRANSLATORS: When processing, we might have to reinstall other dependencies */
		return _("The following packages have to be reinstalled:");
	}

	if (info == PK_INFO_ENUM_DOWNGRADING) {
		/* TRANSLATORS: When processing, we might have to downgrade other dependencies */
		return _("The following packages have to be downgraded:");
	}

	/* do not show */
	return NULL;
}

/**
 * pk_task_text_simulate_question:
 **/
static void
pk_task_text_simulate_question (PkTask *task, guint request, PkResults *results)
{
	guint i;
	guint len;
	gboolean ret;
	const gchar *package_id;
	const gchar *title;
	gchar *printable;
	gchar *summary;
	PkPackage *package;
	PkPackageSack *sack;
	PkInfoEnum info;
	PkInfoEnum info_last = PK_INFO_ENUM_UNKNOWN;
	PkTaskTextPrivate *priv = PK_TASK_TEXT(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	/* clear new line */
	g_print ("\n");

	/* get data */
	sack = pk_results_get_package_sack (results);

	/* print data */
	len = pk_package_sack_get_size (sack);
	for (i=0; i<len; i++) {
		package = pk_package_sack_get_index (sack, i);
		g_object_get (package,
			      "info", &info,
			      "summary", &summary,
			      NULL);
		/* new header */
		if (info != info_last) {
			title = pk_task_text_simulate_question_type_to_text (info);
			if (title == NULL) {
				title = pk_info_enum_to_text (info);
				egg_warning ("cannot translate '%s', please report!", title);
			}
			g_print ("%s\n", title);
			info_last = info;
		}
		package_id = pk_package_get_id (package);
		printable = pk_package_id_to_printable (package_id);
		g_print (" %s\t%s\n", printable, summary);

		g_free (summary);
		g_free (printable);
		g_object_unref (package);
	}

	/* TRANSLATORS: ask the user if the proposed changes are okay */
	ret = pk_console_get_prompt (_("Proceed with changes?"), FALSE);
	if (ret) {
		pk_task_user_accepted (task, request);
	} else {
		/* TRANSLATORS: tell the user we didn't do anything */
		g_print ("%s\n", _("The transaction did not proceed."));
		pk_task_user_declined (task, request);
	}

	g_object_unref (sack);
}

/**
 * pk_task_text_class_init:
 **/
static void
pk_task_text_class_init (PkTaskTextClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	PkTaskClass *task_class = PK_TASK_CLASS (klass);

	object_class->finalize = pk_task_text_finalize;
	task_class->untrusted_question = pk_task_text_untrusted_question;
	task_class->key_question = pk_task_text_key_question;
	task_class->eula_question = pk_task_text_eula_question;
	task_class->media_change_question = pk_task_text_media_change_question;
	task_class->simulate_question = pk_task_text_simulate_question;

	g_type_class_add_private (klass, sizeof (PkTaskTextPrivate));
}

/**
 * pk_task_text_init:
 * @task_text: This class instance
 **/
static void
pk_task_text_init (PkTaskText *task)
{
	task->priv = PK_TASK_TEXT_GET_PRIVATE (task);
	task->priv->user_data = NULL;
}

/**
 * pk_task_text_finalize:
 * @object: The object to finalize
 **/
static void
pk_task_text_finalize (GObject *object)
{
	PkTaskText *task = PK_TASK_TEXT (object);
	task->priv->user_data = NULL;
	G_OBJECT_CLASS (pk_task_text_parent_class)->finalize (object);
}

/**
 * pk_task_text_new:
 *
 * Return value: a new PkTaskText object.
 **/
PkTaskText *
pk_task_text_new (void)
{
	PkTaskText *task;
	task = g_object_new (PK_TYPE_TASK_TEXT, NULL);
	return PK_TASK_TEXT (task);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

#include <packagekit-glib2/pk-package-ids.h>

static void
pk_task_text_test_install_packages_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkTaskText *task = PK_TASK_TEXT (object);
	GError *error = NULL;
	PkResults *results;
	PkExitEnum exit_enum;
	GPtrArray *packages;
	const PkItemPackage *item;
	guint i;

	/* get the results */
	results = pk_task_generic_finish (PK_TASK (task), res, &error);
	if (results == NULL) {
		egg_test_failed (test, "failed to resolve: %s", error->message);
		g_error_free (error);
		goto out;
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
		egg_debug ("%s\t%s\t%s", pk_info_enum_to_text (item->info), item->package_id, item->summary);
	}

	if (packages->len != 3)
		egg_test_failed (test, "invalid number of packages: %i", packages->len);

	g_ptr_array_unref (packages);

	egg_debug ("results exit enum = %s", pk_exit_enum_to_text (exit_enum));
out:
	if (results != NULL)
		g_object_unref (results);
	egg_test_loop_quit (test);
}

static void
pk_task_text_test_progress_cb (PkProgress *progress, PkProgressType type, EggTest *test)
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
pk_task_text_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	PkTaskText *task;
	gchar **package_ids;

	if (!egg_test_start (test, "PkTaskText"))
		return;

	/************************************************************/
	egg_test_title (test, "get task_text");
	task = pk_task_text_new ();
	egg_test_assert (test, task != NULL);

	/* For testing, you will need to manually do:
	pkcon repo-set-data dummy use-gpg 1
	pkcon repo-set-data dummy use-eula 1
	pkcon repo-set-data dummy use-media 1
	*/

	/************************************************************/
	egg_test_title (test, "install package");
	package_ids = pk_package_ids_from_id ("vips-doc;7.12.4-2.fc8;noarch;linva");
	pk_task_install_packages_async (PK_TASK (task), package_ids, NULL,
				        (PkProgressCallback) pk_task_text_test_progress_cb, test,
				        (GAsyncReadyCallback) pk_task_text_test_install_packages_cb, test);
	g_strfreev (package_ids);
	egg_test_loop_wait (test, 150000);
	egg_test_success (test, "installed in %i", egg_test_elapsed (test));

	g_object_unref (task);
	egg_test_end (test);
}
#endif

