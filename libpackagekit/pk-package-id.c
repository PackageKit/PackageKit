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
 * SECTION:pk-package-id
 * @short_description: Functionality to modify a PackageID
 *
 * PackageId's are difficult to read and create.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-common.h"
#include "pk-package-id.h"

/**
 * pk_package_id_new:
 *
 * Creates a new #PkPackageId object with default values
 *
 * Return value: a new #PkPackageId object
 **/
PkPackageId *
pk_package_id_new (void)
{
	PkPackageId *id;
	id = g_new0 (PkPackageId, 1);
	id->name = NULL;
	id->version = NULL;
	id->arch = NULL;
	id->data = NULL;
	return id;
}

/**
 * pk_package_id_check:
 * @package_id: the text the check
 *
 * Return value: %TRUE if the package_id was well formed.
 **/
gboolean
pk_package_id_check (const gchar *package_id)
{
	gchar **sections;
	gboolean ret;

	if (package_id == NULL) {
		return FALSE;
	}
	ret = g_utf8_validate (package_id, -1, NULL);
	if (!ret) {
		egg_warning ("invalid UTF8!");
		return FALSE;
	}
	sections = pk_strsplit (package_id, 4);
	if (sections != NULL) {
		g_strfreev (sections);
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_package_id_new_from_string:
 * @package_id: the text to pre-fill the object
 *
 * Creates a new #PkPackageId object with values taken from the supplied id.
 *
 * Return value: a new #PkPackageId object
 **/
PkPackageId *
pk_package_id_new_from_string (const gchar *package_id)
{
	gchar **sections;
	PkPackageId *id = NULL;

	g_return_val_if_fail (package_id != NULL, NULL);

	sections = pk_strsplit (package_id, 4);
	if (sections == NULL) {
		return NULL;
	}

	/* create new object */
	id = pk_package_id_new ();
	if (egg_strzero (sections[0]) == FALSE) {
		id->name = g_strdup (sections[0]);
	}
	if (egg_strzero (sections[1]) == FALSE) {
		id->version = g_strdup (sections[1]);
	}
	if (egg_strzero (sections[2]) == FALSE) {
		id->arch = g_strdup (sections[2]);
	}
	if (egg_strzero (sections[3]) == FALSE) {
		id->data = g_strdup (sections[3]);
	}
	g_strfreev (sections);
	return id;
}

/**
 * pk_package_id_new_from_list:
 * @name: the package name
 * @version: the package version
 * @arch: the package architecture
 * @data: the package extra data
 *
 * Creates a new #PkPackageId object with values.
 *
 * Return value: a new #PkPackageId object
 **/
PkPackageId *
pk_package_id_new_from_list (const gchar *name, const gchar *version, const gchar *arch, const gchar *data)
{
	PkPackageId *id = NULL;

	g_return_val_if_fail (name != NULL, NULL);

	/* create new object */
	id = pk_package_id_new ();
	id->name = g_strdup (name);
	id->version = g_strdup (version);
	id->arch = g_strdup (arch);
	id->data = g_strdup (data);
	return id;
}

/**
 * pk_package_id_copy:
 * @id: the %PkPackageId structure to copy
 *
 * Copies into a new #PkPackageId object.
 *
 * Return value: a new #PkPackageId object
 **/
PkPackageId *
pk_package_id_copy (const PkPackageId *id)
{
	return pk_package_id_new_from_list (id->name, id->version, id->arch, id->data);
}

/**
 * pk_package_id_to_string:
 * @id: A #PkPackageId object
 *
 * Return value: returns a string representation of #PkPackageId.
 **/
gchar *
pk_package_id_to_string (const PkPackageId *id)
{
	return g_strdup_printf ("%s;%s;%s;%s",
				id->name, id->version,
				id->arch, id->data);
}

/**
 * pk_package_id_build:
 * @name: the package name
 * @version: the package version
 * @arch: the package architecture
 * @data: the package extra data
 *
 * Return value: returns a string putting together the data.
 **/
gchar *
pk_package_id_build (const gchar *name, const gchar *version,
		     const gchar *arch, const gchar *data)
{
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (version != NULL, NULL);
	g_return_val_if_fail (arch != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);

	return g_strdup_printf ("%s;%s;%s;%s", name, version, arch, data);
}

/**
 * pk_package_id_free:
 * @id: the #PkPackageId object
 *
 * Return value: %TRUE if the #PkPackageId object was freed.
 **/
gboolean
pk_package_id_free (PkPackageId *id)
{
	if (id == NULL) {
		return FALSE;
	}
	g_free (id->name);
	g_free (id->arch);
	g_free (id->version);
	g_free (id->data);
	g_free (id);
	return TRUE;
}

/**
 * pk_package_id_equal:
 * @id1: the first %PkPackageId
 * @id2: the second %PkPackageId
 *
 * Only compare the name, version, and arch
 *
 * Return value: %TRUE if the ids can be considered equal.
 **/
gboolean
pk_package_id_equal (const PkPackageId *id1, const PkPackageId *id2)
{
	if (egg_strequal (id1->name, id2->name) &&
	    egg_strequal (id1->version, id2->version) &&
	    egg_strequal (id1->arch, id2->arch))
		return TRUE;
	return FALSE;
}

/**
 * pk_package_id_equal_strings:
 * @pid1: the first package_id
 * @pid2: the second package_id
 *
 * Only compare the first three sections, data is not part of the match
 *
 * Return value: %TRUE if the package_id's can be considered equal.
 **/
gboolean
pk_package_id_equal_strings (const gchar *pid1, const gchar *pid2)
{
	return pk_strcmp_sections (pid1, pid2, 4, 3);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_package_id (LibSelfTest *test)
{
	gboolean ret;
	gchar *text;
	const gchar *temp;
	PkPackageId *id;
	PkPackageId *id2;

	if (libst_start (test, "PkPackageId", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************
	 ****************          id           ******************
	 ************************************************************/

	libst_title (test, "id build");
	text = pk_package_id_build ("moo", "0.0.1", "i386", "fedora");
	if (egg_strequal (text, "moo;0.0.1;i386;fedora")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}
	g_free (text);

	libst_title (test, "pid equal pass (same)");
	ret = pk_package_id_equal_strings ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;fedora");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "pid equal pass (different)");
	ret = pk_package_id_equal_strings ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;data");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get an id object");
	id = pk_package_id_new ();
	if (id != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "test id freeing early");
	ret = pk_package_id_free (id);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "parse incorrect package_id from string (empty)");
	temp = "";
	id = pk_package_id_new_from_string (temp);
	if (id == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed an invalid string '%s'", temp);
	}

	/************************************************************/
	libst_title (test, "parse incorrect package_id from string (not enough)");
	temp = "moo;0.0.1;i386";
	id = pk_package_id_new_from_string (temp);
	if (id == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed an invalid string '%s'", temp);
	}

	/************************************************************/
	libst_title (test, "parse package_id from string");
	id = pk_package_id_new_from_string ("moo;0.0.1;i386;fedora");
	if (id != NULL &&
	    egg_strequal (id->name, "moo") &&
	    egg_strequal (id->arch, "i386") &&
	    egg_strequal (id->data, "fedora") &&
	    egg_strequal (id->version, "0.0.1")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "test copying");
	id2 = pk_package_id_copy (id);
	if (id2 != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "test id building with valid data");
	text = pk_package_id_to_string (id2);
	if (egg_strequal (text, "moo;0.0.1;i386;fedora")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "package_id is '%s'", text);
	}
	g_free (text);
	pk_package_id_free (id);
	pk_package_id_free (id2);

	/************************************************************/
	libst_title (test, "parse short package_id from string");
	id = pk_package_id_new_from_string ("moo;0.0.1;;");
	if (id != NULL &&
	    egg_strequal (id->name, "moo") &&
	    egg_strequal (id->version, "0.0.1") &&
	    id->data == NULL &&
	    id->arch == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}
	pk_package_id_free (id);

	libst_end (test);
}
#endif

