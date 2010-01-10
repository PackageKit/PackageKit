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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:pk-results
 * @short_description: Transaction results
 *
 * This GObject allows a client program to query the results sent from
 * PackageKit. This will include Package(), ErrorCode() and all the other types
 * of objects. Everything is refcounted, so ensure you unref when done with the
 * data.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-results.h>
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
	PkRoleEnum		 role;
	guint			 inputs;
	PkProgress		*progress;
	PkExitEnum		 exit_enum;
	PkError			*error_code;
	GPtrArray		*package_array;
	GPtrArray		*details_array;
	GPtrArray		*update_detail_array;
	GPtrArray		*category_array;
	GPtrArray		*distro_upgrade_array;
	GPtrArray		*require_restart_array;
	GPtrArray		*transaction_array;
	GPtrArray		*files_array;
	GPtrArray		*repo_signature_required_array;
	GPtrArray		*eula_required_array;
	GPtrArray		*media_change_required_array;
	GPtrArray		*repo_detail_array;
	GPtrArray		*message_array;
};

enum {
	PROP_0,
	PROP_ROLE,
	PROP_INPUTS,
	PROP_PROGRESS,
	PROP_LAST
};

G_DEFINE_TYPE (PkResults, pk_results, G_TYPE_OBJECT)

/**
 * pk_results_get_property:
 **/
