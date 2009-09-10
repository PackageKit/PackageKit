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
 * SECTION:pk-item
 * @short_description: A single piece of information from a transaction
 *
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib.h>

#include <packagekit-glib2/pk-item.h>
#include <packagekit-glib2/pk-enum.h>

#include "egg-debug.h"

/**
 * pk_item_package_ref:
 * @item: the #PkItemPackage
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemPackage *
pk_item_package_ref (PkItemPackage *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_details_ref:
 * @item: the #PkItemDetails
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemDetails *
pk_item_details_ref (PkItemDetails *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_update_detail_ref:
 * @item: the #PkItemUpdateDetail
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemUpdateDetail *
pk_item_update_detail_ref (PkItemUpdateDetail *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_category_ref:
 * @item: the #PkItemCategory
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemCategory *
pk_item_category_ref (PkItemCategory *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_distro_upgrade_ref:
 * @item: the #PkItemDistroUpgrade
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemDistroUpgrade *
pk_item_distro_upgrade_ref (PkItemDistroUpgrade *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_require_restart_ref:
 * @item: the #PkItemRequireRestart
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemRequireRestart *
pk_item_require_restart_ref (PkItemRequireRestart *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_transaction_ref:
 * @item: the #PkItemTransaction
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemTransaction *
pk_item_transaction_ref (PkItemTransaction *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_files_ref:
 * @item: the #PkItemFiles
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemFiles *
pk_item_files_ref (PkItemFiles *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_repo_signature_required_ref:
 * @item: the #PkItemRepoSignatureRequired
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemRepoSignatureRequired *
pk_item_repo_signature_required_ref (PkItemRepoSignatureRequired *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_eula_required_ref:
 * @item: the #PkItemEulaRequired
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemEulaRequired *
pk_item_eula_required_ref (PkItemEulaRequired *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_media_change_required_ref:
 * @item: the #PkItemMediaChangeRequired
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemMediaChangeRequired *
pk_item_media_change_required_ref (PkItemMediaChangeRequired *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_repo_detail_ref:
 * @item: the #PkItemRepoDetail
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemRepoDetail *
pk_item_repo_detail_ref (PkItemRepoDetail *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_error_code_ref:
 * @item: the #PkItemErrorCode
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemErrorCode *
pk_item_error_code_ref (PkItemErrorCode *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_message_ref:
 * @item: the #PkItemMessage
 *
 * Increases the reference count by one.
 *
 * Return value: the @item
 **/
PkItemMessage *
pk_item_message_ref (PkItemMessage *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	item->refcount++;
	return item;
}

