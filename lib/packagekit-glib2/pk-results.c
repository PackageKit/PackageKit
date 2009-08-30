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
	PkExitEnum		 exit_enum;
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
	GPtrArray		*error_code_array;
	GPtrArray		*message_array;
};

enum {
	PROP_0,
	PROP_ROLE,
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
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

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
	g_free (item);
}

/**
 * pk_result_item_category_free:
 **/
static void
pk_result_item_category_free (PkResultItemCategory *item)
{
	if (item == NULL)
		return;
	g_free (item->parent_id);
	g_free (item->cat_id);
	g_free (item->name);
	g_free (item->summary);
	g_free (item->icon);
	g_free (item);
}

/**
 * pk_result_item_distro_upgrade_free:
 **/
static void
pk_result_item_distro_upgrade_free (PkResultItemDistroUpgrade *item)
{
	if (item == NULL)
		return;
	g_free (item->name);
	g_free (item->summary);
	g_free (item);
}

/**
 * pk_result_item_require_restart_free:
 **/
static void
pk_result_item_require_restart_free (PkResultItemRequireRestart *item)
{
	if (item == NULL)
		return;
	g_free (item->package_id);
	g_free (item);
}

/**
 * pk_result_item_transaction_free:
 **/
static void
pk_result_item_transaction_free (PkResultItemTransaction *item)
{
	if (item == NULL)
		return;
	g_free (item->tid);
	g_free (item->timespec);
	g_free (item->data);
	g_free (item->cmdline);
	g_free (item);
}

/**
 * pk_result_item_files_free:
 **/
static void
pk_result_item_files_free (PkResultItemFiles *item)
{
	if (item == NULL)
		return;
	g_free (item->package_id);
	g_strfreev (item->files);
	g_free (item);
}

/**
 * pk_result_item_repo_signature_required_free:
 **/
static void
pk_result_item_repo_signature_required_free (PkResultItemRepoSignatureRequired *item)
{
	if (item == NULL)
		return;
	g_free (item->package_id);
	g_free (item->repository_name);
	g_free (item->key_url);
	g_free (item->key_userid);
	g_free (item->key_id);
	g_free (item->key_fingerprint);
	g_free (item->key_timestamp);
	g_free (item);
}

/**
 * pk_result_item_eula_required_free:
 **/
static void
pk_result_item_eula_required_free (PkResultItemEulaRequired *item)
{
	if (item == NULL)
		return;
	g_free (item->eula_id);
	g_free (item->package_id);
	g_free (item->vendor_name);
	g_free (item->license_agreement);
	g_free (item);
}

/**
 * pk_result_item_media_change_required_free:
 **/
static void
pk_result_item_media_change_required_free (PkResultItemMediaChangeRequired *item)
{
	if (item == NULL)
		return;
	g_free (item->media_id);
	g_free (item->media_text);
	g_free (item);
}

/**
 * pk_result_item_repo_detail_free:
 **/
static void
pk_result_item_repo_detail_free (PkResultItemRepoDetail *item)
{
	if (item == NULL)
		return;
	g_free (item->repo_id);
	g_free (item->description);
	g_free (item);
}

/**
 * pk_result_item_error_code_free:
 **/
static void
pk_result_item_error_code_free (PkResultItemErrorCode *item)
{
	if (item == NULL)
		return;
	g_free (item->details);
	g_free (item);
}

/**
 * pk_result_item_message_free:
 **/
static void
pk_result_item_message_free (PkResultItemMessage *item)
{
	if (item == NULL)
		return;
	g_free (item->details);
	g_free (item);
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
 * pk_results_add_category:
 * @results: a valid #PkResults instance
 *
 * Adds a category item to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_category (PkResults *results, const gchar *parent_id, const gchar *cat_id, const gchar *name,
			 const gchar *summary, const gchar *icon)
{
	PkResultItemCategory *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemCategory, 1);
	item->parent_id = g_strdup (parent_id);
	item->cat_id = g_strdup (cat_id);
	item->name = g_strdup (name);
	item->summary = g_strdup (summary);
	item->icon = g_strdup (icon);
	g_ptr_array_add (results->priv->category_array, item);

	return TRUE;
}

