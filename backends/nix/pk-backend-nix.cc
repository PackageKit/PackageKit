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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>
#include <stdlib.h>
#include <gio/gio.h>

#include "nix-helpers.hh"
#include "nix-lib-plus.hh"

typedef struct {
	Path roothome;
} PkBackendNixPrivate;

static PkBackendNixPrivate* priv;
static EvalState* state;
static DrvInfos drvs;

void
pk_backend_initialize (GKeyFile* conf, PkBackend* backend)
{
	g_debug ("backend initalize start");

	priv = g_new0 (PkBackendNixPrivate, 1);

	struct passwd* uid_ent = NULL;
	if ((uid_ent = getpwuid (getuid ())) == NULL)
		g_error ("Failed to get HOME");
	priv->roothome = uid_ent->pw_dir;

	verbosity = (Verbosity) -1;

	try
	{
		initNix();
		initGC();

		state = nix_get_state();
	}
	catch (std::exception & e)
	{
	}
}

void
pk_backend_destroy (PkBackend* backend)
{
	drvs.empty ();
	g_free (state);
	g_free (priv);
}

gboolean
pk_backend_supports_parallelization (PkBackend* backend)
{
	return TRUE;
}

const gchar *
pk_backend_get_description (PkBackend* backend)
{
	return "Nix - the purely functional package manager";
}

const gchar *
pk_backend_get_author (PkBackend* backend)
{
	return "Matthew Bauer <mjbauer95@gmail.com>";
}

// TODO
PkBitfield
pk_backend_get_groups (PkBackend* backend)
{
	return 0;
}

PkBitfield
pk_backend_get_filters (PkBackend* backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_VISIBLE,
		PK_FILTER_ENUM_NOT_VISIBLE,
		PK_FILTER_ENUM_ARCH,
		PK_FILTER_ENUM_NOT_ARCH,
		PK_FILTER_ENUM_SUPPORTED,
		PK_FILTER_ENUM_NOT_SUPPORTED,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_NOT_INSTALLED,

		/* TODO:

		 - breaks with unfree packages
		   PK_FILTER_ENUM_DOWNLOADED,
		   PK_FILTER_ENUM_NOT_DOWNLOADED,
		   PK_FILTER_ENUM_FREE,
		   PK_FILTER_ENUM_NOT_FREE,

		 - others
		   PK_FILTER_ENUM_NONE,
		   PK_FILTER_ENUM_DEVELOPMENT,
		   PK_FILTER_ENUM_NOT_DEVELOPMENT,
		   PK_FILTER_ENUM_GUI,
		   PK_FILTER_ENUM_NOT_GUI,
		   PK_FILTER_ENUM_BASENAME,
		   PK_FILTER_ENUM_NOT_BASENAME,
		   PK_FILTER_ENUM_NEWEST,
		   PK_FILTER_ENUM_NOT_NEWEST,
		   PK_FILTER_ENUM_SOURCE,
		   PK_FILTER_ENUM_NOT_SOURCE,
		   PK_FILTER_ENUM_COLLECTIONS,
		   PK_FILTER_ENUM_NOT_COLLECTIONS,
		   PK_FILTER_ENUM_APPLICATION,
		   PK_FILTER_ENUM_NOT_APPLICATION,
		*/

		-1
	);
}

PkBitfield
pk_backend_get_roles (PkBackend* backend)
{
	return pk_bitfield_from_enums (
		PK_ROLE_ENUM_CANCEL,
		PK_ROLE_ENUM_DOWNLOAD_PACKAGES,
		PK_ROLE_ENUM_GET_DETAILS,
		PK_ROLE_ENUM_GET_PACKAGES,
		PK_ROLE_ENUM_GET_UPDATES,
		PK_ROLE_ENUM_INSTALL_PACKAGES,
		PK_ROLE_ENUM_REFRESH_CACHE,
		PK_ROLE_ENUM_REMOVE_PACKAGES,
		PK_ROLE_ENUM_RESOLVE,
		PK_ROLE_ENUM_SEARCH_DETAILS,
		PK_ROLE_ENUM_SEARCH_NAME,

		/* TODO
		 - need to use binary cache db
		   PK_ROLE_ENUM_SEARCH_FILE,
		   PK_ROLE_ENUM_GET_FILES_LOCAL,
		   PK_ROLE_ENUM_GET_FILES,

		 - need to get data from repos through nix-channel
		   PK_ROLE_ENUM_REPO_ENABLE,
		   PK_ROLE_ENUM_GET_REPO_LIST,
		   PK_ROLE_ENUM_REPO_REMOVE,
		   PK_ROLE_ENUM_REPO_SET_DATA,

		 - need to integrate with nixos
		   PK_ROLE_ENUM_REPAIR_SYSTEM,
		   PK_ROLE_ENUM_UPGRADE_SYSTEM,
		   PK_ROLE_ENUM_GET_DISTRO_UPGRADES,

		 - etc.
		   PK_ROLE_ENUM_DEPENDS_ON,
		   PK_ROLE_ENUM_REQUIRED_BY,
		   PK_ROLE_ENUM_INSTALL_FILES,
		   PK_ROLE_ENUM_INSTALL_SIGNATURE,
		   PK_ROLE_ENUM_SEARCH_GROUP,
		   PK_ROLE_ENUM_WHAT_PROVIDES,
		   PK_ROLE_ENUM_ACCEPT_EULA,
		   PK_ROLE_ENUM_GET_CATEGORIES,
		   PK_ROLE_ENUM_GET_OLD_TRANSACTIONS,
		   PK_ROLE_ENUM_WHAT_PROVIDES,
		   PK_ROLE_ENUM_GET_UPDATE_DETAIL,
		   PK_ROLE_ENUM_GET_DETAILS_LOCAL,
		*/
		-1
	);
}

