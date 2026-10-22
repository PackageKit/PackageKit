/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <glib/gi18n.h>
#include "pk-eula-required.h"
#include "pk-package.h"
#include "pk-package-id.h"
#include "pk-repo-signature-required.h"
#include "pk-task.h"

#include "pk-task-text.h"
#include "pk-console-private.h"

struct _PkTaskText
{
	PkTask parent;
};

G_DEFINE_TYPE (PkTaskText, pk_task_text, PK_TYPE_TASK)

/*
 * pk_task_text_untrusted_question:
 **/
static void
pk_task_text_untrusted_question (PkTask *task, guint request, PkResults *results)
{
	gboolean ret;

	/* clear new line */
	g_print ("\n");

	/* TRANSLATORS: ask the user if they are comfortable installing insecure packages */
	ret = pk_console_get_prompt (_("Do you want to allow installing of unsigned software?"),
				       FALSE);
	if (ret) {
		pk_task_user_accepted (task, request);
	} else {
		/* TRANSLATORS: tell the user we've not done anything */
		g_print ("%s\n", _("The unsigned software will not be installed."));
		pk_task_user_declined (task, request);
	}
}

/*
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

	/* clear new line */
	g_print ("\n");

	/* get data */
	array = pk_results_get_repo_signature_required_array (results);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "package-id",
			      &package_id,
			      "repository-name",
			      &repository_name,
			      "key-url",
			      &key_url,
			      "key-userid",
			      &key_userid,
			      "key-id",
			      &key_id,
			      "key-fingerprint",
			      &key_fingerprint,
			      "key-timestamp",
			      &key_timestamp,
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

/*
 * pk_task_text_eula_question:
 **/
