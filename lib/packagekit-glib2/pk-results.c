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

/**
 * SECTION:pk-results
 * @short_description: TODO
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <dbus/dbus-glib.h>

#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>

#include "egg-debug.h"

static void     pk_results_finalize	(GObject     *object);

#define PK_RESULTS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_RESULTS, PkResultsPrivate))

/**
 * PkResultsPrivate:
 *
 * Private #PkResults data
 **/
struct _PkResultsPrivate
{
	PkExitEnum		 exit_enum;
	GPtrArray		*package_array;
	GPtrArray		*details_array;
	GPtrArray		*update_detail_array;
};

G_DEFINE_TYPE (PkResults, pk_results, G_TYPE_OBJECT)

/**
 * pk_result_item_package_free:
 **/
static void
pk_result_item_package_free (PkResultItemPackage *item)
{
	if (item == NULL)
		return;
	g_free (item->package_id);
	g_free (item->summary);
	g_free (item);
}

/**
 * pk_result_item_details_free:
 **/
static void
pk_result_item_details_free (PkResultItemDetails *item)
{
	if (item == NULL)
		return;
	g_free (item->package_id);
	g_free (item->license);
	g_free (item->description);
	g_free (item->url);
	g_free (item);
}

/**
 * pk_result_item_update_detail_free:
 **/
static void
pk_result_item_update_detail_free (PkResultItemUpdateDetail *item)
{
	if (item == NULL)
		return;
	g_free (item->package_id);
	g_free (item->updates);
	g_free (item->obsoletes);
	g_free (item->vendor_url);
	g_free (item->bugzilla_url);
	g_free (item->cve_url);
	g_free (item->update_text);
	g_free (item->changelog);
	if (item->issued != NULL)
		g_date_free (item->issued);
	if (item->updated != NULL)
		g_date_free (item->updated);
}

/**
 * pk_results_set_exit_code:
 * @results: a valid #PkResults instance
 * @exit_enum: the exit code
 *
 * Sets the results object to have the given exit code.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_set_exit_code (PkResults *results, PkExitEnum exit_enum)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (exit_enum != PK_EXIT_ENUM_UNKNOWN, FALSE);

	results->priv->exit_enum = exit_enum;

	return TRUE;
}

/**
 * pk_results_add_package:
 * @results: a valid #PkResults instance
 *
 * Adds a package to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_package (PkResults *results, PkInfoEnum info_enum, const gchar *package_id, const gchar *summary)
{
	PkResultItemPackage *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (info_enum != PK_INFO_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemPackage, 1);
	item->info_enum = info_enum;
	item->package_id = g_strdup (package_id);
	item->summary = g_strdup (summary);
	g_ptr_array_add (results->priv->package_array, item);

	return TRUE;
}

/**
 * pk_results_add_details:
 * @results: a valid #PkResults instance
 *
 * Adds some package details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_details (PkResults *results, const gchar	*package_id, const gchar *license,
			PkGroupEnum group_enum, const gchar *description, const gchar *url, guint64 size)
{
	PkResultItemDetails *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemDetails, 1);
	item->package_id = g_strdup (package_id);
	item->license = g_strdup (license);
	item->group_enum = group_enum;
	item->description = g_strdup (description);
	item->url = g_strdup (url);
	item->size = size;
	g_ptr_array_add (results->priv->details_array, item);

	return TRUE;
}

/**
 * pk_results_add_update_detail:
 * @results: a valid #PkResults instance
 *
 * Adds some update details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_update_detail (PkResults *results, const gchar *package_id, const gchar *updates,
			      const gchar *obsoletes, const gchar *vendor_url, const gchar *bugzilla_url,
			      const gchar *cve_url, PkRestartEnum restart_enum, const gchar *update_text,
			      const gchar *changelog, PkUpdateStateEnum state_enum, GDate *issued, GDate *updated)
{
	PkResultItemUpdateDetail *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemUpdateDetail, 1);
	item->package_id = g_strdup (package_id);
	item->updates = g_strdup (updates);
	item->obsoletes = g_strdup (obsoletes);
	item->vendor_url = g_strdup (vendor_url);
	item->bugzilla_url = g_strdup (bugzilla_url);
	item->cve_url = g_strdup (cve_url);
	item->restart_enum = restart_enum;
	item->update_text = g_strdup (update_text);
	item->changelog = g_strdup (changelog);
	item->state_enum = state_enum;
	if (issued != NULL)
		item->issued = g_date_new_dmy (issued->day, issued->month, issued->year);
	if (updated != NULL)
		item->updated = g_date_new_dmy (updated->day, updated->month, updated->year);
	g_ptr_array_add (results->priv->update_detail_array, item);

	return TRUE;
}

/**
 * pk_results_get_exit_code:
 * @results: a valid #PkResults instance
 *
 * Gets the exit enum.
 *
 * Return value: The #PkExitEnum or %PK_EXIT_ENUM_UNKNOWN for error or if it was not set
 **/