gchar **
pk_backend_get_mime_types (PkBackend* backend)
{
	const gchar* mime_types[] = { "application/nix-package", NULL };
	return g_strdupv ((gchar **) mime_types);
}

static void
pk_backend_get_details_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	g_autoptr (GError) error = NULL;

	try
	{
		// possibly slow call
		if (drvs.empty ())
			drvs = nix_get_all_derivations (*state, priv->roothome);

		DrvInfos _drvs = nix_get_drvs_from_ids (*state, drvs, (gchar**) p);

		for (auto drv : _drvs)
		{
			if (pk_backend_job_is_cancelled (job))
				break;

			string license = "unknown";
			auto licenseMeta = drv.queryMeta ("license");
			if (licenseMeta != NULL && licenseMeta->type == tAttrs)
			{
				auto symbol = state->symbols.create ("fullName");
				Bindings::iterator fullName = licenseMeta->attrs->find (symbol);
				if (fullName != licenseMeta->attrs->end () && fullName->value->type == tString)
					license = fullName->value->string.s;
			}

			int narSize = 0;
			if (drv.queryOutPath () != "")
				narSize = state->store->queryPathInfo (drv.queryDrvPath ()).narSize;

			string longDescription = drv.queryMetaString ("longDescription");
			if (longDescription == "")
				longDescription = drv.queryMetaString ("description");

			pk_backend_job_details (
				job,
				nix_drv_package_id (drv),
				drv.queryMetaString ("description").c_str (),
				license.c_str(),
				PK_GROUP_ENUM_UNKNOWN, // TODO: hack in group support
				longDescription.c_str (),
				drv.queryMetaString ("homepage").c_str (),
				narSize
			);
		}
	}
	catch (std::exception & e)
	{
	}

	pk_nix_finish (job, error);
}

void
pk_backend_get_details (PkBackend* backend, PkBackendJob* job, gchar** packages)
{
	pk_nix_run (job, PK_STATUS_ENUM_INFO, pk_backend_get_details_thread, packages);
}

static void
pk_backend_get_packages_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	g_autoptr (GError) error = NULL;

	PkBitfield filters;
	g_variant_get (params, "(t)", &filters);

	try
	{
		// possibly slow call
		if (drvs.empty ())
			drvs = nix_get_all_derivations (*state, priv->roothome);

		auto profile = nix_get_profile (job);
		DrvInfos installedDrvs = queryInstalled (*state, profile);

		int n = 0;
		double percentFactor = 100 / drvs.size ();

		for (auto drv : drvs)
		{
			if (pk_backend_job_is_cancelled (job))
				break;

			pk_backend_job_set_percentage (job, (n++) * percentFactor);

			if (!nix_filter_drv (*state, drv, settings, filters))
				continue;

			auto info = PK_INFO_ENUM_AVAILABLE;

			for (auto _drv : installedDrvs)
				if (_drv.name == drv.name)
				{
					info = PK_INFO_ENUM_INSTALLED;
					break;
				}

			if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) && info != PK_INFO_ENUM_INSTALLED)
				continue;

			if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) && info == PK_INFO_ENUM_INSTALLED)
				continue;

			pk_backend_job_package (
				job,
				info,
				nix_drv_package_id (drv),
				drv.queryMetaString ("description").c_str ()
			);
		}
	}
	catch (std::exception & e)
	{
	}

	pk_nix_finish (job, error);
}