static void
pk_task_text_eula_question (PkTask *task, guint request, PkResults *results)
{
	guint i;
	gboolean ret;
	GPtrArray *array;

	/* clear new line */
	g_print ("\n");

	/* get data */
	array = pk_results_get_eula_required_array (results);
	for (i = 0; i < array->len; i++) {
		PkEulaRequired *item;
		g_autofree gchar *printable = NULL;

		item = g_ptr_array_index (array, i);

		/* create printable */
		printable = pk_package_id_to_printable (pk_eula_required_get_package_id (item));

		/* TRANSLATORS: this is another name for a software licence that has to be read before installing */
		g_print ("%s\n", _("End user licence agreement required"));

		/* TRANSLATORS: the package name that was trying to be installed */
		g_print (" %s: %s\n", _("Package"), printable);

		/* TRANSLATORS: the vendor (e.g. vmware) that is providing the EULA */
		g_print (" %s: %s\n", _("Vendor"), pk_eula_required_get_vendor_name (item));

		g_print (" %s: %s\n",
			 /* TRANSLATORS: the EULA text itself (long and boring) */
			 _("Agreement"), pk_eula_required_get_license_agreement (item));
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

/*
 * pk_task_text_simulate_question_type_to_string:
 **/
static const gchar *
pk_task_text_simulate_question_type_to_string (PkInfoEnum info, gboolean purge)
{
	if (info == PK_INFO_ENUM_REMOVE) {
		if (purge) {
			/* TRANSLATORS: When processing with the purge flag, removed packages lose their configuration files and data too */
			return _("The following packages have to be removed, including their configuration files and data:");
		}
		/* TRANSLATORS: When processing, we might have to remove other dependencies */
		return _("The following packages have to be removed:");
	}

	if (info == PK_INFO_ENUM_INSTALL) {
		/* TRANSLATORS: When processing, we might have to install other dependencies */
		return _("The following packages have to be installed:");
	}

	if (info == PK_INFO_ENUM_UPDATE || info == PK_INFO_ENUM_UPDATE_SECURITY ||
	    info == PK_INFO_ENUM_UPDATE_ENHANCEMENT) {
		/* TRANSLATORS: When processing, we might have to update other dependencies */
		return _("The following packages have to be updated:");
	}

	if (info == PK_INFO_ENUM_REINSTALL) {
		/* TRANSLATORS: When processing, we might have to reinstall other dependencies */
		return _("The following packages have to be reinstalled:");
	}

	if (info == PK_INFO_ENUM_DOWNGRADE) {
		/* TRANSLATORS: When processing, we might have to downgrade other dependencies */
		return _("The following packages have to be downgraded:");
	}

	if (info == PK_INFO_ENUM_OBSOLETE) {
		if (purge) {
			/* TRANSLATORS: When processing with the purge flag, obsoleted packages lose their configuration files and data too */
			return _("The following packages have to be obsoleted, including their configuration files and data:");
		}
		/* TRANSLATORS: When processing, we might have to obsolete other dependencies */
		return _("The following packages have to be obsoleted:");
	}

	if (info == PK_INFO_ENUM_UNTRUSTED) {
		/* TRANSLATORS: When processing, untrusted and non-verified packages may be encountered */
		return _("The following packages are untrusted:");
	}

	/* do not show */
	return NULL;
}

/*
 * pk_task_text_simulate_question_section:
 *
 * Map package states to the section they are listed in, so all kinds
 * of updates end up in one section.
 **/
static PkInfoEnum
pk_task_text_simulate_question_section (PkInfoEnum info)
{
	if (info == PK_INFO_ENUM_UPDATE_SECURITY || info == PK_INFO_ENUM_UPDATE_ENHANCEMENT)
		return PK_INFO_ENUM_UPDATE;
	return info;
}

/*
 * pk_task_text_simulate_question_count:
 **/
static guint
pk_task_text_simulate_question_count (GHashTable *table, PkInfoEnum info)
{
	GPtrArray *array = g_hash_table_lookup (table, GINT_TO_POINTER (info));
	return array != NULL ? array->len : 0;
}

/*
 * pk_task_text_simulate_question_summary:
 **/
static gchar *
pk_task_text_simulate_question_summary (GHashTable *table)
{
	guint n_install = pk_task_text_simulate_question_count (table, PK_INFO_ENUM_INSTALL);
	guint n_update = pk_task_text_simulate_question_count (table, PK_INFO_ENUM_UPDATE);
	guint n_remove = pk_task_text_simulate_question_count (table, PK_INFO_ENUM_REMOVE) +
			 pk_task_text_simulate_question_count (table, PK_INFO_ENUM_OBSOLETE);
	guint n_downgrade = pk_task_text_simulate_question_count (table, PK_INFO_ENUM_DOWNGRADE);
	guint n_reinstall = pk_task_text_simulate_question_count (table, PK_INFO_ENUM_REINSTALL);
	g_autoptr(GPtrArray) parts = g_ptr_array_new_with_free_func (g_free);

	g_ptr_array_add (
	    parts,
	    /* TRANSLATORS: part of the transaction summary, e.g. "2 to install, 1 to update, 0 to remove" */
	    g_strdup_printf (ngettext ("%u to install", "%u to install", n_install), n_install));
	g_ptr_array_add (
	    parts,
	    /* TRANSLATORS: part of the transaction summary, e.g. "2 to install, 1 to update, 0 to remove" */
	    g_strdup_printf (ngettext ("%u to update", "%u to update", n_update), n_update));
	g_ptr_array_add (
	    parts,
	    /* TRANSLATORS: part of the transaction summary, e.g. "2 to install, 1 to update, 0 to remove" */
	    g_strdup_printf (ngettext ("%u to remove", "%u to remove", n_remove), n_remove));
	if (n_downgrade > 0) {
		g_ptr_array_add (
		    parts,
		    /* TRANSLATORS: part of the transaction summary, only shown if packages are downgraded */
		    g_strdup_printf (ngettext ("%u to downgrade", "%u to downgrade", n_downgrade),
				     n_downgrade));
	}
	if (n_reinstall > 0) {
		g_ptr_array_add (
		    parts,
		    /* TRANSLATORS: part of the transaction summary, only shown if packages are reinstalled */
		    g_strdup_printf (ngettext ("%u to reinstall", "%u to reinstall", n_reinstall),
				     n_reinstall));
	}
	g_ptr_array_add (parts, NULL);

	/* TRANSLATORS: separator between the parts of the transaction summary */
	return g_strjoinv (_(", "), (gchar **) parts->pdata);
}

/**
 * pk_task_text_simulate_question:
 */
static void
pk_task_text_simulate_question (PkTask *task, guint request, PkResults *results)
{
	gboolean ret;
	guint width;
	g_autofree gchar *summary_line = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GHashTable) table = NULL;
	g_autoptr(GList) list = NULL;
	g_autoptr(GArray) sections = NULL;
	/* clang-format off */
	const PkInfoEnum section_order[] = {
		PK_INFO_ENUM_REMOVE,
		PK_INFO_ENUM_OBSOLETE,
		PK_INFO_ENUM_DOWNGRADE,
		PK_INFO_ENUM_REINSTALL,
		PK_INFO_ENUM_UPDATE,
		PK_INFO_ENUM_INSTALL,
		PK_INFO_ENUM_UNTRUSTED,
	};
	/* clang-format on */

	/* clear new line */
	g_print ("\n");

	/* get data */
	array = pk_results_get_package_array (results);

	/* group the packages by what happens to them */
	table = g_hash_table_new_full (g_direct_hash,
				       g_direct_equal,
				       NULL,
				       (GDestroyNotify) g_ptr_array_unref);
	for (guint i = 0; i < array->len; i++) {
		PkPackage *package = g_ptr_array_index (array, i);
		PkInfoEnum info = pk_task_text_simulate_question_section (
		    pk_package_get_info (package));
		GPtrArray *package_array = g_hash_table_lookup (table, GINT_TO_POINTER (info));

		if (package_array == NULL) {
			package_array = g_ptr_array_new_with_free_func (g_object_unref);
			g_hash_table_insert (table, GINT_TO_POINTER (info), package_array);
		}
		g_ptr_array_add (package_array, g_object_ref (package));
	}

	/* show the sections in a fixed order, unknown ones last */
	sections = g_array_new (FALSE, FALSE, sizeof (PkInfoEnum));
	for (guint i = 0; i < G_N_ELEMENTS (section_order); i++) {
		if (g_hash_table_contains (table, GINT_TO_POINTER (section_order[i])))
			g_array_append_val (sections, section_order[i]);
	}
	list = g_hash_table_get_keys (table);
	for (GList *l = list; l != NULL; l = l->next) {
		PkInfoEnum info = GPOINTER_TO_INT (l->data);
		gboolean known = FALSE;

		for (guint i = 0; i < G_N_ELEMENTS (section_order); i++)
			known = known || section_order[i] == info;
		if (!known)
			g_array_append_val (sections, info);
	}

	width = pk_console_get_width ();
	for (guint s = 0; s < sections->len; s++) {
		PkInfoEnum info = g_array_index (sections, PkInfoEnum, s);
		GPtrArray *package_array = g_hash_table_lookup (table, GINT_TO_POINTER (info));
		g_autoptr(PkConsoleTable) pkg_table = pk_console_table_new (5);
		g_autofree gchar *rendered = NULL;
		const gchar *title;

		/* new header */
		title = pk_task_text_simulate_question_type_to_string (info,
								       pk_task_get_purge (task));
		if (title == NULL) {
			title = pk_info_enum_to_string (info);
			g_warning ("cannot translate '%s', please report!", title);
		}
		g_print ("%s\n", title);

		/* sort the packages and print them as a table */
		g_ptr_array_sort (package_array, pk_console_package_sort_func);
		pk_console_table_set_indent (pkg_table, 2);
		pk_console_table_set_max_width (pkg_table, 0, PK_CONSOLE_NAME_WIDTH_MAX);
		pk_console_table_set_max_width (pkg_table, 1, PK_CONSOLE_VERSION_WIDTH_MAX);
		pk_console_table_set_max_width (pkg_table, 2, PK_CONSOLE_ARCH_WIDTH_MAX);
		pk_console_table_set_max_width (pkg_table, 3, PK_CONSOLE_ORIGIN_WIDTH_MAX);
		pk_console_table_set_flexible (pkg_table, 4);
		for (guint i = 0; i < package_array->len; i++) {
			PkPackage *package = g_ptr_array_index (package_array, i);

			pk_console_table_add_row (pkg_table,
						  pk_package_get_name (package),
						  pk_package_get_version (package),
						  pk_package_get_arch (package),
						  pk_package_get_origin (package),
						  pk_package_get_summary (package));
		}
		rendered = pk_console_table_render (pkg_table, width, FALSE);
		g_print ("%s\n", rendered);
	}

	summary_line = pk_task_text_simulate_question_summary (table);
	g_print ("%s\n", summary_line);

	/* TRANSLATORS: ask the user if the proposed changes are okay */
	ret = pk_console_get_prompt (_("Proceed with changes?"), FALSE);
	if (ret) {
		pk_task_user_accepted (task, request);
	} else {
		/* TRANSLATORS: tell the user we didn't do anything */
		g_print ("%s\n", _("The transaction did not proceed."));
		pk_task_user_declined (task, request);
	}
}

/*
 * pk_task_text_class_init:
 **/
static void
pk_task_text_class_init (PkTaskTextClass *klass)
{
	PkTaskClass *task_class = PK_TASK_CLASS (klass);

	task_class->untrusted_question = pk_task_text_untrusted_question;
	task_class->key_question = pk_task_text_key_question;
	task_class->eula_question = pk_task_text_eula_question;
	task_class->simulate_question = pk_task_text_simulate_question;
}

/*
 * pk_task_text_init:
 * @task_text: This class instance
 **/
static void
pk_task_text_init (PkTaskText *task)
{}

/**
 * pk_task_text_new:
 *
 * Return value: a new #PkTaskText object.
 **/
PkTaskText *
pk_task_text_new (void)
{
	PkTaskText *task;
	task = g_object_new (PK_TYPE_TASK_TEXT, NULL);
	return PK_TASK_TEXT (task);
}
