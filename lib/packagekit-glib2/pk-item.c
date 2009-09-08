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
 * @short_description: TODO
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

	/* post decrement */
	if (item->refcount-- != 0)
		return item;

	g_strfreev (item->files);
	return NULL;
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

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_item_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;

	if (!egg_test_start (test, "PkResults"))
		return;

	egg_test_end (test);
}
#endif