/**
 * pk_results_add_distro_upgrade:
 * @results: a valid #PkResults instance
 *
 * Adds a distribution upgrade item to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_distro_upgrade (PkResults *results, PkUpdateStateEnum state_enum, const gchar *name, const gchar *summary)
{
	PkResultItemDistroUpgrade *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (state_enum != PK_UPDATE_STATE_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemDistroUpgrade, 1);
	item->state = state_enum;
	item->name = g_strdup (name);
	item->summary = g_strdup (summary);
	g_ptr_array_add (results->priv->distro_upgrade_array, item);

	return TRUE;
}

/**
 * pk_results_add_require_restart:
 * @results: a valid #PkResults instance
 *
 * Adds a require restart item to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_require_restart (PkResults *results, PkRestartEnum restart_enum, const gchar *package_id)
{
	PkResultItemRequireRestart *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (restart_enum != PK_RESTART_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemRequireRestart, 1);
	item->restart = restart_enum;
	item->package_id = g_strdup (package_id);
	g_ptr_array_add (results->priv->require_restart_array, item);

	return TRUE;
}

/**
 * pk_results_add_transaction:
 * @results: a valid #PkResults instance
 *
 * Adds a transaction item to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_transaction (PkResults *results, const gchar *tid, const gchar *timespec,
			    gboolean succeeded, PkRoleEnum role_enum,
			    guint duration, const gchar *data,
			    guint uid, const gchar *cmdline)
{
	PkResultItemTransaction *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (role_enum != PK_ROLE_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemTransaction, 1);
	item->tid = g_strdup (tid);
	item->timespec = g_strdup (timespec);
	item->succeeded = succeeded;
	item->role = role_enum;
	item->duration = duration;
	item->data = g_strdup (data);
	item->uid = uid;
	item->cmdline = g_strdup (cmdline);
	g_ptr_array_add (results->priv->transaction_array, item);

	return TRUE;
}

/**
 * pk_results_add_files:
 * @results: a valid #PkResults instance
 *
 * Adds some files details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_files (PkResults *results, const gchar *package_id, gchar **files)
{
	PkResultItemFiles *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (files != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemFiles, 1);
	item->package_id = g_strdup (package_id);
	item->files = g_strdupv (files);
	g_ptr_array_add (results->priv->files_array, item);

	return TRUE;
}

/**
 * pk_results_add_repo_signature_required:
 * @results: a valid #PkResults instance
 *
 * Adds some repository signature details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_repo_signature_required (PkResults *results, const gchar *package_id, const gchar *repository_name,
					const gchar *key_url, const gchar *key_userid, const gchar *key_id,
					const gchar *key_fingerprint, const gchar *key_timestamp,
					PkSigTypeEnum type_enum)
{
	PkResultItemRepoSignatureRequired *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemRepoSignatureRequired, 1);
	item->package_id = g_strdup (package_id);
	item->repository_name = g_strdup (repository_name);
	item->key_url = g_strdup (key_url);
	item->key_userid = g_strdup (key_userid);
	item->key_id = g_strdup (key_id);
	item->key_fingerprint = g_strdup (key_fingerprint);
	item->key_timestamp = g_strdup (key_timestamp);
	item->type = type_enum;
	g_ptr_array_add (results->priv->repo_signature_required_array, item);

	return TRUE;
}

/**
 * pk_results_add_eula_required:
 * @results: a valid #PkResults instance
 *
 * Adds some EULA details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_eula_required (PkResults *results, const gchar *eula_id, const gchar *package_id,
			      const gchar *vendor_name, const gchar *license_agreement)
{
	PkResultItemEulaRequired *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (eula_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemEulaRequired, 1);
	item->eula_id = g_strdup (eula_id);
	item->package_id = g_strdup (package_id);
	item->vendor_name = g_strdup (vendor_name);
	item->license_agreement = g_strdup (license_agreement);
	g_ptr_array_add (results->priv->eula_required_array, item);

	return TRUE;
}

/**
 * pk_results_add_media_change_required:
 * @results: a valid #PkResults instance
 *
 * Adds some media change details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_media_change_required (PkResults *results, PkMediaTypeEnum media_type_enum,
				      const gchar *media_id, const gchar *media_text)
{
	PkResultItemMediaChangeRequired *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (media_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemMediaChangeRequired, 1);
	item->media_type = media_type_enum;
	item->media_id = g_strdup (media_id);
	item->media_text = g_strdup (media_text);
	g_ptr_array_add (results->priv->media_change_required_array, item);

	return TRUE;
}

/**
 * pk_results_add_repo_detail:
 * @results: a valid #PkResults instance
 *
 * Adds some repository details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_repo_detail (PkResults *results, const gchar *repo_id,
			    const gchar *description, gboolean enabled)
{
	PkResultItemRepoDetail *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	g_return_val_if_fail (repo_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemRepoDetail, 1);
	item->repo_id = g_strdup (repo_id);
	item->description = g_strdup (description);
	item->enabled = enabled;
	g_ptr_array_add (results->priv->repo_detail_array, item);

	return TRUE;
}

/**
 * pk_results_add_error_code:
 * @results: a valid #PkResults instance
 *
 * Adds some error details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_error_code (PkResults *results, PkErrorCodeEnum code_enum, const gchar *details)
{
	PkResultItemErrorCode *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemErrorCode, 1);
	item->code = code_enum;
	item->details = g_strdup (details);
	g_ptr_array_add (results->priv->error_code_array, item);

	return TRUE;
}

/**
 * pk_results_add_message:
 * @results: a valid #PkResults instance
 *
 * Adds some message details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
gboolean
pk_results_add_message (PkResults *results, PkMessageEnum message_enum, const gchar *details)
{
	PkResultItemMessage *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);

	/* copy and add to array */
	item = g_new0 (PkResultItemMessage, 1);
	item->message = message_enum;
	item->details = g_strdup (details);
	g_ptr_array_add (results->priv->message_array, item);

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
 * pk_results_get_category_array:
 * @results: a valid #PkResults instance
 *
 * Gets the categories from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemCategory's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_category_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	return g_ptr_array_ref (results->priv->category_array);
}

