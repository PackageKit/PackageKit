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
#include <pk-bitfield.h>

#include <poldek/pkg.h>

G_BEGIN_DECLS

PkBitfield pk_poldek_filters_supported (void);
gboolean   pk_poldek_pkg_is_devel (const struct pkg *pkg);
gboolean   pk_poldek_pkg_is_native (const struct pkg *pkg);
gboolean   pk_poldek_filters_accept (PkBitfield	       filters,
				     const struct pkg *pkg);
void	   pk_poldek_filter_newest (GPtrArray *pkgs,
				    gboolean   newest);

G_END_DECLS
