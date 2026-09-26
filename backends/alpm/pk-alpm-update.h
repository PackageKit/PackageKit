/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2020 Carson Black <uhhadd@gmail.com>
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

#include <glib/gstdio.h>
#include <alpm.h>
#include <pk-backend.h>

gboolean pk_alpm_refresh_databases (PkBackendJob *job, gint force,
					alpm_list_t *dbs, GError **error);

alpm_pkg_t *
pk_alpm_pkg_replaces (alpm_db_t *db, alpm_pkg_t *pkg);