/**
 * pk_item_package_unref:
 * @item: the #PkItemPackage
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemPackage *
pk_item_package_unref (PkItemPackage *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->package_id);
	g_free (item->summary);
	g_free (item);
	return NULL;
}

/**
 * pk_item_details_unref:
 * @item: the #PkItemDetails
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemDetails *
pk_item_details_unref (PkItemDetails *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->package_id);
	g_free (item->license);
	g_free (item->description);
	g_free (item->url);
	g_free (item);
	return NULL;
}

/**
 * pk_item_update_detail_unref:
 * @item: the #PkItemUpdateDetail
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemUpdateDetail *
pk_item_update_detail_unref (PkItemUpdateDetail *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

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
	return NULL;
}

/**
 * pk_item_category_unref:
 * @item: the #PkItemCategory
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemCategory *
pk_item_category_unref (PkItemCategory *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->parent_id);
	g_free (item->cat_id);
	g_free (item->name);
	g_free (item->summary);
	g_free (item->icon);
	g_free (item);
	return NULL;
}

/**
 * pk_item_distro_upgrade_unref:
 * @item: the #PkItemDistroUpgrade
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemDistroUpgrade *
pk_item_distro_upgrade_unref (PkItemDistroUpgrade *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->name);
	g_free (item->summary);
	g_free (item);
	return NULL;
}

/**
 * pk_item_require_restart_unref:
 * @item: the #PkItemRequireRestart
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemRequireRestart *
pk_item_require_restart_unref (PkItemRequireRestart *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->package_id);
	g_free (item);
	return NULL;
}

/**
 * pk_item_transaction_unref:
 * @item: the #PkItemTransaction
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemTransaction *
pk_item_transaction_unref (PkItemTransaction *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->tid);
	g_free (item->timespec);
	g_free (item->data);
	g_free (item->cmdline);
	g_free (item);
	return NULL;
}

/**
 * pk_item_files_unref:
 * @item: the #PkItemFiles
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemFiles *
pk_item_files_unref (PkItemFiles *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->package_id);
	g_strfreev (item->files);
	g_free (item);
	return NULL;
}

/**
 * pk_item_repo_signature_required_unref:
 * @item: the #PkItemRepoSignatureRequired
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemRepoSignatureRequired *
pk_item_repo_signature_required_unref (PkItemRepoSignatureRequired *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->package_id);
	g_free (item->repository_name);
	g_free (item->key_url);
	g_free (item->key_userid);
	g_free (item->key_id);
	g_free (item->key_fingerprint);
	g_free (item->key_timestamp);
	g_free (item);
	return NULL;
}

/**
 * pk_item_eula_required_unref:
 * @item: the #PkItemEulaRequired
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemEulaRequired *
pk_item_eula_required_unref (PkItemEulaRequired *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->eula_id);
	g_free (item->package_id);
	g_free (item->vendor_name);
	g_free (item->license_agreement);
	g_free (item);
	return NULL;
}

/**
 * pk_item_media_change_required_unref:
 * @item: the #PkItemMediaChangeRequired
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemMediaChangeRequired *
pk_item_media_change_required_unref (PkItemMediaChangeRequired *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->media_id);
	g_free (item->media_text);
	g_free (item);
	return NULL;
}

/**
 * pk_item_repo_detail_unref:
 * @item: the #PkItemRepoDetail
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemRepoDetail *
pk_item_repo_detail_unref (PkItemRepoDetail *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->repo_id);
	g_free (item->description);
	g_free (item);
	return NULL;
}

/**
 * pk_item_error_code_unref:
 * @item: the #PkItemErrorCode
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemErrorCode *
pk_item_error_code_unref (PkItemErrorCode *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->details);
	g_free (item);
	return NULL;
}

/**
 * pk_item_message_unref:
 * @item: the #PkItemMessage
 *
 * Decreases the reference count by one.
 *
 * Return value: the @item, or %NULL if the object is no longer valid
 **/
PkItemMessage *
pk_item_message_unref (PkItemMessage *item)
{
	g_return_val_if_fail (item != NULL, NULL);

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_free (item->details);
	g_free (item);
	return NULL;
}


