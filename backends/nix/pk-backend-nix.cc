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

#include <pk-backend.h>
#include <pk-backend-job.h>

#include <nix/config.h>

#include <nix/globals.hh>
#include <nix/eval.hh>
#include <nix/store-api.hh>
#include <nix/names.hh>
#include <nix/eval-cache.hh>
#include <nix/attr-path.hh>
#include <nix/profiles.hh>
#include <nix/flake/flake.hh>

#include <pwd.h>

#include "nix-lib-plus.hh"

typedef struct {
	nix::ref<nix::EvalState> state;
	std::string defaultFlake;
} PkBackendNixPrivate;
static PkBackendNixPrivate* priv;

void
pk_backend_initialize (GKeyFile* conf, PkBackend* backend)
{
	priv = g_new0 (PkBackendNixPrivate, 1);

	nix::loadConfFile ();
	nix::initGC ();

	nix::verbosity = nix::lvlWarn;
	nix::settings.verboseBuild = false;
	nix::settings.experimentalFeatures = {"flakes"};
	nix::evalSettings.pureEval = true;

	const nix::Strings searchPath;
	priv->state = nix::ref<nix::EvalState> (std::make_shared<nix::EvalState>(searchPath, nix::openStore ()));

	// this might be useful as a configuration setting in the future
	priv->defaultFlake = "nixpkgs";
}

void
pk_backend_destroy (PkBackend* backend)
{
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

PkBitfield
pk_backend_get_groups (PkBackend* backend)
{
	return 0;
}

PkBitfield
pk_backend_get_filters (PkBackend* backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_SUPPORTED,
		PK_FILTER_ENUM_NOT_SUPPORTED,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_NOT_INSTALLED,
		-1
	);
}

PkBitfield
pk_backend_get_roles (PkBackend* backend)
{
	return pk_bitfield_from_enums (
		PK_ROLE_ENUM_CANCEL,
		PK_ROLE_ENUM_GET_DETAILS,
		PK_ROLE_ENUM_GET_PACKAGES,
		PK_ROLE_ENUM_INSTALL_PACKAGES,
		PK_ROLE_ENUM_REFRESH_CACHE,
		PK_ROLE_ENUM_REMOVE_PACKAGES,
		PK_ROLE_ENUM_RESOLVE,
		PK_ROLE_ENUM_SEARCH_DETAILS,
		PK_ROLE_ENUM_SEARCH_NAME,
		PK_ROLE_ENUM_UPDATE_PACKAGES,
		PK_ROLE_ENUM_GET_UPDATES,
		-1
	);
}

gchar **
pk_backend_get_mime_types (PkBackend* backend)
{
	const gchar* mime_types[] = { "application/nix-package", NULL };
	return g_strdupv ((gchar **) mime_types);
}

static std::shared_ptr<nix::eval_cache::AttrCursor>
nix_get_cursor (nix::EvalState & state, std::string flake, std::string attrPath)
{
	nix::flake::LockFlags lockFlags;
	auto lockedFlake = std::make_shared<nix::flake::LockedFlake> (nix::flake::lockFlake (state, nix::parseFlakeRef(flake), lockFlags));

	auto evalCache = nix::ref (std::make_shared<nix::eval_cache::EvalCache> (true, lockedFlake->getFingerprint(), state,
								[&state, lockedFlake]()
		{
			auto vFlake = state.allocValue ();
			nix::flake::callFlake (state, *lockedFlake, *vFlake);

			state.forceValue (*vFlake);

			auto aOutputs = vFlake->attrs->get (state.symbols.create("outputs"));
			assert (aOutputs);

			return aOutputs->value;
		}));

	return evalCache->getRoot()->findAlongAttrPath (nix::parseAttrPath (state, attrPath));
}

