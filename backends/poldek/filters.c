/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2012 Marcin Banasiak <megabajt@pld-linux.org>
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

/* The package filters that depend on a single package, and newest. */

#include <pk-enum.h>

#include <poldek/pkgcmp.h>

#include "filters.h"

PkBitfield
pk_poldek_filters_supported (void)
{
	return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
				       PK_FILTER_ENUM_DEVELOPMENT,
				       PK_FILTER_ENUM_NEWEST,
				       PK_FILTER_ENUM_ARCH,
				       -1);
}

/* by name only: PLD's Development groups also have interpreters and tools such as python3 and git */
gboolean
pk_poldek_pkg_is_devel (const struct pkg *pkg)
{
	const gchar *suffixes[] = { "-devel", "-static", "-debuginfo", "-debugsource" };

	for (guint i = 0; i < G_N_ELEMENTS (suffixes); i++) {
		if (g_str_has_suffix (pkg->name, suffixes[i]))
			return TRUE;
	}
	return FALSE;
}

/* rpm scores the machine's own arch 1, compatible ones higher, foreign ones 0 */
gboolean
pk_poldek_pkg_is_native (const struct pkg *pkg)
{
	return g_strcmp0 (pkg_arch (pkg), "noarch") == 0 || pkg_arch_score (pkg) == 1;
}

/* The filters that look at one package at a time; installed and newest are not among them. */
gboolean
pk_poldek_filters_accept (PkBitfield filters, const struct pkg *pkg)
{
	struct
	{
		PkFilterEnum yes;
		PkFilterEnum no;
		gboolean (*test) (const struct pkg *pkg);
	} tests[] = {
		/* clang-format off */
		{ PK_FILTER_ENUM_DEVELOPMENT, PK_FILTER_ENUM_NOT_DEVELOPMENT, pk_poldek_pkg_is_devel },
		{ PK_FILTER_ENUM_ARCH, PK_FILTER_ENUM_NOT_ARCH, pk_poldek_pkg_is_native },
		/* clang-format on */
	};

	for (guint i = 0; i < G_N_ELEMENTS (tests); i++) {
		gboolean want = pk_bitfield_contain (filters, tests[i].yes);
		gboolean want_not = pk_bitfield_contain (filters, tests[i].no);
		gboolean is;

		if (!want && !want_not)
			continue;
		is = tests[i].test (pkg);
		if ((want && !is) || (want_not && is))
			return FALSE;
	}
	return TRUE;
}

/*
 * Keeps only the highest version of each name and arch in @pkgs, an array of
 * struct pkg, or with @newest FALSE all but that one, without changing the
 * order of what is kept.
 */
void
pk_poldek_filter_newest (GPtrArray *pkgs, gboolean newest)
{
	g_autoptr(GHashTable) best = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	guint kept = 0;

	for (guint i = 0; i < pkgs->len; i++) {
		struct pkg *pkg = g_ptr_array_index (pkgs, i);
		gchar *key = g_strdup_printf ("%s;%s", pkg->name, pkg_arch (pkg));
		struct pkg *other = g_hash_table_lookup (best, key);

		if (other == NULL || pkg_cmp_evr (pkg, other) > 0)
			g_hash_table_replace (best, key, pkg);
		else
			g_free (key);
	}
	for (guint i = 0; i < pkgs->len; i++) {
		struct pkg *pkg = g_ptr_array_index (pkgs, i);
		g_autofree gchar *key = g_strdup_printf ("%s;%s", pkg->name, pkg_arch (pkg));

		if ((g_hash_table_lookup (best, key) == pkg) == newest)
			pkgs->pdata[kept++] = pkg;
	}
	g_ptr_array_set_size (pkgs, kept);
}