static void
pk_results_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkResults *results = PK_RESULTS (object);
	PkResultsPrivate *priv = results->priv;

	switch (prop_id) {
	case PROP_ROLE:
		g_value_set_uint (value, priv->role);
		break;
	case PROP_INPUTS:
		g_value_set_uint (value, priv->inputs);
		break;
	case PROP_PROGRESS:
		g_value_set_object (value, priv->progress);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_results_set_property:
 **/
static void
pk_results_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkResults *results = PK_RESULTS (object);
	PkResultsPrivate *priv = results->priv;

	switch (prop_id) {
	case PROP_ROLE:
		priv->role = g_value_get_uint (value);
		break;
	case PROP_INPUTS:
		priv->inputs = g_value_get_uint (value);
		break;
	case PROP_PROGRESS:
		if (priv->progress != NULL)
			g_object_unref (priv->progress);
		priv->progress = g_object_ref (g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
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
 * @item: the object to add to the array
 *
 * Adds a package to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_package (PkResults *results, PkPackage *item)
{
	PkInfoEnum info;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* do not allow finished types */
	g_object_get (item,
		      "info", &info,
		      NULL);
	if (info == PK_INFO_ENUM_FINISHED) {
		egg_warning ("internal error: finished packages cannot be added to a PkResults object");
		return FALSE;
	}

	/* copy and add to array */
	g_ptr_array_add (results->priv->package_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_add_details:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds some package details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_details (PkResults *results, PkDetails *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* copy and add to array */
	g_ptr_array_add (results->priv->details_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_add_update_detail:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds some update details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_update_detail (PkResults *results, PkUpdateDetail *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* copy and add to array */
	g_ptr_array_add (results->priv->update_detail_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_add_category:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds a category item to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_category (PkResults *results, PkCategory *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* copy and add to array */
	g_ptr_array_add (results->priv->category_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_add_distro_upgrade:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds a distribution upgrade item to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_distro_upgrade (PkResults *results, PkDistroUpgrade *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* copy and add to array */
	g_ptr_array_add (results->priv->distro_upgrade_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_add_require_restart:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds a require restart item to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_require_restart (PkResults *results, PkRequireRestart *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* copy and add to array */
	g_ptr_array_add (results->priv->require_restart_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_add_transaction:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds a transaction item to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_transaction (PkResults *results, PkTransactionPast *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* copy and add to array */
	g_ptr_array_add (results->priv->transaction_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_add_files:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds some files details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_files (PkResults *results, PkFiles *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* copy and add to array */
	g_ptr_array_add (results->priv->files_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_add_repo_signature_required:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds some repository signature details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_repo_signature_required (PkResults *results, PkRepoSignatureRequired *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* copy and add to array */
	g_ptr_array_add (results->priv->repo_signature_required_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_add_eula_required:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds some EULA details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_eula_required (PkResults *results, PkEulaRequired *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* copy and add to array */
	g_ptr_array_add (results->priv->eula_required_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_add_media_change_required:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds some media change details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_media_change_required (PkResults *results, PkMediaChangeRequired *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* copy and add to array */
	g_ptr_array_add (results->priv->media_change_required_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_add_repo_detail:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds some repository details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_repo_detail (PkResults *results, PkRepoDetail *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* copy and add to array */
	g_ptr_array_add (results->priv->repo_detail_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_set_error_code:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds some error details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_set_error_code (PkResults *results, PkError *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* unref old error, then ref */
	if (results->priv->error_code != NULL)
		g_object_unref (results->priv->error_code);
	results->priv->error_code = g_object_ref (item);

	return TRUE;
}

/**
 * pk_results_add_message:
 * @results: a valid #PkResults instance
 * @item: the object to add to the array
 *
 * Adds some message details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_message (PkResults *results, PkMessage *item)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* copy and add to array */
	g_ptr_array_add (results->priv->message_array, g_object_ref (item));

	return TRUE;
}

/**
 * pk_results_get_exit_code:
 * @results: a valid #PkResults instance
 *
 * Gets the exit enum. You probably don't want to be using this function, and
 * instead using the much more useful pk_results_get_error_code() function.
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
 * pk_results_get_error_code:
 * @results: a valid #PkResults instance
 *
 * Gets the last error code from the transaction.
 *
 * Return value: A #PkError, or %NULL, free with g_object_unref()
 **/
PkError *
pk_results_get_error_code (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);

	/* failed, but no exit code? */
	if (results->priv->error_code == NULL && results->priv->exit_enum != PK_EXIT_ENUM_SUCCESS)
		egg_warning ("internal error: failed, but no exit code: %s", pk_exit_enum_to_text (results->priv->exit_enum));

	if (results->priv->error_code == NULL)
		return NULL;
	return g_object_ref (results->priv->error_code);
}

/**
 * pk_results_get_package_array:
 * @results: a valid #PkResults instance
 *
 * Gets the packages from the transaction.
 *
 * Return value: A #GPtrArray array of #PkDetails's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_package_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
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

	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);

	/* create a new sack */
	sack = pk_package_sack_new ();

	/* go through each of the bare packages */
	array = results->priv->package_array;
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		pk_package_sack_add_package (sack, g_object_ref (package));
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
 * Return value: A #GPtrArray array of #PkPackage's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_details_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_ptr_array_ref (results->priv->details_array);
}

/**
 * pk_results_get_update_detail_array:
 * @results: a valid #PkResults instance
 *
 * Gets the update details from the transaction.
 *
 * Return value: A #GPtrArray array of #PkUpdateDetail's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_update_detail_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_ptr_array_ref (results->priv->update_detail_array);
}

/**
 * pk_results_get_category_array:
 * @results: a valid #PkResults instance
 *
 * Gets the categories from the transaction.
 *
 * Return value: A #GPtrArray array of #PkCategory's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_category_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_ptr_array_ref (results->priv->category_array);
}

/**
 * pk_results_get_distro_upgrade_array:
 * @results: a valid #PkResults instance
 *
 * Gets the distribution upgrades from the transaction.
 *
 * Return value: A #GPtrArray array of #PkDistroUpgrade's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_distro_upgrade_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_ptr_array_ref (results->priv->distro_upgrade_array);
}

/**
 * pk_results_get_require_restart_array:
 * @results: a valid #PkResults instance
 *
 * Gets the require restarts from the transaction.
 *
 * Return value: A #GPtrArray array of #PkRequireRestart's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_require_restart_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_ptr_array_ref (results->priv->require_restart_array);
}

/**
 * pk_results_get_require_restart_worst:
 * @results: a valid #PkResults instance
 *
 * This method returns the 'worst' restart of all the transactions.
 * It is needed as multiple sub-transactions may emit require-restart with
 * different values, and we always want to get the most invasive of all.
 *
 * For instance, if a transaction emits RequireRestart(system) and then
 * RequireRestart(session) then pk_client_get_require_restart will return
 * system as a session restart is implied with a system restart.
 *
 * Return value: a #PkRestartEnum value, e.g. PK_RESTART_ENUM_SYSTEM
 **/
PkRestartEnum
pk_results_get_require_restart_worst (PkResults *results)
{
	GPtrArray *array;
	PkRestartEnum worst = 0;
	PkRestartEnum restart;
	guint i;
	PkRequireRestart *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), 0);

	array = results->priv->require_restart_array;
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "restart", &restart,
			      NULL);
		if (restart > worst)
			worst = restart;
	}

	return worst;
}

/**
 * pk_results_get_transaction_array:
 * @results: a valid #PkResults instance
 *
 * Gets the transactions from the transaction.
 *
 * Return value: A #GPtrArray array of #PkTransactionPast's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_transaction_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_ptr_array_ref (results->priv->transaction_array);
}

/**
 * pk_results_get_files_array:
 * @results: a valid #PkResults instance
 *
 * Gets the files from the transaction.
 *
 * Return value: A #GPtrArray array of #PkFiles's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_files_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_ptr_array_ref (results->priv->files_array);
}

/**
 * pk_results_get_repo_signature_required_array:
 * @results: a valid #PkResults instance
 *
 * Gets the repository signatures required from the transaction.
 *
 * Return value: A #GPtrArray array of #PkRepoSignatureRequired's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_repo_signature_required_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_ptr_array_ref (results->priv->repo_signature_required_array);
}

/**
 * pk_results_get_eula_required_array:
 * @results: a valid #PkResults instance
 *
 * Gets the eulas required from the transaction.
 *
 * Return value: A #GPtrArray array of #PkEulaRequired's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_eula_required_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_ptr_array_ref (results->priv->eula_required_array);
}

/**
 * pk_results_get_media_change_required_array:
 * @results: a valid #PkResults instance
 *
 * Gets the media changes required from the transaction.
 *
 * Return value: A #GPtrArray array of #PkMediaChangeRequired's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_media_change_required_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_ptr_array_ref (results->priv->media_change_required_array);
}

/**
 * pk_results_get_repo_detail_array:
 * @results: a valid #PkResults instance
 *
 * Gets the repository details from the transaction.
 *
 * Return value: A #GPtrArray array of #PkRepoDetail's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_repo_detail_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_ptr_array_ref (results->priv->repo_detail_array);
}

/**
 * pk_results_get_message_array:
 * @results: a valid #PkResults instance
 *
 * Gets the messages from the transaction.
 *
 * Return value: A #GPtrArray array of #PkMessage's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_message_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_ptr_array_ref (results->priv->message_array);
}

/**
 * pk_results_class_init:
 **/
static void
pk_results_class_init (PkResultsClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_results_finalize;
	object_class->get_property = pk_results_get_property;
	object_class->set_property = pk_results_set_property;

	/**
	 * PkResults:role:
	 */
	pspec = g_param_spec_uint ("role", NULL, NULL,
				   0, PK_ROLE_ENUM_LAST, PK_ROLE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ROLE, pspec);

	/**
	 * PkResults:inputs:
	 */
	pspec = g_param_spec_uint ("inputs", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_INPUTS, pspec);

	g_type_class_add_private (klass, sizeof (PkResultsPrivate));

	/**
	 * PkResults:progress:
	 */
	pspec = g_param_spec_object ("progress", NULL,
				     "The progress instance",
				     PK_TYPE_PROGRESS,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PROGRESS, pspec);
}

/**
 * pk_results_init:
 **/
static void
pk_results_init (PkResults *results)
{
	results->priv = PK_RESULTS_GET_PRIVATE (results);
	results->priv->role = PK_ROLE_ENUM_UNKNOWN;
	results->priv->exit_enum = PK_EXIT_ENUM_UNKNOWN;
	results->priv->inputs = 0;
	results->priv->progress = NULL;
	results->priv->error_code = NULL;
	results->priv->package_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	results->priv->details_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	results->priv->update_detail_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	results->priv->category_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	results->priv->distro_upgrade_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	results->priv->require_restart_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	results->priv->transaction_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	results->priv->files_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	results->priv->repo_signature_required_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	results->priv->eula_required_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	results->priv->media_change_required_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	results->priv->repo_detail_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	results->priv->message_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * pk_results_finalize:
 **/
static void
pk_results_finalize (GObject *object)
{
	PkResults *results = PK_RESULTS (object);
	PkResultsPrivate *priv = results->priv;

	g_ptr_array_unref (priv->package_array);
	g_ptr_array_unref (priv->details_array);
	g_ptr_array_unref (priv->update_detail_array);
	g_ptr_array_unref (priv->category_array);
	g_ptr_array_unref (priv->distro_upgrade_array);
	g_ptr_array_unref (priv->require_restart_array);
	g_ptr_array_unref (priv->transaction_array);
	g_ptr_array_unref (priv->files_array);
	g_ptr_array_unref (priv->repo_signature_required_array);
	g_ptr_array_unref (priv->eula_required_array);
	g_ptr_array_unref (priv->media_change_required_array);
	g_ptr_array_unref (priv->repo_detail_array);
	g_ptr_array_unref (priv->message_array);
	if (results->priv->progress != NULL)
		g_object_unref (results->priv->progress);
	if (results->priv->error_code != NULL)
		g_object_unref (results->priv->error_code);

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
pk_results_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	gboolean ret;
	PkResults *results;
	PkExitEnum exit_enum;
	GPtrArray *packages;
	PkPackage *item;
	PkInfoEnum info;
	gchar *package_id;
	gchar *summary;

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
	item = pk_package_new ();
	g_object_set (item,
		      "info", PK_INFO_ENUM_AVAILABLE,
		      "package-id", "gnome-power-manager;0.1.2;i386;fedora",
		      "summary", "Power manager for GNOME",
		      NULL);
	ret = pk_results_add_package (results, item);
	g_object_unref (item);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get package list of set results");
	packages = pk_results_get_package_array (results);
	egg_test_assert (test, (packages->len == 1));

	/************************************************************/
	egg_test_title (test, "check data");
	item = g_ptr_array_index (packages, 0);
	g_object_get (item,
		      "info", &info,
		      "package-id", &package_id,
		      "summary", &summary,
		      NULL);
	egg_test_assert (test, (info == PK_INFO_ENUM_AVAILABLE &&
				g_strcmp0 ("gnome-power-manager;0.1.2;i386;fedora", package_id) == 0 &&
				g_strcmp0 ("Power manager for GNOME", summary) == 0));
	g_object_ref (item);
	g_ptr_array_unref (packages);
	g_free (package_id);
	g_free (summary);

	/************************************************************/
	egg_test_title (test, "check ref");
	g_object_get (item,
		      "info", &info,
		      "package-id", &package_id,
		      "summary", &summary,
		      NULL);
	egg_test_assert (test, (info == PK_INFO_ENUM_AVAILABLE &&
				g_strcmp0 ("gnome-power-manager;0.1.2;i386;fedora", package_id) == 0 &&
				g_strcmp0 ("Power manager for GNOME", summary) == 0));
	g_object_unref (item);
	g_free (package_id);
	g_free (summary);

	g_object_unref (results);
	egg_test_end (test);
}
#endif