static void
pk_backend_get_details_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	gchar** package_ids;
	g_variant_get (params, "(^a&s)", &package_ids);

	gchar** parts;
	for (size_t i = 0; package_ids[i] != NULL; i++) {
		// we put the attr path in "PK_PACKAGE_ID_NAME" because that’s how we identify it
		parts = pk_package_id_split (package_ids[i]);
		std::string attrPath = std::string (parts[PK_PACKAGE_ID_NAME]);
		std::string flake = std::string (parts[PK_PACKAGE_ID_DATA]);
		g_strfreev (parts);

		auto cursor = nix_get_cursor (*priv->state, flake, "legacyPackages." + nix::settings.thisSystem.get () + "." + attrPath);

		if (pk_backend_job_is_cancelled (job))
			return;

		if (cursor && cursor->isDerivation ()) {
			auto aMeta = cursor->maybeGetAttr ("meta");

			auto aDescription = aMeta ? aMeta->maybeGetAttr ("description") : NULL;
			std::string description = aDescription ? aDescription->getString () : "";
			std::replace (description.begin (), description.end (), '\n', ' ');

			std::string license = "unknown";
			auto licenseMeta = aMeta ? aMeta->maybeGetAttr ("license") : NULL;
			if (licenseMeta) {
				auto fullName = licenseMeta->maybeGetAttr ("fullName");
				if (fullName)
					license = fullName->getString ();
			}

			auto aLongDescription = aMeta ? aMeta->maybeGetAttr ("longDescription") : NULL;
			std::string longDescription = aLongDescription ? aLongDescription->getString () : "";

			auto aHomepage = aMeta ? aMeta->maybeGetAttr ("homepage") : NULL;
			std::string homepage = aHomepage ? aHomepage->getString () : "";

			pk_backend_job_details (job,
						package_ids[i],
						description.c_str (),
						license.c_str (),
						PK_GROUP_ENUM_UNKNOWN,
						longDescription.c_str (),
						homepage.c_str (),
						0);
		} else {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_UNKNOWN,
						   "%s is not a package",
						   attrPath.c_str ());
			return;
		}
	}

	pk_backend_job_set_percentage (job, 100);
}

void
pk_backend_get_details (PkBackend* backend, PkBackendJob* job, gchar** packages)
{
	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_thread_create(job, pk_backend_get_details_thread, NULL, NULL);
}

static nix::Path
nix_get_user_profile (PkBackendJob* job)
{
	guint uid = pk_backend_job_get_uid (job);

	struct passwd* uid_ent = NULL;
	if ((uid_ent = getpwuid (uid)) == NULL)
		g_error ("Failed to get HOME");

	return std::string(uid_ent->pw_dir) + "/.nix-profile";
}

