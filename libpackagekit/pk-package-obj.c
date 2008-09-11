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

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-common.h"
#include "pk-package-obj.h"

/**
 * pk_package_obj_new:
 **/
PkPackageObj *
pk_package_obj_new (PkInfoEnum info, const PkPackageId *id, const gchar *summary)
{
	PkPackageObj *obj;

	g_return_val_if_fail (id != NULL, NULL);

	obj = g_new0 (PkPackageObj, 1);
	obj->info = info;
	obj->id = pk_package_id_copy (id);
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
	pk_package_id_free (obj->id);
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
	return (obj1->info == obj2->info && pk_package_id_equal (obj1->id, obj2->id));
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
	return pk_package_obj_new (obj->info, obj->id, obj->summary);
}

/**
 * pk_package_obj_to_string:
 *
 * Convert a PkPackageObj to a string
 **/
gchar *
pk_package_obj_to_string (const PkPackageObj *obj)
{
	gchar *text;
	gchar *package_id;

	g_return_val_if_fail (obj != NULL, NULL);

	package_id = pk_package_id_to_string (obj->id);
	text = g_strdup_printf ("%s\t%s\t%s",
				pk_info_enum_to_text (obj->info),
				package_id, obj->summary);
	g_free (package_id);
	return text;
}

/**
 * pk_package_obj_from_string:
 *
 * Convert a PkPackageObj from a string
 **/
PkPackageObj *
pk_package_obj_from_string (const gchar *text)
{
	gchar **sections;
	PkPackageObj *obj = NULL;
	PkPackageId *id = NULL;
	PkInfoEnum info;

	g_return_val_if_fail (text != NULL, NULL);

	sections = g_strsplit (text, "\t", 3);
	if (sections == NULL) {
		egg_warning ("invalid input: %s", text);
		goto out;
	}

	info = pk_info_enum_from_text (sections[0]);
	if (info == PK_INFO_ENUM_UNKNOWN) {
		egg_warning ("invalid info for string %s", text);
		goto out;
	}
	id = pk_package_id_new_from_string (sections[1]);
	if (id == NULL) {
		egg_warning ("invalid package_id for string %s", text);
		goto out;
	}
	obj = pk_package_obj_new (info, id, sections[2]);
out:
	pk_package_id_free (id);
	g_strfreev (sections);
	return obj;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_package_obj_test (EggTest *test)
{
	PkPackageObj *obj1;
	PkPackageObj *obj2;
	PkPackageObj *obj3;
	gboolean ret;
	PkPackageId *id;
	gchar *text;

	if (!egg_test_start (test, "PkPackageObj"))
		return;

	/************************************************************/
	egg_test_title (test, "add entry");
	id = pk_package_id_new_from_string ("gnome;1.23;i386;data");
	obj1 = pk_package_obj_new (PK_INFO_ENUM_INSTALLED, id, "GNOME!");
	if (obj1 != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);
	pk_package_id_free (id);

	/************************************************************/
	egg_test_title (test, "add entry");
	id = pk_package_id_new_from_string ("gnome;1.23;i386;data");
	obj2 = pk_package_obj_new (PK_INFO_ENUM_INSTALLED, id, "GNOME foo!");
	if (obj2 != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);
	pk_package_id_free (id);

	/************************************************************/
	egg_test_title (test, "copy entry");
	obj3 = pk_package_obj_copy (obj2);
	if (obj3 != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check equal");
	ret = pk_package_obj_equal (obj1, obj3);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	pk_package_obj_free (obj2);
	pk_package_obj_free (obj3);

	/************************************************************/
	egg_test_title (test, "add entry");
	id = pk_package_id_new_from_string ("gnome-do;1.23;i386;data");
	obj2 = pk_package_obj_new (PK_INFO_ENUM_INSTALLED, id, "GNOME doo!");
	if (obj2 != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check !equal");
	ret = pk_package_obj_equal (obj1, obj2);
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check to string");
	text = pk_package_obj_to_string (obj1);
	if (egg_strequal (text, "installed\tgnome;1.23;i386;data\tGNOME!"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %s", text);

	/************************************************************/
	egg_test_title (test, "check from string");
	obj3 = pk_package_obj_from_string (text);
	if (obj3->info == PK_INFO_ENUM_INSTALLED &&
	    pk_package_id_equal (obj3->id, obj1->id) &&
	    egg_strequal (obj3->summary, "GNOME!"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "got incorrect data %s,%s,%s",
			      pk_info_enum_to_text (obj3->info),
			      pk_package_id_to_string (obj3->id),
			      obj3->summary);
	}

	pk_package_id_free (id);
	pk_package_obj_free (obj1);
	pk_package_obj_free (obj2);
	pk_package_obj_free (obj3);
	g_free (text);

	egg_test_end (test);
}
#endif