void
pk_backend_get_packages (PkBackend* backend, PkBackendJob* job, PkBitfield filters)
{
	pk_nix_run (job, PK_STATUS_ENUM_GENERATE_PACKAGE_LIST, pk_backend_get_packages_thread, NULL);
}

static void
pk_backend_resolve_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	g_autoptr (GError) error = NULL;

	const gchar **search;
	PkBitfield filters;
	g_variant_get (params, "(t^a&s)", &filters, &search);

	try
	{
		// possibly slow call
		if (drvs.empty ())
			drvs = nix_get_all_derivations (*state, priv->roothome);

		auto profile = nix_get_profile (job);
		DrvInfos installedDrvs = queryInstalled (*state, profile);

		for (; *search != NULL; ++search)
		{
			if (pk_backend_job_is_cancelled (job))
				break;

			DrvName searchName (*search);

			for (auto drv : drvs)
			{
				DrvName drvName (drv.name);
				if (searchName.matches (drvName))
				{
					if (!nix_filter_drv (*state, drv, settings, filters))
						continue;

					auto info = PK_INFO_ENUM_AVAILABLE;

					for (auto _drv : installedDrvs)
						if (_drv.name == drv.name)
						{
							info = PK_INFO_ENUM_INSTALLED;
							break;
						}

					if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) && info != PK_INFO_ENUM_INSTALLED)
						continue;

					if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) && info == PK_INFO_ENUM_INSTALLED)
						continue;

					pk_backend_job_package (
						job,
						info,
						nix_drv_package_id (drv),
						drv.queryMetaString ("description").c_str ()
					);
				}
			}
		}
	}
	catch (std::exception & e)
	{
	}

	pk_nix_finish (job, error);
}

void
pk_backend_resolve (PkBackend* self, PkBackendJob* job, PkBitfield filters, gchar** search)
{
	pk_nix_run (job, PK_STATUS_ENUM_QUERY, pk_backend_resolve_thread, NULL);
}

static void
pk_backend_search_names_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	g_autoptr (GError) error = NULL;

	gchar **search;
	PkBitfield filters;
	g_variant_get (params, "(t^a&s)", &filters, &search);

	try
	{
		// possibly slow call
		if (drvs.empty ())
			drvs = nix_get_all_derivations (*state, priv->roothome);

		auto profile = nix_get_profile (job);
		DrvInfos installedDrvs = queryInstalled (*state, profile);

		for (; *search != NULL; ++search)
		{
			if (pk_backend_job_is_cancelled (job))
				break;

			for (auto drv : drvs)
				if (drv.name.find(*search) != -1)
				{
					if (!nix_filter_drv (*state, drv, settings, filters))
						continue;

					auto info = PK_INFO_ENUM_AVAILABLE;

					for (auto _drv : installedDrvs)
						if (_drv.name == drv.name)
						{
							info = PK_INFO_ENUM_INSTALLED;
							break;
						}

					if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) && info != PK_INFO_ENUM_INSTALLED)
						continue;

					if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) && info == PK_INFO_ENUM_INSTALLED)
						continue;

					pk_backend_job_package (
						job,
						info,
						nix_drv_package_id (drv),
						drv.queryMetaString ("description").c_str ()
					);
				}
		}
	}
	catch (std::exception & e)
	{
	}

	pk_nix_finish (job, error);
}

void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_nix_run (job, PK_STATUS_ENUM_QUERY, pk_backend_search_names_thread, NULL);
}

static void
pk_backend_search_details_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	g_autoptr (GError) error = NULL;

	gchar **value;
	PkBitfield filters;
	g_variant_get (params, "(t^a&s)", &filters, &value);

	try
	{
		// possibly slow call
		if (drvs.empty ())
			drvs = nix_get_all_derivations (*state, priv->roothome);

		auto profile = nix_get_profile (job);
		DrvInfos installedDrvs = queryInstalled (*state, profile);

		for (; *value != NULL; ++value)
		{
			if (pk_backend_job_is_cancelled (job))
				break;

			for (auto drv : drvs)
				if (drv.queryMetaString ("description").find (*value) != -1)
				{
					if (!nix_filter_drv (*state, drv, settings, filters))
						continue;

					auto info = PK_INFO_ENUM_AVAILABLE;

					for (auto _drv : installedDrvs)
						if (_drv.name == drv.name)
						{
							info = PK_INFO_ENUM_INSTALLED;
							break;
						}

					if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) && info != PK_INFO_ENUM_INSTALLED)
						continue;

					if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) && info == PK_INFO_ENUM_INSTALLED)
						continue;

					pk_backend_job_package (
						job,
						info,
						nix_drv_package_id (drv),
						drv.queryMetaString ("description").c_str ()
					);
				}
		}
	}
	catch (std::exception & e)
	{
	}

	pk_nix_finish (job, error);
}