static void
nix_search_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	const gchar **search;
	PkBitfield filters;

	PkRoleEnum role = pk_backend_job_get_role (job);

	switch(role) {
	case PK_ROLE_ENUM_GET_PACKAGES:
		g_variant_get (params, "(t)", &filters);
		break;
	case PK_ROLE_ENUM_SEARCH_NAME:
	case PK_ROLE_ENUM_SEARCH_DETAILS:
	case PK_ROLE_ENUM_RESOLVE:
		g_variant_get (params, "(t^a&s)", &filters, &search);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	std::string attrPath = "legacyPackages." + nix::settings.thisSystem.get () + ".";
	auto cursor = nix_get_cursor (*priv->state, priv->defaultFlake, attrPath);

	if (pk_backend_job_is_cancelled (job))
		return;

	std::vector<std::regex> regexes;
	if (search)
		for (; *search != NULL; search++)
			regexes.push_back (std::regex (*search, std::regex::extended | std::regex::icase));

	nix::DrvInfos installedDrvs;

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)
		|| pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		std::optional<nix::PathSet> oldAllowedPaths = priv->state->allowedPaths;
		priv->state->allowedPaths = std::nullopt;

		std::string userProfile = nix_get_user_profile (job);
		if (nix::pathExists (userProfile + "/manifest.nix")) {
			nix::Value v;
			priv->state->evalFile (userProfile + "/manifest.nix", v);
			nix::Bindings & bindings (*priv->state->allocBindings(0));
			nix::getDerivations (*priv->state, v, "", bindings, installedDrvs, false);
		}

		std::string defaultProfile = nix::settings.nixStateDir + "/profiles/default";
		if (nix::pathExists (defaultProfile + "/manifest.nix")) {
			nix::Value v;
			priv->state->evalFile (defaultProfile + "/manifest.nix", v);
			nix::Bindings & bindings (*priv->state->allocBindings(0));
			nix::getDerivations (*priv->state, v, "", bindings, installedDrvs, false);
		}

		priv->state->allowedPaths = oldAllowedPaths;
	}

	int totalDrvs = 0;
	int foundDrvs = 0;

	std::function<void(nix::eval_cache::AttrCursor & cursor, const std::vector<nix::Symbol> & attrPath)> visit;
	visit = [&](nix::eval_cache::AttrCursor & cursor, const std::vector<nix::Symbol> & attrPath) {
		try {
		     if (pk_backend_job_is_cancelled (job))
			     return;

			auto recurse = [&] () {
				auto attrs = cursor.getAttrs ();

				totalDrvs += attrs.size();
				if (totalDrvs > 0)
					pk_backend_job_set_percentage (job, 100 * foundDrvs / totalDrvs);

				for (const auto & attr : attrs) {
					auto cursor2 = cursor.getAttr (attr);
					auto attrPath2 (attrPath);
					attrPath2.push_back (attr);
					visit (*cursor2, attrPath2);
				}
			};

			if (cursor.isDerivation ()) {
				foundDrvs++;

				size_t found = 0;

				nix::DrvName name (cursor.getAttr ("name")->getString());

				auto aMeta = cursor.maybeGetAttr ("meta");
				auto aDescription = aMeta ? aMeta->maybeGetAttr ("description") : NULL;

				auto description = aDescription ? aDescription->getString() : "";
				std::replace (description.begin (), description.end (), '\n', ' ');

				auto attrPath2 = concatStringsSep (".", attrPath);

				for (auto & regex : regexes) {
					switch (role) {
					case PK_ROLE_ENUM_SEARCH_NAME:
					case PK_ROLE_ENUM_RESOLVE: {
						std::smatch nameMatch;
						std::regex_search (name.name, nameMatch, regex);
						std::smatch attrMatch;
						std::regex_search (attrPath2, attrMatch, regex);
						if (!nameMatch.empty () || !attrMatch.empty())
							found++;
						break;
					}
					case PK_ROLE_ENUM_SEARCH_DETAILS: {
						std::smatch descriptionMatch;
						std::regex_search (description, descriptionMatch, regex);
						if (!descriptionMatch.empty ())
							found++;
						break;
					}
					default:
						found++;
						break;
					}
				}

				if (found == regexes.size () || regexes.empty ()) {
					bool isInstalled = false;
					for (auto drv : installedDrvs) {
						if (nix::DrvName (drv.queryName ()).matches(name)) {
							isInstalled = true;
							break;
						}
					}

					if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) && isInstalled)
						return;
					if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) && !isInstalled)
						return;

					auto available = aMeta ? aMeta->maybeGetAttr ("available") : NULL;
					bool isSupported = available ? available->getBool () : true;

					if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SUPPORTED) && !isSupported)
						return;
					if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_SUPPORTED) && isSupported)
						return;

					std::string system = cursor.getAttr ("system")->getString();

					PkInfoEnum info = PK_INFO_ENUM_UNKNOWN;
					if (isSupported)
						info = PK_INFO_ENUM_AVAILABLE;
					if (isInstalled)
						info = PK_INFO_ENUM_INSTALLED;

					if (totalDrvs > 0)
						pk_backend_job_set_percentage (job, 100 * foundDrvs / totalDrvs);

					pk_backend_job_package (job,
								info,
								pk_package_id_build (attrPath2.c_str (),
										     name.version.c_str (),
										     system.c_str (),
										     priv->defaultFlake.c_str ()),
								description.c_str());
				}
			}

			else if (attrPath.size() <= 1)
				recurse();

			else if (attrPath.size() > 1) {
				auto attr = cursor.maybeGetAttr(priv->state->sRecurseForDerivations);
				if (attr && attr->getBool())
					recurse();
			}
		} catch (nix::EvalError & e) {
		}
        };
	visit(*cursor, {});
	pk_backend_job_set_percentage (job, 100);
}

void
pk_backend_get_packages (PkBackend* backend, PkBackendJob* job, PkBitfield filters)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_GENERATE_PACKAGE_LIST);
	pk_backend_job_thread_create(job, nix_search_thread, NULL, NULL);
}

void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values) {
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_thread_create (job, nix_search_thread, NULL, NULL);
}

void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_thread_create (job, nix_search_thread, NULL, NULL);
}

void
pk_backend_resolve(PkBackend* self, PkBackendJob* job, PkBitfield filters, gchar** search)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_thread_create (job, nix_search_thread, NULL, NULL);
}

static void
nix_refresh_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	nix::settings.tarballTtl = 0;
	nix_search_thread (job, params, p);
	nix::settings.tarballTtl = 60 * 60;

	pk_backend_job_set_percentage (job, 100);
}

void
pk_backend_refresh_cache (PkBackend* backend, PkBackendJob* job, gboolean force)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_job_thread_create (job, nix_refresh_thread, NULL, NULL);
}