/**
 * pk_item_package_new:
 *
 * Adds a package to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemPackage *
pk_item_package_new (PkInfoEnum info_enum, const gchar *package_id, const gchar *summary)
{
	PkItemPackage *item;

	g_return_val_if_fail (info_enum != PK_INFO_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkItemPackage, 1);
	item->info = info_enum;
	item->package_id = g_strdup (package_id);
	item->summary = g_strdup (summary);
	return item;
}

/**
 * pk_item_details_new:
 *
 * Adds some package details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemDetails *
pk_item_details_new (const gchar *package_id, const gchar *license,
		     PkGroupEnum group_enum, const gchar *description, const gchar *url, guint64 size)
{
	PkItemDetails *item;

	g_return_val_if_fail (package_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkItemDetails, 1);
	item->package_id = g_strdup (package_id);
	item->license = g_strdup (license);
	item->group = group_enum;
	item->description = g_strdup (description);
	item->url = g_strdup (url);
	item->size = size;
	return item;
}

/**
 * pk_item_update_detail_new:
 *
 * Adds some update details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemUpdateDetail *
pk_item_update_detail_new (const gchar *package_id, const gchar *updates,
			   const gchar *obsoletes, const gchar *vendor_url, const gchar *bugzilla_url,
			   const gchar *cve_url, PkRestartEnum restart_enum, const gchar *update_text,
			   const gchar *changelog, PkUpdateStateEnum state_enum, GDate *issued, GDate *updated)
{
	PkItemUpdateDetail *item;

	g_return_val_if_fail (package_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkItemUpdateDetail, 1);
	item->package_id = g_strdup (package_id);
	item->updates = g_strdup (updates);
	item->obsoletes = g_strdup (obsoletes);
	item->vendor_url = g_strdup (vendor_url);
	item->bugzilla_url = g_strdup (bugzilla_url);
	item->cve_url = g_strdup (cve_url);
	item->restart = restart_enum;
	item->update_text = g_strdup (update_text);
	item->changelog = g_strdup (changelog);
	item->state = state_enum;
	if (issued != NULL)
		item->issued = g_date_new_dmy (issued->day, issued->month, issued->year);
	if (updated != NULL)
		item->updated = g_date_new_dmy (updated->day, updated->month, updated->year);
	return item;
}

/**
 * pk_item_category_new:
 *
 * Adds a category item to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemCategory *
pk_item_category_new (const gchar *parent_id, const gchar *cat_id, const gchar *name,
		      const gchar *summary, const gchar *icon)
{
	PkItemCategory *item;

	g_return_val_if_fail (name != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkItemCategory, 1);
	item->parent_id = g_strdup (parent_id);
	item->cat_id = g_strdup (cat_id);
	item->name = g_strdup (name);
	item->summary = g_strdup (summary);
	item->icon = g_strdup (icon);
	return item;
}

/**
 * pk_item_distro_upgrade_new:
 *
 * Adds a distribution upgrade item to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemDistroUpgrade *
pk_item_distro_upgrade_new (PkUpdateStateEnum state_enum, const gchar *name, const gchar *summary)
{
	PkItemDistroUpgrade *item;

	g_return_val_if_fail (state_enum != PK_UPDATE_STATE_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkItemDistroUpgrade, 1);
	item->state = state_enum;
	item->name = g_strdup (name);
	item->summary = g_strdup (summary);
	return item;
}

/**
 * pk_item_require_restart_new:
 *
 * Adds a require restart item to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemRequireRestart *
pk_item_require_restart_new (PkRestartEnum restart_enum, const gchar *package_id)
{
	PkItemRequireRestart *item;

	g_return_val_if_fail (restart_enum != PK_RESTART_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkItemRequireRestart, 1);
	item->restart = restart_enum;
	item->package_id = g_strdup (package_id);
	return item;
}

/**
 * pk_item_transaction_new:
 *
 * Adds a transaction item to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemTransaction *
pk_item_transaction_new (const gchar *tid, const gchar *timespec,
			 gboolean succeeded, PkRoleEnum role_enum,
			 guint duration, const gchar *data,
			 guint uid, const gchar *cmdline)
{
	PkItemTransaction *item;

	g_return_val_if_fail (role_enum != PK_ROLE_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkItemTransaction, 1);
	item->tid = g_strdup (tid);
	item->timespec = g_strdup (timespec);
	item->succeeded = succeeded;
	item->role = role_enum;
	item->duration = duration;
	item->data = g_strdup (data);
	item->uid = uid;
	item->cmdline = g_strdup (cmdline);
	return item;
}

/**
 * pk_item_files_new:
 *
 * Adds some files details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemFiles *
pk_item_files_new (const gchar *package_id, gchar **files)
{
	PkItemFiles *item;

	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (files != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkItemFiles, 1);
	item->package_id = g_strdup (package_id);
	item->files = g_strdupv (files);
	return item;
}

/**
 * pk_item_repo_signature_required_new:
 *
 * Adds some repository signature details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemRepoSignatureRequired *
pk_item_repo_signature_required_new (const gchar *package_id, const gchar *repository_name,
				     const gchar *key_url, const gchar *key_userid, const gchar *key_id,
				     const gchar *key_fingerprint, const gchar *key_timestamp,
				     PkSigTypeEnum type_enum)
{
	PkItemRepoSignatureRequired *item;

	g_return_val_if_fail (package_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkItemRepoSignatureRequired, 1);
	item->package_id = g_strdup (package_id);
	item->repository_name = g_strdup (repository_name);
	item->key_url = g_strdup (key_url);
	item->key_userid = g_strdup (key_userid);
	item->key_id = g_strdup (key_id);
	item->key_fingerprint = g_strdup (key_fingerprint);
	item->key_timestamp = g_strdup (key_timestamp);
	item->type = type_enum;
	return item;
}

/**
 * pk_item_eula_required_new:
 *
 * Adds some EULA details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemEulaRequired *
pk_item_eula_required_new (const gchar *eula_id, const gchar *package_id,
			   const gchar *vendor_name, const gchar *license_agreement)
{
	PkItemEulaRequired *item;

	g_return_val_if_fail (eula_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkItemEulaRequired, 1);
	item->eula_id = g_strdup (eula_id);
	item->package_id = g_strdup (package_id);
	item->vendor_name = g_strdup (vendor_name);
	item->license_agreement = g_strdup (license_agreement);
	return item;
}

/**
 * pk_item_media_change_required_new:
 *
 * Adds some media change details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemMediaChangeRequired *
pk_item_media_change_required_new (PkMediaTypeEnum media_type_enum, const gchar *media_id, const gchar *media_text)
{
	PkItemMediaChangeRequired *item;

	g_return_val_if_fail (media_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkItemMediaChangeRequired, 1);
	item->media_type = media_type_enum;
	item->media_id = g_strdup (media_id);
	item->media_text = g_strdup (media_text);
	return item;
}

/**
 * pk_item_repo_detail_new:
 *
 * Adds some repository details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemRepoDetail *
pk_item_repo_detail_new (const gchar *repo_id, const gchar *description, gboolean enabled)
{
	PkItemRepoDetail *item;

	g_return_val_if_fail (repo_id != NULL, FALSE);

	/* copy and add to array */
	item = g_new0 (PkItemRepoDetail, 1);
	item->repo_id = g_strdup (repo_id);
	item->description = g_strdup (description);
	item->enabled = enabled;
	return item;
}

