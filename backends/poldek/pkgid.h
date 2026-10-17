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

#pragma once

#include <glib.h>

#include <poldek/pkg.h>

G_BEGIN_DECLS

/* the source install-files adds for the files it is given; its packages have origin local */
#define PK_POLDEK_LOCAL_SOURCE "pk-local"
#define PK_POLDEK_ORIGIN_LOCAL "local"

/* the kind of package a package id names, from its origin and data fields */
typedef enum {
	PK_POLDEK_PKG_AVAILABLE,
	PK_POLDEK_PKG_INSTALLED,
} PkPoldekPkgKind;

typedef struct
{
	gchar	      **split;
	PkPoldekPkgKind kind;
} PkPoldekPkgId;

gchar	*pk_poldek_pkg_evr (const struct pkg *pkg);
gchar	*pk_poldek_pkg_origin (const struct pkg *pkg);
gchar	*pk_poldek_pkgid_build (const struct pkg *pkg,
				PkPoldekPkgKind	  kind);
gboolean pk_poldek_pkgid_parse (const gchar   *package_id,
				PkPoldekPkgId *id,
				GError	     **error);
void	 pk_poldek_pkgid_clear (PkPoldekPkgId *id);
gboolean pk_poldek_pkgid_matches (const PkPoldekPkgId *id,
				  const struct pkg    *pkg);
gboolean pk_poldek_pkg_same_nevra (const struct pkg *a,
				   const struct pkg *b);

G_END_DECLS