static void
nix_install_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	PkBitfield flags;
	gchar** package_ids;
	g_variant_get (params, "(t^a&s)", &flags, &package_ids);

	nix::DrvInfos newElems;
	gchar** parts;

	for (size_t i = 0; package_ids[i] != NULL; i++) {
		if (pk_backend_job_is_cancelled (job))
			return;

		// we put the attr path in "PK_PACKAGE_ID_NAME" because that’s how we identify it
		parts = pk_package_id_split (package_ids[i]);
		std::string attrPath = std::string (parts[PK_PACKAGE_ID_NAME]);
		std::string flake = std::string (parts[PK_PACKAGE_ID_DATA]);
		g_strfreev (parts);

		auto cursor = nix_get_cursor (*priv->state, flake, "legacyPackages." + nix::settings.thisSystem.get () + "." + attrPath);

		if (cursor && cursor->isDerivation ()) {
			std::optional<nix::DrvInfo> drv;
			drv = nix::getDerivation (*priv->state, cursor->forceValue(), false);
			if (drv) {
				auto aMeta = cursor->maybeGetAttr ("meta");
				auto aDescription = aMeta ? aMeta->maybeGetAttr ("description") : NULL;
				auto description = aDescription ? aDescription->getString() : "";

				try {
					drv->queryDrvPath ();
				} catch (nix::Error & e) {
					pk_backend_job_error_code (job,
								   PK_ERROR_ENUM_UNKNOWN,
								   "failed to evaluate %s",
								   attrPath.c_str ());
					return;
				}

				pk_backend_job_package (job,
							PK_INFO_ENUM_INSTALLING,
							package_ids[i],
							description.c_str());
				newElems.push_back (*drv);
			} else {
				pk_backend_job_error_code (job,
							   PK_ERROR_ENUM_UNKNOWN,
							   "failed to evaluate %s",
							   attrPath.c_str ());
				return;
			}
		} else {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_UNKNOWN,
						   "%s is not a package",
						   attrPath.c_str ());
			return;
		}
	}

	std::string profile = nix_get_user_profile (job);

	std::optional<nix::PathSet> oldAllowedPaths = priv->state->allowedPaths;
	priv->state->allowedPaths = std::nullopt;

	while (true) {
		if (pk_backend_job_is_cancelled (job)) {
			priv->state->allowedPaths = oldAllowedPaths;
			return;
		}

		std::string lockToken = nix::optimisticLockProfile (profile);

		nix::DrvInfos allElems (newElems);

		/* Add in the already installed derivations, unless they have
		   the same name as a to-be-installed element. */
		nix::DrvInfos installedElems = nix::queryInstalled (*priv->state, profile);

		for (auto & i : installedElems) {
			bool found = false;

			for (auto & j : newElems) {
				if (DrvName(i.queryName ()).name == DrvName (j.queryName ()).name) {
					found = true;
					break;
				}
			}

			if (!found)
				allElems.push_back (i);
		}

		try {
			if (nix::createUserEnv (*priv->state, allElems, profile, false, lockToken))
				break;
		} catch (nix::Error & e) {
			priv->state->allowedPaths = oldAllowedPaths;
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_UNKNOWN,
						   "failed to create new environment");
			return;
		}
	}

	priv->state->allowedPaths = oldAllowedPaths;

	for (size_t i = 0; package_ids[i] != NULL; i++)
		pk_backend_job_package (job, PK_INFO_ENUM_INSTALLED, package_ids[i], NULL);

	pk_backend_job_set_percentage (job, 100);
}

void
pk_backend_install_packages (PkBackend* backend, PkBackendJob* job, PkBitfield transaction_flags, gchar** package_ids)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);
	pk_backend_job_thread_create (job, nix_install_thread, NULL, NULL);
}