PkExitEnum
pk_results_get_exit_code (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), PK_EXIT_ENUM_UNKNOWN);
	return results->priv->exit_enum;
}

/**
 * pk_results_get_package_array:
 * @results: a valid #PkResults instance
 *
 * Gets the packages from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemDetails's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_package_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	return g_ptr_array_ref (results->priv->package_array);
}

/**
 * pk_results_get_package_sack:
 * @results: a valid #PkResults instance
 *
 * Gets a package sack from the transaction.
 *
 * Return value: A #PkPackageSack of data.
 **/
PkPackageSack *
pk_results_get_package_sack (PkResults *results)
{
	PkPackage *package;
	PkPackageSack *sack;
	GPtrArray *array;
	guint i;
	const PkResultItemPackage *item;
	gboolean ret;

	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);

	/* create a new sack */
	sack = pk_package_sack_new ();

	/* go through each of the bare packages */
	array = results->priv->package_array;
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* create a PkPackage object */
		package = pk_package_new ();
		ret = pk_package_set_id (package, item->package_id, NULL);
		if (!ret)
			egg_error ("couldn't add package ID, internal error");

		/* set data we already know */
		g_object_set (package,
			      "info", item->info_enum,
			      "summary", item->summary,
			      NULL);

		/* add to sack */
		pk_package_sack_add_package (sack, package);
		g_object_unref (package);
	}

	return sack;
}

/**
 * pk_results_get_details_array:
 * @results: a valid #PkResults instance
 *
 * Gets the package details from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemPackage's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_details_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	return g_ptr_array_ref (results->priv->details_array);
}

/**
 * pk_results_get_update_detail_array:
 * @results: a valid #PkResults instance
 *
 * Gets the update details from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemUpdateDetail's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_update_detail_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	return g_ptr_array_ref (results->priv->update_detail_array);
}

/**
 * pk_results_class_init:
 * @klass: The PkResultsClass
 **/
static void
pk_results_class_init (PkResultsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_results_finalize;
	g_type_class_add_private (klass, sizeof (PkResultsPrivate));
}

/**
 * pk_results_init:
 * @results: This class instance
 **/
static void
pk_results_init (PkResults *results)
{
	results->priv = PK_RESULTS_GET_PRIVATE (results);
	results->priv->exit_enum = PK_EXIT_ENUM_UNKNOWN;
	results->priv->package_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_package_free);
	results->priv->details_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_details_free);
	results->priv->update_detail_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_update_detail_free);
}

/**
 * pk_results_finalize:
 * @object: The object to finalize
 **/
static void
pk_results_finalize (GObject *object)
{
	PkResults *results = PK_RESULTS (object);
	PkResultsPrivate *priv = results->priv;

	g_ptr_array_unref (priv->package_array);
	g_ptr_array_unref (priv->details_array);
	g_ptr_array_unref (priv->update_detail_array);

	G_OBJECT_CLASS (pk_results_parent_class)->finalize (object);
}

/**
 * pk_results_new:
 *
 * Return value: a new PkResults object.
 **/
PkResults *
pk_results_new (void)
{
	PkResults *results;
	results = g_object_new (PK_TYPE_RESULTS, NULL);
	return PK_RESULTS (results);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_results_test (EggTest *test)
{
	gboolean ret;
	PkResults *results;
	PkExitEnum exit_enum;
	GPtrArray *packages;

	if (!egg_test_start (test, "PkResults"))
		return;

	/************************************************************/
	egg_test_title (test, "get results");
	results = pk_results_new ();
	egg_test_assert (test, results != NULL);

	/************************************************************/
	egg_test_title (test, "get exit code of unset results");
	exit_enum = pk_results_get_exit_code (results);
	egg_test_assert (test, (exit_enum == PK_EXIT_ENUM_UNKNOWN));

	/************************************************************/
	egg_test_title (test, "get package list of unset results");
	packages = pk_results_get_package_array (results);
	egg_test_assert (test, (packages->len == 0));
	g_ptr_array_unref (packages);

	/************************************************************/
	egg_test_title (test, "set valid exit code");
	ret = pk_results_set_exit_code (results, PK_EXIT_ENUM_CANCELLED);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get exit code of set results");
	exit_enum = pk_results_get_exit_code (results);
	egg_test_assert (test, (exit_enum == PK_EXIT_ENUM_CANCELLED));

	/************************************************************/
	egg_test_title (test, "add package");
	ret = pk_results_add_package (results, PK_INFO_ENUM_AVAILABLE, "gnome-power-manager;0.1.2;i386;fedora", "Power manager for GNOME");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get package list of set results");
	packages = pk_results_get_package_array (results);
	egg_test_assert (test, (packages->len == 1));
	g_ptr_array_unref (packages);

	g_object_unref (results);
out:
	egg_test_end (test);
}
#endif

