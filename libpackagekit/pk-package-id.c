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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include "pk-debug.h"
#include "pk-common.h"
#include "pk-package-id.h"

/**
 * pk_package_id_new:
 **/
PkPackageId *
pk_package_id_new (void)
{
	PkPackageId *ident;
	ident = g_new0 (PkPackageId, 1);
	ident->name = NULL;
	ident->version = NULL;
	ident->arch = NULL;
	ident->data = NULL;
	return ident;
}

/**
 * pk_package_id_check:
 **/
gboolean
pk_package_id_check (const gchar *package_id)
{
	gchar **sections;
	sections = pk_string_id_split (package_id, 4);
	if (sections != NULL) {
		g_strfreev (sections);
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_package_id_new_from_string:
 **/
PkPackageId *
pk_package_id_new_from_string (const gchar *package_id)
{
	gchar **sections;
	PkPackageId *ident = NULL;

	sections = pk_string_id_split (package_id, 4);
	if (sections == NULL) {
		return NULL;
	}

	/* create new object */
	ident = pk_package_id_new ();
	if (strlen (sections[0]) > 0) {
		ident->name = g_strdup (sections[0]);
	}
	if (strlen (sections[1]) > 0) {
		ident->version = g_strdup (sections[1]);
	}
	if (strlen (sections[2]) > 0) {
		ident->arch = g_strdup (sections[2]);
	}
	if (strlen (sections[3]) > 0) {
		ident->data = g_strdup (sections[3]);
	}
	g_strfreev (sections);
	return ident;
}

/**
 * pk_package_id_new_from_list:
 **/
PkPackageId *
pk_package_id_new_from_list (const gchar *name, const gchar *version,
		     const gchar *arch, const gchar *data)
{
	PkPackageId *ident = NULL;

	/* create new object */
	ident = pk_package_id_new ();
	ident->name = g_strdup (name);
	ident->version = g_strdup (version);
	ident->arch = g_strdup (arch);
	ident->data = g_strdup (data);
	return ident;
}

/**
 * pk_package_id_to_string:
 **/
gchar *
pk_package_id_to_string (PkPackageId *ident)
{
	return g_strdup_printf ("%s;%s;%s;%s",
				ident->name, ident->version,
				ident->arch, ident->data);
}

/**
 * pk_package_id_build:
 **/
gchar *
pk_package_id_build (const gchar *name, const gchar *version,
		     const gchar *arch, const gchar *data)
{
	return g_strdup_printf ("%s;%s;%s;%s", name, version, arch, data);
}

/**
 * pk_package_id_free:
 **/
gboolean
pk_package_id_free (PkPackageId *ident)
{
	if (ident == NULL) {
		return FALSE;
	}
	g_free (ident->name);
	g_free (ident->arch);
	g_free (ident->version);
	g_free (ident->data);
	g_free (ident);
	return TRUE;
}

/**
 * pk_string_id_equal:
 **/
gboolean
pk_package_id_equal (const gchar *pid1, const gchar *pid2)
{
	return pk_string_id_equal (pid1, pid2, 4, 3);
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
	PkPackageId *ident;

	if (libst_start (test, "PkPackageId", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************
	 ****************          IDENT           ******************
	 ************************************************************/

	libst_title (test, "pid equal pass (same)");
	ret = pk_package_id_equal ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;fedora");
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_title (test, "pid equal pass (different)");
	ret = pk_package_id_equal ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;data");
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_title (test, "get an ident object");
	ident = pk_package_id_new ();
	if (ident != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "test ident freeing early");
	ret = pk_package_id_free (ident);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "parse incorrect package_id from string (null)");
	temp = NULL;
	ident = pk_package_id_new_from_string (temp);
	if (ident == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed an invalid string '%s'", temp);
	}

	/************************************************************/
	libst_title (test, "parse incorrect package_id from string (empty)");
	temp = "";
	ident = pk_package_id_new_from_string (temp);
	if (ident == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed an invalid string '%s'", temp);
	}

	/************************************************************/
	libst_title (test, "parse incorrect package_id from string (not enough)");
	temp = "moo;0.0.1;i386";
	ident = pk_package_id_new_from_string (temp);
	if (ident == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed an invalid string '%s'", temp);
	}

	/************************************************************/
	libst_title (test, "parse package_id from string");
	ident = pk_package_id_new_from_string ("moo;0.0.1;i386;fedora");
	if (strcmp (ident->name, "moo") == 0 &&
	    strcmp (ident->arch, "i386") == 0 &&
	    strcmp (ident->data, "fedora") == 0 &&
	    strcmp (ident->version, "0.0.1") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "test ident building with valid data");
	text = pk_package_id_to_string (ident);
	if (strcmp (text, "moo;0.0.1;i386;fedora") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "package_id is '%s'", text);
	}
	g_free (text);
	pk_package_id_free (ident);

	libst_end (test);
}
#endif

