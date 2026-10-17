/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Package ids: name;[epoch:]version-release;arch;origin;data where
 *  - an available package has the name of its source as origin,
 *  - an installed package has no origin and "installed" as data,
 *  - a package file has the origin "local", as PackageKit's concepts say.
 * The data field keeps installed and available ids of the same NEVRA apart.
 * A "local" id is looked up like any other available one: it finds the
 * packages of a source named local, and never a file of an install-files job.
 */

#include <string.h>

#include <pk-package-id.h>

#include <poldek/pkgcmp.h>
#include <poldek/pkgdir/pkgdir.h>

#include "job.h"
#include "pkgid.h"

#define PK_POLDEK_PKGID_INSTALLED "installed"

gchar *
pk_poldek_pkg_evr (const struct pkg *pkg)
{
	if (pkg->epoch > 0)
		return g_strdup_printf ("%d:%s-%s", pkg->epoch, pkg->ver, pkg->rel);
	return g_strdup_printf ("%s-%s", pkg->ver, pkg->rel);
}

/* The origin in the id of an available package: the name of its source, in UTF-8 as ids are. */
gchar *
pk_poldek_pkg_origin (const struct pkg *pkg)
{
	g_assert (pkg->pkgdir != NULL && pkg->pkgdir->name != NULL);
	if (strcmp (pkg->pkgdir->name, PK_POLDEK_LOCAL_SOURCE) == 0)
		return g_strdup (PK_POLDEK_ORIGIN_LOCAL);
	return g_utf8_make_valid (pkg->pkgdir->name, -1);
}

gchar *
pk_poldek_pkgid_build (const struct pkg *pkg, PkPoldekPkgKind kind)
{
	g_autofree gchar *evr = pk_poldek_pkg_evr (pkg);
	g_autofree gchar *origin = NULL;
	g_autofree gchar *id = NULL;
	const gchar *data = "";

	switch (kind) {
	case PK_POLDEK_PKG_AVAILABLE:
		origin = pk_poldek_pkg_origin (pkg);
		break;
	case PK_POLDEK_PKG_INSTALLED:
		data = PK_POLDEK_PKGID_INSTALLED;
		break;
	}
	id = pk_package_id_build (pkg->name,
				  evr,
				  pkg_arch (pkg),
				  origin != NULL ? origin : "",
				  data);
	/* package headers have no encoding, ids are UTF-8 */
	return g_utf8_make_valid (id, -1);
}

gboolean
pk_poldek_pkgid_parse (const gchar *package_id, PkPoldekPkgId *id, GError **error)
{
	gchar **split = pk_package_id_split (package_id);
	const gchar *origin;
	const gchar *data;

	if (split == NULL || split[PK_PACKAGE_ID_NAME][0] == '\0' ||
	    split[PK_PACKAGE_ID_VERSION][0] == '\0' || split[PK_PACKAGE_ID_ARCH][0] == '\0') {
		g_strfreev (split);
		g_set_error (error,
			     PK_POLDEK_ERROR,
			     PK_ERROR_ENUM_PACKAGE_ID_INVALID,
			     "invalid package id '%s'",
			     package_id);
		return FALSE;
	}
	origin = split[PK_PACKAGE_ID_ORIGIN];
	data = split[PK_PACKAGE_ID_DATA];
	if (origin[0] == '\0' && g_strcmp0 (data, PK_POLDEK_PKGID_INSTALLED) == 0) {
		id->kind = PK_POLDEK_PKG_INSTALLED;
	} else if (origin[0] != '\0' && data[0] == '\0') {
		id->kind = PK_POLDEK_PKG_AVAILABLE;
	} else {
		g_strfreev (split);
		g_set_error (error,
			     PK_POLDEK_ERROR,
			     PK_ERROR_ENUM_PACKAGE_ID_INVALID,
			     "package id '%s' is not one of this backend's",
			     package_id);
		return FALSE;
	}
	id->split = split;
	return TRUE;
}

void
pk_poldek_pkgid_clear (PkPoldekPkgId *id)
{
	g_clear_pointer (&id->split, g_strfreev);
}

/* Whether @a and @b are the same name, version and arch, as different objects can be. */
gboolean
pk_poldek_pkg_same_nevra (const struct pkg *a, const struct pkg *b)
{
	return strcmp (a->name, b->name) == 0 && pkg_cmp_evr (a, b) == 0 &&
	       pkg_cmp_arch (a, b) == 0;
}

/* Whether @pkg has the name, version and arch of @id; the origin is the caller's business. */
gboolean
pk_poldek_pkgid_matches (const PkPoldekPkgId *id, const struct pkg *pkg)
{
	g_autofree gchar *evr = NULL;

	if (strcmp (pkg->name, id->split[PK_PACKAGE_ID_NAME]) != 0 ||
	    g_strcmp0 (pkg_arch (pkg), id->split[PK_PACKAGE_ID_ARCH]) != 0)
		return FALSE;
	evr = pk_poldek_pkg_evr (pkg);
	return strcmp (evr, id->split[PK_PACKAGE_ID_VERSION]) == 0;
}
