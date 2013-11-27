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
	PkBitfield		 transaction_flags;
	guint			 inputs;
	PkProgress		*progress;
	PkExitEnum		 exit_enum;
	PkError			*error_code;
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
	PkPackageSack		*package_sack;
};

enum {
	PROP_0,
	PROP_ROLE,
	PROP_TRANSACTION_FLAGS,
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
	case PROP_TRANSACTION_FLAGS:
		g_value_set_uint64 (value, priv->transaction_flags);
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
	case PROP_TRANSACTION_FLAGS:
		priv->transaction_flags = g_value_get_uint64 (value);
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.3
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
		g_warning ("internal error: finished packages cannot be added to a PkResults object");
		return FALSE;
	}
	pk_package_sack_add_package (results->priv->package_sack, item);
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
 **/
PkExitEnum
pk_results_get_exit_code (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), PK_EXIT_ENUM_UNKNOWN);
	return results->priv->exit_enum;
}

/**
 * pk_results_get_role:
 * @results: a valid #PkResults instance
 *
 * Gets the role that produced these results.
 *
 * Return value: The #PkRoleEnum or %PK_ROLE_ENUM_UNKNOWN if not set
 *
 * Since: 0.7.5
 **/
PkRoleEnum
pk_results_get_role (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), PK_ROLE_ENUM_UNKNOWN);
	return results->priv->role;
}

/**
 * pk_results_get_transaction_flags:
 * @results: a valid #PkResults instance
 *
 * Gets the transaction flag for these results.
 *
 * Return value: The #PkBitfield or 0 if not set
 *
 * Since: 0.8.1
 **/
PkBitfield
pk_results_get_transaction_flags (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), 0);
	return results->priv->transaction_flags;
}

/**
 * pk_results_get_error_code:
 * @results: a valid #PkResults instance
 *
 * Gets the last error code from the transaction.
 *
 * Return value: (transfer full): A #PkError, or %NULL, free with g_object_unref()
 *
 * Since: 0.5.2
 **/
PkError *
pk_results_get_error_code (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);

	/* failed, but no exit code? */
	if (results->priv->error_code == NULL && results->priv->exit_enum != PK_EXIT_ENUM_SUCCESS) {
		g_warning ("internal error: failed, but no error code: %s",
			   pk_exit_enum_to_string (results->priv->exit_enum));
		return NULL;
	}

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
 * Return value: (element-type PkPackage) (transfer container): A #GPtrArray array of #PkPackage's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
 **/

GPtrArray *
pk_results_get_package_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return pk_package_sack_get_array (results->priv->package_sack);
}

/**
 * pk_results_get_package_sack:
 * @results: a valid #PkResults instance
 *
 * Gets a package sack from the transaction.
 *
 * Return value: (transfer full): A #PkPackageSack of data, g_object_unref() to free.
 *
 * Since: 0.5.2
 **/
PkPackageSack *
pk_results_get_package_sack (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), NULL);
	return g_object_ref (results->priv->package_sack);
}

/**
 * pk_results_get_details_array:
 * @results: a valid #PkResults instance
 *
 * Gets the package details from the transaction.
 *
 * Return value: (element-type PkDetails) (transfer container): A #GPtrArray array of #PkDetails's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
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
 * Return value: (element-type PkUpdateDetail) (transfer container): A #GPtrArray array of #PkUpdateDetail's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
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
 * Return value: (element-type PkCategory) (transfer container): A #GPtrArray array of #PkCategory's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
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
 * Return value: (element-type PkDistroUpgrade) (transfer container): A #GPtrArray array of #PkDistroUpgrade's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
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
 * Return value: (element-type PkRequireRestart) (transfer container): A #GPtrArray array of #PkRequireRestart's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
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
 *
 * Since: 0.5.2
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
 * Return value: (element-type PkTransactionPast) (transfer container): A #GPtrArray array of #PkTransactionPast's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
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
 * Return value: (element-type PkFiles) (transfer container): A #GPtrArray array of #PkFiles's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
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
 * Return value: (element-type PkRepoSignatureRequired) (transfer container): A #GPtrArray array of #PkRepoSignatureRequired's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
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
 * Return value: (element-type PkEulaRequired) (transfer container): A #GPtrArray array of #PkEulaRequired's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
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
 * Return value: (element-type PkMediaChangeRequired) (transfer container): A #GPtrArray array of #PkMediaChangeRequired's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
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
 * Return value: (element-type PkRepoDetail) (transfer container): A #GPtrArray array of #PkRepoDetail's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
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
 * Return value: (element-type PkMessage) (transfer container): A #GPtrArray array of #PkMessage's, free with g_ptr_array_unref().
 *
 * Since: 0.5.2
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
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("role", NULL, NULL,
				   0, PK_ROLE_ENUM_LAST, PK_ROLE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ROLE, pspec);

	/**
	 * PkResults:transaction-flags:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_uint64 ("transaction-flags", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_TRANSACTION_FLAGS, pspec);

	/**
	 * PkResults:inputs:
	 *
	 * Since: 0.5.3
	 */
	pspec = g_param_spec_uint ("inputs", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_INPUTS, pspec);

	g_type_class_add_private (klass, sizeof (PkResultsPrivate));

	/**
	 * PkResults:progress:
	 *
	 * Since: 0.5.3
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
	results->priv->package_sack = pk_package_sack_new ();
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
	g_object_unref (priv->package_sack);
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
 *
 * Since: 0.5.2
 **/
PkResults *
pk_results_new (void)
{
	PkResults *results;
	results = g_object_new (PK_TYPE_RESULTS, NULL);
	return PK_RESULTS (results);
}