/**
 * pk_results_get_distro_upgrade_array:
 * @results: a valid #PkResults instance
 *
 * Gets the distribution upgrades from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemDistroUpgrade's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_distro_upgrade_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	return g_ptr_array_ref (results->priv->distro_upgrade_array);
}

/**
 * pk_results_get_require_restart_array:
 * @results: a valid #PkResults instance
 *
 * Gets the require restarts from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemRequireRestart's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_require_restart_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
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
	guint i;
	const PkResultItemRequireRestart *item;

	g_return_val_if_fail (PK_IS_RESULTS (results), 0);

	array = results->priv->require_restart_array;
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (item->restart > worst)
			worst = item->restart;
	}

	return worst;
}

/**
 * pk_results_get_transaction_array:
 * @results: a valid #PkResults instance
 *
 * Gets the transactions from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemTransaction's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_transaction_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	return g_ptr_array_ref (results->priv->transaction_array);
}

/**
 * pk_results_get_files_array:
 * @results: a valid #PkResults instance
 *
 * Gets the files from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemFiles's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_files_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	return g_ptr_array_ref (results->priv->files_array);
}

/**
 * pk_results_get_repo_signature_required_array:
 * @results: a valid #PkResults instance
 *
 * Gets the repository signatures required from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemRepoSignatureRequired's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_repo_signature_required_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	return g_ptr_array_ref (results->priv->repo_signature_required_array);
}

/**
 * pk_results_get_eula_required_array:
 * @results: a valid #PkResults instance
 *
 * Gets the eulas required from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemEulaRequired's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_eula_required_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	return g_ptr_array_ref (results->priv->eula_required_array);
}

/**
 * pk_results_get_media_change_required_array:
 * @results: a valid #PkResults instance
 *
 * Gets the media changes required from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemMediaChangeRequired's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_media_change_required_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	return g_ptr_array_ref (results->priv->media_change_required_array);
}

/**
 * pk_results_get_repo_detail_array:
 * @results: a valid #PkResults instance
 *
 * Gets the repository details from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemRepoDetail's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_repo_detail_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	return g_ptr_array_ref (results->priv->repo_detail_array);
}

/**
 * pk_results_get_error_code_array:
 * @results: a valid #PkResults instance
 *
 * Gets the error codes from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemErrorCode's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_error_code_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
	return g_ptr_array_ref (results->priv->error_code_array);
}

/**
 * pk_results_get_error_code:
 * @results: a valid #PkResults instance
 *
 * Gets the last error code from the transaction.
 *
 * Return value: A #PkResultItemErrorCode, or %NULL
 **/
const PkResultItemErrorCode *
pk_results_get_error_code (PkResults *results)
{
	GPtrArray *array;

	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);

	array = results->priv->error_code_array;
	if (array->len == 0)
		return NULL;
	return g_ptr_array_index (array, 0);
}

/**
 * pk_results_get_message_array:
 * @results: a valid #PkResults instance
 *
 * Gets the messages from the transaction.
 *
 * Return value: A #GPtrArray array of #PkResultItemMessage's, free with g_ptr_array_unref().
 **/
GPtrArray *
pk_results_get_message_array (PkResults *results)
{
	g_return_val_if_fail (PK_IS_RESULTS (results), FALSE);
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
				   0, PK_ROLE_ENUM_UNKNOWN, PK_ROLE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ROLE, pspec);

	g_type_class_add_private (klass, sizeof (PkResultsPrivate));
}

/**
 * pk_results_init:
 **/
static void
pk_results_init (PkResults *results)
{
	results->priv = PK_RESULTS_GET_PRIVATE (results);
	results->priv->exit_enum = PK_EXIT_ENUM_UNKNOWN;
	results->priv->package_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_package_free);
	results->priv->details_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_details_free);
	results->priv->update_detail_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_update_detail_free);
	results->priv->category_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_category_free);
	results->priv->distro_upgrade_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_distro_upgrade_free);
	results->priv->require_restart_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_require_restart_free);
	results->priv->transaction_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_transaction_free);
	results->priv->files_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_files_free);
	results->priv->repo_signature_required_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_repo_signature_required_free);
	results->priv->eula_required_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_eula_required_free);
	results->priv->media_change_required_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_media_change_required_free);
	results->priv->repo_detail_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_repo_detail_free);
	results->priv->error_code_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_error_code_free);
	results->priv->message_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_result_item_message_free);
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
	g_ptr_array_unref (priv->error_code_array);
	g_ptr_array_unref (priv->message_array);

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
	egg_test_end (test);
}
#endif

