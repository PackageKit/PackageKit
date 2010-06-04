/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-package-id
 * @short_description: Functionality to read a PackageID
 */

#include "config.h"

#include "egg-debug.h"

#include <glib.h>

#include <packagekit-glib2/pk-package-id.h>

/**
 * pk_package_id_split:
 * @package_id: the ; delimited PackageID to split
 *
 * Splits a PackageID into the correct number of parts, checking the correct
 * number of delimiters are present.
 *
 * Return value: a GStrv or %NULL if invalid, use g_strfreev() to free
 *
 * Since: 0.5.3
 **/
gchar **
pk_package_id_split (const gchar *package_id)
{
	gchar **sections = NULL;

	if (package_id == NULL)
		goto out;

	/* split by delimeter ';' */
	sections = g_strsplit (package_id, ";", -1);
	if (g_strv_length (sections) != 4)
		goto out;

	/* name has to be valid */
	if (sections[0][0] != '\0')
		return sections;
out:
	g_strfreev (sections);
	return NULL;
}

/**
 * pk_package_id_check:
 * @package_id: the PackageID to check
 *
 * Return value: %TRUE if the PackageID was well formed.
 *
 * Since: 0.5.0
 **/
gboolean
pk_package_id_check (const gchar *package_id)
{
	gchar **sections;
	gboolean ret;

	/* NULL check */
	if (package_id == NULL)
		return FALSE;

	/* UTF8 */
	ret = g_utf8_validate (package_id, -1, NULL);
	if (!ret) {
		egg_warning ("invalid UTF8!");
		return FALSE;
	}

	/* correct number of sections */
	sections = pk_package_id_split (package_id);
	if (sections == NULL)
		return FALSE;

	/* all okay */
	g_strfreev (sections);
	return TRUE;
}

/**
 * pk_package_id_build:
 * @name: the package name
 * @version: the package version
 * @arch: the package architecture
 * @data: the package extra data
 *
 * Return value: returns a string to form the PackageID.
 *
 * Since: 0.5.0
 **/
gchar *
pk_package_id_build (const gchar *name, const gchar *version,
		     const gchar *arch, const gchar *data)
{
	g_return_val_if_fail (name != NULL, NULL);
	return g_strdup_printf ("%s;%s;%s;%s", name,
				version != NULL ? version : "",
				arch != NULL ? arch : "",
				data != NULL ? data : "");
}

/**
 * pk_arch_base_ix86:
 **/
static gboolean
pk_arch_base_ix86 (const gchar *arch)
{
	if (g_strcmp0 (arch, "i386") == 0 ||
	    g_strcmp0 (arch, "i486") == 0 ||
	    g_strcmp0 (arch, "i586") == 0 ||
	    g_strcmp0 (arch, "i686") == 0)
		return TRUE;
	return FALSE;
}

/**
 * pk_package_id_equal_fuzzy_arch_section:
 **/
static gboolean
pk_package_id_equal_fuzzy_arch_section (const gchar *arch1, const gchar *arch2)
{
	if (g_strcmp0 (arch1, arch2) == 0)
		return TRUE;
	if (pk_arch_base_ix86 (arch1) && pk_arch_base_ix86 (arch2))
		return TRUE;
	return FALSE;
}

/**
 * pk_package_id_equal_fuzzy_arch:
 * @package_id1: the first PackageID
 * @package_id2: the second PackageID
 *
 * Only compare the name, version, and arch, where the architecture will fuzzy
 * match with i*86.
 *
 * Return value: %TRUE if the PackageIDs can be considered equal.
 *
 * Since: 0.5.0
 **/
gboolean
pk_package_id_equal_fuzzy_arch (const gchar *package_id1, const gchar *package_id2)
{
	gchar **sections1;
	gchar **sections2;
	gboolean ret = FALSE;

	sections1 = pk_package_id_split (package_id1);
	sections2 = pk_package_id_split (package_id2);
	if (g_strcmp0 (sections1[0], sections2[0]) == 0 &&
	    g_strcmp0 (sections1[1], sections2[1]) == 0 &&
	    pk_package_id_equal_fuzzy_arch_section (sections1[2], sections2[2]))
		ret = TRUE;

	g_strfreev (sections1);
	g_strfreev (sections2);
	return ret;
}

/**
 * pk_package_id_to_printable:
 * @package_id: the PackageID
 *
 * Formats the PackageID to be printable to the user.
 *
 * Return value: the name-version.arch formatted string, use g_free() to free.
 *
 * Since: 0.5.2
 **/
