/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
 * SECTION:pk-package-item
 * @short_description: A cached Package structure
 *
 * These provide a way to query and store a single package.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>

#include "pk-debug.h"
#include "pk-common.h"
#include "pk-package-item.h"

/**
 * pk_package_item_new:
 **/
PkPackageItem *
pk_package_item_new (PkInfoEnum info, const gchar *package_id, const gchar *summary)
{
	PkPackageItem *item;

	g_return_val_if_fail (package_id != NULL, FALSE);

	pk_debug ("adding to cache item package %s, %s, %s", pk_info_enum_to_text (info), package_id, summary);
	item = g_new0 (PkPackageItem, 1);
	item->info = info;
	item->package_id = g_strdup (package_id);
	item->summary = g_strdup (summary);
	return item;
}

/**
 * pk_package_item_free:
 **/
gboolean
pk_package_item_free (PkPackageItem *item)
{
	g_return_val_if_fail (item != NULL, FALSE);

	g_free (item->package_id);
	g_free (item->summary);
	g_free (item);
	return TRUE;
}

/**
 * pk_package_item_equal:
 *
 * Only compares the package_id's and the info enum
 **/
gboolean
pk_package_item_equal (PkPackageItem *item1, PkPackageItem *item2)
{
	return (item1->info == item2->info &&
		pk_strequal (item1->package_id, item2->package_id));
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_package_item (LibSelfTest *test)
{
	PkPackageItem *item1;
	PkPackageItem *item2;
	gboolean ret;

	if (libst_start (test, "PkPackageItem", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "add entry");
	item1 = pk_package_item_new (PK_INFO_ENUM_INSTALLED, "gnome;1.23;i386;data", "GNOME!");
	if (item1 != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "add entry");
	item2 = pk_package_item_new (PK_INFO_ENUM_INSTALLED, "gnome;1.23;i386;data", "GNOME foo!");
	if (item2 != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check equal");
	ret = pk_package_item_equal (item1, item2);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	pk_package_item_free (item2);
	/************************************************************/
	libst_title (test, "add entry");
	item2 = pk_package_item_new (PK_INFO_ENUM_INSTALLED, "gnome-do;1.23;i386;data", "GNOME doo!");
	if (item2 != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check !equal");
	ret = pk_package_item_equal (item1, item2);
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	pk_package_item_free (item1);
	pk_package_item_free (item2);

	libst_end (test);
}
#endif

