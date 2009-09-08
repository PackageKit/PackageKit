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
 * pk_item_package_unref:
 **/
void
pk_item_package_unref (PkItemPackage *item)
{
	if (item == NULL)
		return;
	g_free (item->package_id);
	g_free (item->summary);
	g_free (item);
}

/**
 * pk_item_details_unref:
 **/
void
pk_item_details_unref (PkItemDetails *item)
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
 * pk_item_update_detail_unref:
 **/
void
pk_item_update_detail_unref (PkItemUpdateDetail *item)
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
 * pk_item_category_unref:
 **/
void
pk_item_category_unref (PkItemCategory *item)
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
 * pk_item_distro_upgrade_unref:
 **/
void
pk_item_distro_upgrade_unref (PkItemDistroUpgrade *item)
{
	if (item == NULL)
		return;
	g_free (item->name);
	g_free (item->summary);
	g_free (item);
}

/**
 * pk_item_require_restart_unref:
 **/
void
pk_item_require_restart_unref (PkItemRequireRestart *item)
{
	if (item == NULL)
		return;
	g_free (item->package_id);
	g_free (item);
}

/**
 * pk_item_transaction_unref:
 **/
void
pk_item_transaction_unref (PkItemTransaction *item)
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
 * pk_item_files_unref:
 **/
void
pk_item_files_unref (PkItemFiles *item)
{
	if (item == NULL)
		return;
	g_free (item->package_id);
	g_strfreev (item->files);
	g_free (item);
}

/**
 * pk_item_repo_signature_required_unref:
 **/
void
pk_item_repo_signature_required_unref (PkItemRepoSignatureRequired *item)
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
 * pk_item_eula_required_unref:
 **/
void
pk_item_eula_required_unref (PkItemEulaRequired *item)
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
 * pk_item_media_change_required_unref:
 **/
void
pk_item_media_change_required_unref (PkItemMediaChangeRequired *item)
{
	if (item == NULL)
		return;
	g_free (item->media_id);
	g_free (item->media_text);
	g_free (item);
}

/**
 * pk_item_repo_detail_unref:
 **/
void
pk_item_repo_detail_unref (PkItemRepoDetail *item)
{
	if (item == NULL)
		return;
	g_free (item->repo_id);
	g_free (item->description);
	g_free (item);
}

/**
 * pk_item_error_code_unref:
 **/
void
pk_item_error_code_unref (PkItemErrorCode *item)
{
	if (item == NULL)
		return;
	g_free (item->details);
	g_free (item);
}

/**
 * pk_item_message_unref:
 **/
void
pk_item_message_unref (PkItemMessage *item)
{
	if (item == NULL)
		return;
	g_free (item->details);
	g_free (item);
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
#if 0
	gboolean ret;
	PkResults *results;
	PkExitEnum exit_enum;
	GPtrArray *packages;
	const PkItemPackage *item;

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

	g_object_unref (results);
#endif

	egg_test_end (test);
}
#endif

