/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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
 * ExtraObject's are difficult to read and create.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include "pk-debug.h"
#include "pk-common.h"
#include "pk-package-id.h"
#include "pk-extra.h"
#include "pk-extra-obj.h"

/**
 * pk_extra_obj_new:
 *
 * Creates a new #PkExtraObj object with default values
 *
 * Return value: a new #PkExtraObj object
 **/
PkExtraObj *
pk_extra_obj_new (void)
{
	PkExtraObj *extra_obj;
	extra_obj = g_new0 (PkExtraObj, 1);
	extra_obj->id = NULL;
	extra_obj->icon = NULL;
	extra_obj->exec = NULL;
	extra_obj->summary = NULL;
	return extra_obj;
}

/**
 * pk_extra_obj_new_from_package_id:
 * @package_id: the package_id to pre-fill the object
 *
 * Creates a new #PkExtraObj object with values taken from the supplied id.
 *
 * Return value: a new #PkExtraObj object
 **/
PkExtraObj *
pk_extra_obj_new_from_package_id (const gchar *package_id)
{
	PkExtra *extra;
	PkExtraObj *extra_obj;

	extra_obj = pk_extra_obj_new ();
	extra_obj->id = pk_package_id_new_from_string (package_id);

	extra = pk_extra_new ();
	pk_extra_get_localised_detail (extra, extra_obj->id->name, &extra_obj->summary, NULL, NULL);
	pk_extra_get_package_detail (extra, extra_obj->id->name, &extra_obj->icon, &extra_obj->exec);
	g_object_unref (extra);

	return extra_obj;
}

/**
 * pk_extra_obj_new_from_package_id_summary:
 * @package_id: the package_id to pre-fill the object
 *
 * Creates a new #PkExtraObj object with values taken from the supplied id.
 *
 * Return value: a new #PkExtraObj object
 **/
PkExtraObj *
pk_extra_obj_new_from_package_id_summary (const gchar *package_id, const gchar *summary)
{
	PkExtraObj *extra_obj;
	extra_obj = pk_extra_obj_new_from_package_id (package_id);
	/* nothing better */
	if (extra_obj->summary == NULL) {
		extra_obj->summary = g_strdup (summary);
	}
	return extra_obj;
}

/**
 * pk_extra_obj_free:
 * @extra_obj: the #PkExtraObj object
 *
 * Return value: %TRUE if the #PkExtraObj object was freed.
 **/
gboolean
pk_extra_obj_free (PkExtraObj *extra_obj)
{
	if (extra_obj == NULL) {
		return FALSE;
	}
	if (extra_obj->id != NULL) {
		pk_package_id_free (extra_obj->id);
	}
	g_free (extra_obj->icon);
	g_free (extra_obj->exec);
	g_free (extra_obj->summary);
	g_free (extra_obj);
	return TRUE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_extra_obj (LibSelfTest *test)
{
	gboolean ret;
	PkExtra *extra;
	PkExtraObj *extra_obj;

	if (libst_start (test, "PkExtraObj", CLASS_AUTO) == FALSE) {
		return;
	}

	/* should be single instance */
	extra = pk_extra_new ();
	pk_extra_set_database (extra, PK_EXTRA_DEFAULT_DATABASE);
	pk_extra_set_locale (extra, "fr");

	/************************************************************/
	libst_title (test, "get an extra_obj object");
	extra_obj = pk_extra_obj_new_from_package_id ("gnome-power-manager;0.0.1;i386;fedora");
	if (extra_obj != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "got an icon");
	if (extra_obj->icon != NULL) {
		libst_success (test, "got %s", extra_obj->icon);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "got an exec");
	if (extra_obj->exec != NULL) {
		libst_success (test, "got %s", extra_obj->exec);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "got an summary");
	if (extra_obj->summary != NULL) {
		libst_success (test, "got %s", extra_obj->summary);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "free extra_obj");
	ret = pk_extra_obj_free (extra_obj);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_end (test);
}
#endif

