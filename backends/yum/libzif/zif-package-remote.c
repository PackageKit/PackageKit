/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:zif-package-remote
 * @short_description: Remote package object
 *
 * This object is a subclass of #ZifPackage
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "egg-debug.h"

#include "zif-utils.h"
#include "zif-package-remote.h"
#include "zif-groups.h"
#include "zif-string.h"

#define ZIF_PACKAGE_REMOTE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_PACKAGE_REMOTE, ZifPackageRemotePrivate))

/**
 * ZifPackageRemotePrivate:
 *
 * Private #ZifPackageRemote data
 **/
struct _ZifPackageRemotePrivate
{
	ZifGroups		*groups;
	gchar			*sql_id;
};

G_DEFINE_TYPE (ZifPackageRemote, zif_package_remote, ZIF_TYPE_PACKAGE)

/**
 * zif_package_remote_set_from_repo:
 * @pkg: the #ZifPackageRemote object
 * @length: length of data and type arrays
 * @type: data type array
 * @data: data value array
 * @repo_id: the repository id
 * @error: a #GError which is used on failure, or %NULL
 *
 * Sets details on a remote package from repo data derived from the metadata xml.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.0.1
 **/
gboolean
zif_package_remote_set_from_repo (ZifPackageRemote *pkg, guint length, gchar **type, gchar **data, const gchar *repo_id, GError **error)
{
	guint i;
	const gchar *name = NULL;
	const gchar *epoch = NULL;
	const gchar *version = NULL;
	const gchar *release = NULL;
	const gchar *arch = NULL;
	gchar *package_id;
	ZifString *string;

	g_return_val_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg), FALSE);
	g_return_val_if_fail (type != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get the ID */
	for (i=0; i<length; i++) {
		if (g_strcmp0 (type[i], "name") == 0) {
			name = data[i];
		} else if (g_strcmp0 (type[i], "epoch") == 0) {
			epoch = data[i];
		} else if (g_strcmp0 (type[i], "version") == 0) {
			version = data[i];
		} else if (g_strcmp0 (type[i], "release") == 0) {
			release = data[i];
		} else if (g_strcmp0 (type[i], "arch") == 0) {
			arch = data[i];
		} else if (g_strcmp0 (type[i], "summary") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_summary (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "description") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_description (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "url") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_url (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "rpm_license") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_license (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "rpm_group") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_category (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "size_package") == 0) {
			zif_package_set_size (ZIF_PACKAGE (pkg), atoi (data[i]));
		} else if (g_strcmp0 (type[i], "pkgId") == 0) {
			pkg->priv->sql_id = g_strdup (data[i]);
		} else if (g_strcmp0 (type[i], "location_href") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_location_href (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else {
			egg_warning ("unrecognized: %s=%s", type[i], data[i]);
		}
	}
	zif_package_set_installed (ZIF_PACKAGE (pkg), FALSE);
	package_id = zif_package_id_from_nevra (name, epoch, version, release, arch, repo_id);
	zif_package_set_id (ZIF_PACKAGE (pkg), package_id);
	g_free (package_id);
	return TRUE;
}

/**
 * zif_package_remote_finalize:
 **/
static void
zif_package_remote_finalize (GObject *object)
{
	ZifPackageRemote *pkg;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_PACKAGE_REMOTE (object));
	pkg = ZIF_PACKAGE_REMOTE (object);

	g_free (pkg->priv->sql_id);
	g_object_unref (pkg->priv->groups);

	G_OBJECT_CLASS (zif_package_remote_parent_class)->finalize (object);
}

/**
 * zif_package_remote_class_init:
 **/
static void
zif_package_remote_class_init (ZifPackageRemoteClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_package_remote_finalize;
	g_type_class_add_private (klass, sizeof (ZifPackageRemotePrivate));
}

/**
 * zif_package_remote_init:
 **/
static void
zif_package_remote_init (ZifPackageRemote *pkg)
{
	pkg->priv = ZIF_PACKAGE_REMOTE_GET_PRIVATE (pkg);
	pkg->priv->sql_id = NULL;
	pkg->priv->groups = zif_groups_new ();
}

/**
 * zif_package_remote_new:
 *
 * Return value: A new #ZifPackageRemote class instance.
 *
 * Since: 0.0.1
 **/
ZifPackageRemote *
zif_package_remote_new (void)
{
	ZifPackageRemote *pkg;
	pkg = g_object_new (ZIF_TYPE_PACKAGE_REMOTE, NULL);
	return ZIF_PACKAGE_REMOTE (pkg);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_package_remote_test (EggTest *test)
{
	ZifPackageRemote *pkg;

	if (!egg_test_start (test, "ZifPackageRemote"))
		return;

	/************************************************************/
	egg_test_title (test, "get package_remote");
	pkg = zif_package_remote_new ();
	egg_test_assert (test, pkg != NULL);

	g_object_unref (pkg);

	egg_test_end (test);
}
#endif