static void
nix_remove_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	PkBitfield transaction_flags;
	gchar** package_ids;
	gboolean allow_deps, autoremove;
	g_variant_get (params, "(t^a&sbb)", &transaction_flags, &package_ids, &allow_deps, &autoremove);

	nix::Path profile = nix_get_user_profile (job);

	nix::DrvInfos elemsToDelete;
	gchar** parts;

	for (size_t i = 0; package_ids[i] != NULL; i++) {
		// we put the attr path in "PK_PACKAGE_ID_NAME" because that’s how we identify it
		parts = pk_package_id_split (package_ids[i]);
		std::string attrPath = std::string (parts[PK_PACKAGE_ID_NAME]);
		std::string flake = std::string (parts[PK_PACKAGE_ID_DATA]);
		g_strfreev (parts);

		auto cursor = nix_get_cursor (*priv->state, flake, "legacyPackages." + nix::settings.thisSystem.get () + "." + attrPath);
		if (cursor && cursor->isDerivation ()) {
			auto drv = nix::getDerivation (*priv->state, cursor->forceValue(), false);
			if (drv)
				elemsToDelete.push_back (*drv);
			else {
				pk_backend_job_error_code (job,
							   PK_ERROR_ENUM_UNKNOWN,
							   "failed to evaluate %s",
							   attrPath.c_str ());
				return;
			}
		} else {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_UNKNOWN,
						   "%s is not a package",
						   attrPath.c_str ());
			return;
		}

		auto aMeta = cursor->maybeGetAttr ("meta");
		auto aDescription = aMeta ? aMeta->maybeGetAttr ("description") : NULL;
		auto description = aDescription ? aDescription->getString() : "";

		pk_backend_job_package (job, PK_INFO_ENUM_REMOVING, package_ids[i], description.c_str());
	}

	std::optional<nix::PathSet> oldAllowedPaths = priv->state->allowedPaths;
	priv->state->allowedPaths = std::nullopt;

	while (true) {
		if (pk_backend_job_is_cancelled (job)) {
			priv->state->allowedPaths = oldAllowedPaths;
			return;
		}

		std::string lockToken = nix::optimisticLockProfile (profile);

		nix::DrvInfos installedElems = nix::queryInstalled (*priv->state, profile);
		nix::DrvInfos newElems;

		for (auto & i : installedElems) {
			bool found = false;


			for (auto & j : elemsToDelete) {
				if (DrvName (i.queryName ()).name == DrvName (j.queryName ()).name) {
					found = true;
					break;
				}
			}

			if (!found)
				newElems.push_back (i);
		}

		if (nix::createUserEnv (*priv->state, newElems, profile, false, lockToken))
			break;
	}

	priv->state->allowedPaths = oldAllowedPaths;

	for (size_t i = 0; package_ids[i] != NULL; i++)
		pk_backend_job_package (job, PK_INFO_ENUM_AVAILABLE, package_ids[i], NULL);

	pk_backend_job_set_percentage (job, 100);
}

void
pk_backend_remove_packages (PkBackend* backend, PkBackendJob* job, PkBitfield transaction_flags, gchar** package_ids, gboolean allow_deps, gboolean autoremove)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);
	pk_backend_job_thread_create (job, nix_remove_thread, NULL, NULL);
}

void
pk_backend_update_packages (PkBackend* backend, PkBackendJob* job, PkBitfield transaction_flags, gchar** package_ids)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);
	pk_backend_job_thread_create (job, nix_install_thread, NULL, NULL);
}

static void
nix_get_updates_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	auto profile = nix_get_user_profile (job);

	std::optional<nix::PathSet> oldAllowedPaths = priv->state->allowedPaths;
	priv->state->allowedPaths = std::nullopt;

	nix::DrvInfos installedElems = nix::queryInstalled (*priv->state, profile);

	priv->state->allowedPaths = oldAllowedPaths;

	int progress = 0;
	for (auto & i : installedElems) {
		pk_backend_job_set_percentage (job, 100 * progress++ / installedElems.size());

		auto cursor = nix_get_cursor (*priv->state, priv->defaultFlake, "legacyPackages." + nix::settings.thisSystem.get () + "." + i.attrPath);
		if (cursor && cursor->isDerivation ()) {
			auto drv = nix::getDerivation (*priv->state, cursor->forceValue(), false);
			if (drv && drv->queryDrvPath () != i.queryDrvPath ()) {
				nix::DrvName name (drv->queryName ());
				pk_backend_job_package (job,
							PK_INFO_ENUM_NORMAL,
							pk_package_id_build (drv->attrPath.c_str (),
									     name.version.c_str (),
									     drv->querySystem ().c_str (),
									     priv->defaultFlake.c_str ()),
							drv->queryMetaString ("description").c_str ());
			}
		}
	}

	pk_backend_job_set_percentage (job, 100);
}

void
pk_backend_get_updates(PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_thread_create(job, nix_get_updates_thread, NULL, NULL);
}
