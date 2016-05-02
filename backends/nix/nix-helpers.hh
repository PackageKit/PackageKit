/* -*- Mode: C; tab-width: 8; indent-tab-modes: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Matthew Bauer <mjbauer95@gmail.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed i3n the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef NIX_HELPERS_HH
#define NIX_HELPERS_HH

#include <pwd.h>
#include <glib.h>

#include <pk-backend.h>
#include <pk-backend-job.h>

#include "nix-lib-plus.hh"

void
pk_nix_run (PkBackendJob *job, PkStatusEnum status, PkBackendJobThreadFunc func, gpointer data);

void
pk_nix_error_emit (PkBackendJob* job, GError* error);

gboolean
pk_nix_finish (PkBackendJob* job, GError* error);

DrvInfos
nix_get_drvs_from_ids (EvalState & state, DrvInfos drvs, gchar** package_ids);

EvalState*
nix_get_state ();

DrvInfos
nix_get_all_derivations (EvalState & state, const Path & path);

gchar*
nix_drv_package_id (DrvInfo & drv);

DrvInfo
nix_find_drv (EvalState & state, DrvInfos drvs, gchar* package_id);

bool
nix_filter_drv (EvalState & state, DrvInfo & drv, const Settings & settings, PkBitfield filters);

Path
nix_get_profile (PkBackendJob* job);

#endif