/**
 * pk_item_error_code_new:
 *
 * Adds some error details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemErrorCode *
pk_item_error_code_new (PkErrorCodeEnum code_enum, const gchar *details)
{
	PkItemErrorCode *item;

	/* copy and add to array */
	item = g_new0 (PkItemErrorCode, 1);
	item->code = code_enum;
	item->details = g_strdup (details);
	return item;
}

/**
 * pk_item_message_new:
 *
 * Adds some message details to the results set.
 *
 * Return value: %TRUE if the value was set
 **/
PkItemMessage *
pk_item_message_new (PkMessageEnum message_enum, const gchar *details)
{
	PkItemMessage *item;

	/* copy and add to array */
	item = g_new0 (PkItemMessage, 1);
	item->message = message_enum;
	item->details = g_strdup (details);
	return item;
}


/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_item_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	PkItemPackage *item;

	if (!egg_test_start (test, "PkItem"))
		return;

	item = pk_item_package_new (PK_INFO_ENUM_AVAILABLE, "gnome-power-manager;0.1.2;i386;fedora", "Power manager for GNOME");

	/************************************************************/
	egg_test_title (test, "check refcount");
	egg_test_assert (test, (item->refcount == 0));

	/************************************************************/
	egg_test_title (test, "check set");
	egg_test_assert (test, (item->info == PK_INFO_ENUM_AVAILABLE &&
				g_strcmp0 ("gnome-power-manager;0.1.2;i386;fedora", item->package_id) == 0 &&
				g_strcmp0 ("Power manager for GNOME", item->summary) == 0));

	/************************************************************/
	egg_test_title (test, "check refcount up");
	item = pk_item_package_ref (item);
	egg_test_assert (test, (item->refcount == 1));

	/************************************************************/
	egg_test_title (test, "check refcount down");
	item = pk_item_package_unref (item);
	egg_test_assert (test, (item->refcount == 0));

	/************************************************************/
	egg_test_title (test, "check NULL");
	item = pk_item_package_unref (item);
	egg_test_assert (test, (item == NULL));

	egg_test_end (test);
}
#endif

