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

#include <glib/gi18n.h>
#include <packagekit-glib2/pk-eula-required.h>
#include <packagekit-glib2/pk-media-change-required.h>
#include <packagekit-glib2/pk-package.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-repo-signature-required.h>
#include <packagekit-glib2/pk-task.h>

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
	gchar *printable = NULL;
	gchar *package_id;
	gchar *repository_name;
	gchar *key_url;
	gchar *key_userid;
	gchar *key_id;
	gchar *key_fingerprint;
	gchar *key_timestamp;
	PkRepoSignatureRequired *item;
	PkTaskTextPrivate *priv = PK_TASK_TEXT(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	/* clear new line */
	g_print ("\n");

	/* get data */
	array = pk_results_get_repo_signature_required_array (results);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "package-id", &package_id,
			      "repository-name", &repository_name,
			      "key-url", &key_url,
			      "key-userid", &key_userid,
			      "key-id", &key_id,
			      "key-fingerprint", &key_fingerprint,
			      "key-timestamp", &key_timestamp,
			      NULL);

		/* create printable */
		printable = pk_package_id_to_printable (package_id);

		/* TRANSLATORS: the package repository is signed by a key that is not recognised */
		g_print ("%s\n", _("Software source signature required"));

		/* TRANSLATORS: the package that is not signed by a known key */
		g_print (" %s: %s\n", _("Package"), printable);

		/* TRANSLATORS: the package repository name */
		g_print (" %s: %s\n", _("Software source name"), repository_name);

		/* TRANSLATORS: the key URL */
		g_print (" %s: %s\n", _("Key URL"), key_url);

		/* TRANSLATORS: the username of the key */
		g_print (" %s: %s\n", _("Key user"), key_userid);

		/* TRANSLATORS: the key ID, usually a few hex digits */
		g_print (" %s: %s\n", _("Key ID"), key_id);

		/* TRANSLATORS: the key fingerprint, again, yet more hex */
		g_print (" %s: %s\n", _("Key fingerprint"), key_fingerprint);

		/* TRANSLATORS: the timestamp (a bit like a machine readable time) */
		g_print (" %s: %s\n", _("Key Timestamp"), key_timestamp);

		g_free (printable);
		g_free (package_id);
		g_free (repository_name);
		g_free (key_url);
		g_free (key_userid);
		g_free (key_id);
		g_free (key_fingerprint);
		g_free (key_timestamp);
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
	gchar *printable = NULL;
	GPtrArray *array;
	PkEulaRequired *item;
	gchar *package_id;
	gchar *vendor_name;
	gchar *license_agreement;
	PkTaskTextPrivate *priv = PK_TASK_TEXT(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	/* clear new line */
	g_print ("\n");

	/* get data */
	array = pk_results_get_eula_required_array (results);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "package-id", &package_id,
			      "vendor-name", &vendor_name,
			      "license-agreement", &license_agreement,
			      NULL);

		/* create printable */
		printable = pk_package_id_to_printable (package_id);

		/* TRANSLATORS: this is another name for a software licence that has to be read before installing */
		g_print ("%s\n", _("End user licence agreement required"));

		/* TRANSLATORS: the package name that was trying to be installed */
		g_print (" %s: %s\n", _("Package"), printable);

		/* TRANSLATORS: the vendor (e.g. vmware) that is providing the EULA */
		g_print (" %s: %s\n", _("Vendor"), vendor_name);

		/* TRANSLATORS: the EULA text itself (long and boring) */
		g_print (" %s: %s\n", _("Agreement"), license_agreement);

		g_free (printable);
		g_free (package_id);
		g_free (vendor_name);
		g_free (license_agreement);
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
	PkMediaChangeRequired *item;
	gchar *media_id;
	PkMediaTypeEnum media_type;
	gchar *media_text;
	PkTaskTextPrivate *priv = PK_TASK_TEXT(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	/* clear new line */
	g_print ("\n");

	/* get data */
	array = pk_results_get_media_change_required_array (results);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "media-id", &media_id,
			      "media-type", &media_type,
			      "media-text", &media_text,
			      NULL);

		/* TRANSLATORS: the user needs to change media inserted into the computer */
		g_print ("%s\n", _("Media change required"));

		/* TRANSLATORS: the type, e.g. DVD, CD, etc */
		g_print (" %s: %s\n", _("Media type"), pk_media_type_enum_to_string (media_type));

		/* TRANSLATORS: the media label, usually like 'disk-1of3' */
		g_print (" %s: %s\n", _("Media label"), media_id);

		/* TRANSLATORS: the media description, usually like 'Fedora 12 disk 5' */
		g_print (" %s: %s\n", _("Text"), media_text);
		g_free (media_id);
		g_free (media_text);
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
 * pk_task_text_simulate_question_type_to_string:
 **/
static const gchar *
pk_task_text_simulate_question_type_to_string (PkInfoEnum info)
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

	if (info == PK_INFO_ENUM_UNTRUSTED) {
		/* TRANSLATORS: When processing, untrusted and non-verified packages may be encountered */
		return _("The following packages are untrusted:");
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
	gboolean ret;
	const gchar *title;
	gchar *printable;
	PkInfoEnum info;
	gchar *package_id;
	gchar *summary;
	PkPackage *package;
	GPtrArray *array;
	PkInfoEnum info_last = PK_INFO_ENUM_UNKNOWN;
	PkTaskTextPrivate *priv = PK_TASK_TEXT(task)->priv;

	/* set some user data, for no reason */
	priv->user_data = NULL;

	/* clear new line */
	g_print ("\n");

	/* get data */
	array = pk_results_get_package_array (results);

	/* print data */
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_object_get (package,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);
		/* new header */
		if (info != info_last) {
			title = pk_task_text_simulate_question_type_to_string (info);
			if (title == NULL) {
				title = pk_info_enum_to_string (info);
				g_warning ("cannot translate '%s', please report!", title);
			}
			g_print ("%s\n", title);
			info_last = info;
		}
		printable = pk_package_id_to_printable (package_id);
		g_print (" %s\t%s\n", printable, summary);
		g_free (printable);
		g_free (package_id);
		g_free (summary);
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

	g_ptr_array_unref (array);
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