void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_nix_run (job, PK_STATUS_ENUM_QUERY, pk_backend_search_details_thread, NULL);
}

static void
pk_backend_refresh_cache_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	g_autoptr (GError) error = NULL;

	try
	{
		state = nix_get_state ();
		drvs = nix_get_all_derivations (*state, priv->roothome);
	}
	catch (std::exception & e)
	{
	}

	pk_nix_finish (job, error);
}

void
pk_backend_refresh_cache (PkBackend* backend, PkBackendJob* job, gboolean force)
{
	pk_nix_run (job, PK_STATUS_ENUM_REFRESH_CACHE, pk_backend_refresh_cache_thread, NULL);
}

static void
pk_backend_install_packages_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	g_autoptr (GError) error = NULL;

	PkBitfield flags;
	gchar** package_ids;

	g_variant_get (params, "(t^a&s)", &flags, &package_ids);

	try
	{
		// possibly slow call
		if (drvs.empty ())
			drvs = nix_get_all_derivations (*state, priv->roothome);

		DrvInfos newElems = nix_get_drvs_from_ids (*state, drvs, package_ids);

		for (auto drv : newElems)
		{
			pk_backend_job_package (
				job,
				PK_INFO_ENUM_INSTALLING,
				nix_drv_package_id (drv),
				drv.queryMetaString ("description").c_str ()
			);
		}

		Path profile = nix_get_profile (job);

		while (true)
		{
			if (pk_backend_job_is_cancelled (job))
				break;

			string lockToken = optimisticLockProfile (profile);
			DrvInfos allElems (newElems);

			DrvInfos installedElems = queryInstalled (*state, profile);

			for (auto & i : installedElems)
			{
				DrvName drvName(i.name);
				allElems.push_back (i);
			}

			if (createUserEnv (*state, allElems, profile, false, lockToken))
				break;
		}

		for (auto drv : newElems)
		{
			pk_backend_job_package (
				job,
				PK_INFO_ENUM_INSTALLED,
				nix_drv_package_id (drv),
				drv.queryMetaString ("description").c_str ()
			);
		}
	}
	catch (std::exception & e)
	{
	}

	pk_nix_finish (job, error);
}

void
pk_backend_install_packages (PkBackend* backend, PkBackendJob* job, PkBitfield transaction_flags, gchar** package_ids)
{
	pk_nix_run (job, PK_STATUS_ENUM_INSTALL, pk_backend_install_packages_thread, NULL);
}

static void
pk_backend_remove_packages_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	g_autoptr (GError) error = NULL;

	PkBitfield transaction_flags;
	gchar** package_ids;
	gboolean allow_deps, autoremove;
	g_variant_get (params, "(t^a&sbb)", &transaction_flags, &package_ids, &allow_deps, &autoremove);

	try
	{
		// possibly slow call
		if (drvs.empty ())
			drvs = nix_get_all_derivations(*state, priv->roothome);

		DrvInfos _drvs = nix_get_drvs_from_ids (*state, drvs, package_ids);

		for (auto drv : _drvs)
		{
			pk_backend_job_package (
				job,
				PK_INFO_ENUM_REMOVING,
				nix_drv_package_id (drv),
				drv.queryMetaString ("description").c_str ()
			);
		}

		Path profile = nix_get_profile (job);

		while (true)
		{
			if (pk_backend_job_is_cancelled (job))
				break;

			string lockToken = optimisticLockProfile (profile);

			DrvInfos installedElems = queryInstalled (*state, profile);
			DrvInfos newElems;

			for (auto & drv : installedElems)
			{
				bool found = false;

				for (auto & _drv : _drvs)
					if (drv.attrPath == _drv.attrPath)
					{
						found = true;
						break;
					}

				if (!found)
					newElems.push_back (drv);
			}

			if (createUserEnv (*state, newElems, profile, false, lockToken))
				break;
		}

		for (auto drv : _drvs)
		{
			pk_backend_job_package (
				job,
				PK_INFO_ENUM_AVAILABLE,
				nix_drv_package_id (drv),
				drv.queryMetaString ("description").c_str ()
			);
		}
	}
	catch (std::exception & e)
	{
	}

	pk_nix_finish (job, error);
}

void
pk_backend_remove_packages (PkBackend* backend, PkBackendJob* job, PkBitfield transaction_flags, gchar** package_ids, gboolean allow_deps, gboolean autoremove)
{
	pk_nix_run (job, PK_STATUS_ENUM_REMOVE, pk_backend_remove_packages_thread, NULL);
}

