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
 * SECTION:pk-package-obj
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
#include "pk-package-obj.h"

/**
 * pk_package_obj_new:
 **/
PkPackageObj *
pk_package_obj_new (PkInfoEnum info, const gchar *package_id, const gchar *summary)
{
	PkPackageObj *obj;

	g_return_val_if_fail (package_id != NULL, FALSE);

	obj = g_new0 (PkPackageObj, 1);
	obj->info = info;
	obj->package_id = g_strdup (package_id);
	obj->summary = g_strdup (summary);
	return obj;
}

/**
 * pk_package_obj_free:
 **/
gboolean
pk_package_obj_free (PkPackageObj *obj)
{
	if (obj == NULL) {
		return FALSE;
	}
	g_free (obj->package_id);
	g_free (obj->summary);
	g_free (obj);
	return TRUE;
}

/**
 * pk_package_obj_equal:
 *
 * Only compares the package_id's and the info enum
 **/
gboolean
pk_package_obj_equal (const PkPackageObj *obj1, const PkPackageObj *obj2)
{
	if (obj1 == NULL || obj2 == NULL) {
		return FALSE;
	}
	return (obj1->info == obj2->info &&
		pk_strequal (obj1->package_id, obj2->package_id));
}

/**
 * pk_package_obj_copy:
 *
 * Copy a PkPackageObj
 **/
PkPackageObj *
pk_package_obj_copy (const PkPackageObj *obj)
{
	g_return_val_if_fail (obj != NULL, NULL);
	return pk_package_obj_new (obj->info, obj->package_id, obj->summary);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_package_obj (LibSelfTest *test)
{
	PkPackageObj *obj1;
	PkPackageObj *obj2;
	PkPackageObj *obj3;
	gboolean ret;

	if (libst_start (test, "PkPackageObj", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "add entry");
	obj1 = pk_package_obj_new (PK_INFO_ENUM_INSTALLED, "gnome;1.23;i386;data", "GNOME!");
	if (obj1 != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "add entry");
	obj2 = pk_package_obj_new (PK_INFO_ENUM_INSTALLED, "gnome;1.23;i386;data", "GNOME foo!");
	if (obj2 != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "copy entry");
	obj3 = pk_package_obj_copy (obj2);
	if (obj3 != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check equal");
	ret = pk_package_obj_equal (obj1, obj3);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	pk_package_obj_free (obj2);
	pk_package_obj_free (obj3);

	/************************************************************/
	libst_title (test, "add entry");
	obj2 = pk_package_obj_new (PK_INFO_ENUM_INSTALLED, "gnome-do;1.23;i386;data", "GNOME doo!");
	if (obj2 != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check !equal");
	ret = pk_package_obj_equal (obj1, obj2);
	if (!ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	pk_package_obj_free (obj1);
	pk_package_obj_free (obj2);

	libst_end (test);
}
#endif