gchar *
pk_package_id_to_printable (const gchar *package_id)
{
	gchar **parts = NULL;
	gchar *value = NULL;
	GString *string = NULL;

	/* invalid */
	if (package_id == NULL)
		goto out;

	/* split */
	parts = pk_package_id_split (package_id);
	if (parts == NULL)
		goto out;

	/* name */
	string = g_string_new (parts[PK_PACKAGE_ID_NAME]);

	/* version if present */
	if (parts[PK_PACKAGE_ID_VERSION][0] != '\0')
		g_string_append_printf (string, "-%s", parts[PK_PACKAGE_ID_VERSION]);

	/* arch if present */
	if (parts[PK_PACKAGE_ID_ARCH][0] != '\0')
		g_string_append_printf (string, ".%s", parts[PK_PACKAGE_ID_ARCH]);
out:
	if (string != NULL)
		value = g_string_free (string, FALSE);
	g_strfreev (parts);
	return value;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_package_id_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	gboolean ret;
	gchar *text;
	gchar **sections;

	if (!egg_test_start (test, "PkPackageId"))
		return;

	/************************************************************/
	egg_test_title (test, "check not valid - NULL");
	ret = pk_package_id_check (NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "check not valid - no name");
	ret = pk_package_id_check (";0.0.1;i386;fedora");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "check not valid - invalid");
	ret = pk_package_id_check ("moo;0.0.1;i386");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "check valid");
	ret = pk_package_id_check ("moo;0.0.1;i386;fedora");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "id build");
	text = pk_package_id_build ("moo", "0.0.1", "i386", "fedora");
	if (g_strcmp0 (text, "moo;0.0.1;i386;fedora") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);
	g_free (text);

	egg_test_title (test, "id build partial");
	text = pk_package_id_build ("moo", NULL, NULL, NULL);
	if (g_strcmp0 (text, "moo;;;") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got '%s', expected '%s'", text, "moo;;;");
	g_free (text);

	/************************************************************/
	egg_test_title (test, "test printable");
	text = pk_package_id_to_printable ("moo;0.0.1;i386;fedora");
	if (g_strcmp0 (text, "moo-0.0.1.i386") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "printable is '%s'", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "test printable no arch");
	text = pk_package_id_to_printable ("moo;0.0.1;;");
	if (g_strcmp0 (text, "moo-0.0.1") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "printable is '%s'", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "test printable just name");
	text = pk_package_id_to_printable ("moo;;;");
	if (g_strcmp0 (text, "moo") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "printable is '%s'", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "test on real packageid");
	sections = pk_package_id_split ("kde-i18n-csb;4:3.5.8~pre20071001-0ubuntu1;all;");
	if (sections != NULL &&
	    g_strcmp0 (sections[0], "kde-i18n-csb") == 0 &&
	    g_strcmp0 (sections[1], "4:3.5.8~pre20071001-0ubuntu1") == 0 &&
	    g_strcmp0 (sections[2], "all") == 0 &&
	    g_strcmp0 (sections[3], "") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %s, %s, %s, %s", sections[0], sections[1], sections[2], sections[3]);
	g_strfreev (sections);

	/************************************************************/
	egg_test_title (test, "test on short packageid");
	sections = pk_package_id_split ("kde-i18n-csb;4:3.5.8~pre20071001-0ubuntu1;;");
	if (sections != NULL &&
	    g_strcmp0 (sections[0], "kde-i18n-csb") == 0 &&
	    g_strcmp0 (sections[1], "4:3.5.8~pre20071001-0ubuntu1") == 0 &&
	    g_strcmp0 (sections[2], "") == 0 &&
	    g_strcmp0 (sections[3], "") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %s, %s, %s, %s", sections[0], sections[1], sections[2], sections[3]);
	g_strfreev (sections);

	/************************************************************/
	egg_test_title (test, "test fail under");
	sections = pk_package_id_split ("foo;moo");
	egg_test_assert (test, sections == NULL);

	/************************************************************/
	egg_test_title (test, "test fail over");
	sections = pk_package_id_split ("foo;moo;dave;clive;dan");
	egg_test_assert (test, sections == NULL);

	/************************************************************/
	egg_test_title (test, "test fail missing first");
	sections = pk_package_id_split (";0.1.2;i386;data");
	egg_test_assert (test, sections == NULL);

	egg_test_end (test);
}
#endif