static void
pk_backend_update_packages_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	g_autoptr (GError) error = NULL;

	try
	{
		// possibly slow call
		if (drvs.empty ())
			drvs = nix_get_all_derivations (*state, priv->roothome);

		auto profile = nix_get_profile (job);

		while (true)
		{
			if (pk_backend_job_is_cancelled (job))
				break;

			string lockToken = optimisticLockProfile (profile);

			DrvInfos installedElems = queryInstalled (*state, profile);

			/* Go through all installed derivations. */
			DrvInfos newElems;
			for (auto & i : installedElems)
			{
				DrvName drvName (i.name);

				try
				{
					if (keep (i))
					{
						newElems.push_back (i);
						continue;
					}

					/* Find the derivation in the input Nix expression
					   with the same name that satisfies the version
					   constraints specified by upgradeType.  If there are
					   multiple matches, take the one with the highest
					   priority.  If there are still multiple matches,
					   take the one with the highest version.
					   Do not upgrade if it would decrease the priority. */
					DrvInfos::iterator bestElem = drvs.end ();
					string bestVersion;

					for (auto j = drvs.begin (); j != drvs.end (); ++j)
					{
						if (comparePriorities (*state, i, *j) > 0)
							continue;

						DrvName newName (j->name);
						if (newName.name == drvName.name)
						{
							int d = compareVersions (drvName.version, newName.version);
							if (d < 0)
							{
								int d2 = -1;
								if (bestElem != drvs.end ())
								{
									d2 = comparePriorities (*state, *bestElem, *j);
									if (d2 == 0)
										d2 = compareVersions (bestVersion, newName.version);
								}
								if (d2 < 0)
								{
									bestElem = j;
									bestVersion = newName.version;
								}
							}
						}
					}

					if (bestElem != drvs.end () && i.queryOutPath () != bestElem->queryOutPath ())
					{
						const char * action;
						auto _drv = *bestElem;
						if (compareVersions (drvName.version, bestVersion) <= 0)
						{
							pk_backend_job_package (
								job,
								PK_INFO_ENUM_UPDATING,
								nix_drv_package_id (_drv),
								_drv.queryMetaString ("description").c_str ()
							);

							action = "upgrading";
						}
						else
						{
							pk_backend_job_package (
								job,
								PK_INFO_ENUM_DOWNGRADING,
								nix_drv_package_id (_drv),
								_drv.queryMetaString ("description").c_str ()
							);

							action = "downgrading";
						}
						newElems.push_back (*bestElem);
					}
					else
						newElems.push_back (i);
				}
				catch (Error & e)
				{
					throw;
				}
			}

			if (createUserEnv (*state, newElems, profile, false, lockToken))
			{
				for (auto drv : newElems)
				{
					pk_backend_job_package (
						job,
						PK_INFO_ENUM_INSTALLED,
						nix_drv_package_id (drv),
						drv.queryMetaString ("description").c_str ()
					);
				}

				break;
			}
		}
	}
	catch (std::exception & e)
	{
	}

	pk_nix_finish (job, error);
}

void
pk_backend_update_packages (PkBackend* backend, PkBackendJob* job, PkBitfield transaction_flags, gchar** package_ids)
{
	pk_nix_run (job, PK_STATUS_ENUM_UPDATE, pk_backend_update_packages_thread, NULL);
}

static void
pk_backend_download_packages_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	g_autoptr (GError) error = NULL;

	gchar* directory;
	gchar** package_ids;
	g_variant_get (params, "(^a&ss)", &package_ids, &directory);

	try
	{
		// possibly slow call
		if (drvs.empty ())
			drvs = nix_get_all_derivations (*state, priv->roothome);

		DrvInfos _drvs = nix_get_drvs_from_ids (*state, drvs, package_ids);

		PathSet paths;
		for (auto drv : _drvs)
		{
			if (pk_backend_job_is_cancelled (job))
				break;

			pk_backend_job_package (
				job,
				PK_INFO_ENUM_DOWNLOADING,
				nix_drv_package_id (drv),
				drv.queryMetaString ("description").c_str ()
			);

			// should provide updates to status
			// just build one path at a time
			PathSet paths;
			paths.insert (drv.queryOutPath ());
			state->store->buildPaths (paths);
		}
	}
	catch (std::exception & e)
	{
	}

	pk_nix_finish (job, error);
}

void
pk_backend_download_packages (PkBackend* backend, PkBackendJob* job, gchar** package_ids, const gchar* directory)
{
	pk_nix_run (job, PK_STATUS_ENUM_DOWNLOAD, pk_backend_download_packages_thread, NULL);
}
