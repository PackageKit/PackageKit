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

#include "nix-helpers.hh"

// find drv based on attrpath and system
DrvInfo
nix_find_drv (EvalState & state, DrvInfos drvs, gchar* package_id)
{
	gchar** package_id_parts = pk_package_id_split (package_id);

	// string name (package_id_parts[0]);
	// string version (package_id_parts[1]);
	string system (package_id_parts[2]);
	string attrPath (package_id_parts[3]);

	for (auto drv : drvs)
		if (drv.attrPath == attrPath && drv.querySystem() == system)
			return drv;

	DrvInfo drv (state);
	return drv;
}

// generate package id from derivation
gchar*
nix_drv_package_id (DrvInfo & drv)
{
	DrvName name (drv.queryName());

	return pk_package_id_build (
		name.name.c_str (),
		name.version.c_str (),
		drv.querySystem().c_str (),
		drv.attrPath.c_str ()
	);
}

// get all drvs from list of ids
DrvInfos
nix_get_drvs_from_ids (EvalState & state, DrvInfos drvs, gchar** package_ids)
{
	DrvInfos _drvs;

	for (; *package_ids != NULL; package_ids++)
		_drvs.push_back (
			nix_find_drv (
				state,
				drvs,
				*package_ids
			)
		);

	return _drvs;
}

// return false if drvinfo doesn't conflicts with a filter
bool
nix_filter_drv (EvalState & state, DrvInfo & drv, const Settings & settings, PkBitfield filters)
{
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_VISIBLE) || pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_VISIBLE))
		if (!drv.hasFailed ())
		{
			if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_VISIBLE))
				return FALSE;
		}
		else
		{
			if (pk_bitfield_contain (filters, PK_FILTER_ENUM_VISIBLE))
				return FALSE;
		}

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_ARCH) || pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_ARCH))
		if (drv.querySystem() == settings.thisSystem)
		{
			if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_ARCH))
				return FALSE;
		}
		else
		{
			if (pk_bitfield_contain (filters, PK_FILTER_ENUM_ARCH))
				return FALSE;
		}

	return TRUE;
}

// get current state
EvalState*
nix_get_state ()
{
	auto store = openStore ();
	Strings searchPath;
	return new EvalState (searchPath, store);
}

// get all derivations
// TODO: figure out how to speed this up
DrvInfos
nix_get_all_derivations (EvalState & state, const Path & homedir)
{
	Value v;
	loadSourceExpr (state, homedir + "/.nix-defexpr", v);

	Bindings & bindings(*state.allocBindings(0));

	DrvInfos drvs;
	getDerivations (state, v, "", bindings, drvs, true);

	return drvs;
}

// get current nix profile frmo job's uid
Path
nix_get_profile (PkBackendJob* job)
{
	guint uid = pk_backend_job_get_uid (job);

	struct passwd* uid_ent = NULL;
	if ((uid_ent = getpwuid (uid)) == NULL)
		g_error ("Failed to get HOME");

	string homedir (uid_ent->pw_dir);

	return homedir + "/.nix-profile";
}

// run func in a thread
void
pk_nix_run (PkBackendJob *job, PkStatusEnum status, PkBackendJobThreadFunc func, gpointer data)
{
	g_return_if_fail (func != NULL);

	pk_backend_job_set_percentage (job, 0);
	pk_backend_job_set_allow_cancel (job, TRUE);
	pk_backend_job_set_status (job, status);
	pk_backend_job_set_started (job, TRUE);

	pk_backend_job_thread_create (job, func, data, NULL);
}

// emit an error if error not NULL
void
pk_nix_error_emit (PkBackendJob* job, GError* error)
{
	PkErrorEnum code = PK_ERROR_ENUM_UNKNOWN;
	g_return_if_fail (error != NULL);
	pk_backend_job_error_code (job, code, "%s", error->message);
}

// finish running job
gboolean
pk_nix_finish (PkBackendJob* job, GError* error)
{
	if (error != NULL) {
		pk_nix_error_emit (job, error);
		return FALSE;
	}

	pk_backend_job_set_percentage (job, 100);
	pk_backend_job_finished (job);

	return TRUE;
}
