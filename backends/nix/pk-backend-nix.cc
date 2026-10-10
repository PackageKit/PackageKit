/* -*- Mode: C; tab-width: 8; indent-tab-modes: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Matthew Bauer <mjbauer95@gmail.com>
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

#include <pk-backend.h>
#include <pk-backend-job.h>

#include <nix/store/globals.hh>
#include <nix/store/store-open.hh>
#include <nix/store/names.hh>
#include <nix/store/profiles.hh>
#include <nix/expr/eval.hh>
#include <nix/expr/eval-gc.hh>
#include <nix/expr/eval-settings.hh>
#include <nix/expr/eval-cache.hh>
#include <nix/expr/attr-path.hh>
#include <nix/fetchers/fetch-settings.hh>
#include <nix/flake/flake.hh>
#include <nix/flake/settings.hh>
#include <nix/util/experimental-features.hh>
#include <nix/cmd/common-eval-args.hh>

#include <pwd.h>
#include <regex>

#include "nix-lib-plus.hh"

typedef struct {
    nix::ref<nix::EvalState> state;
    std::string defaultFlake;
} PkBackendNixPrivate;
static PkBackendNixPrivate *priv;

void pk_backend_initialize(GKeyFile *conf, PkBackend *backend)
{
    nix::initLibStore();
    nix::initGC();
#if NIX_USE_BOEHMGC
    GC_allow_register_threads();
#endif

    nix::verbosity = nix::lvlWarn;
    nix::settings.verboseBuild = false;
    nix::experimentalFeatureSettings.experimentalFeatures = {nix::ExperimentalFeature::Flakes};
    nix::evalSettings.pureEval = true;

    priv = new PkBackendNixPrivate{
        nix::make_ref<nix::EvalState>(nix::LookupPath{}, nix::openStore(), nix::fetchSettings, nix::evalSettings),
        // this might be useful as a configuration setting in the future
        "nixpkgs",
    };
}

void pk_backend_destroy(PkBackend *backend)
{
    delete priv;
}

gboolean pk_backend_supports_parallelization(PkBackend *backend)
{
    return TRUE;
}

const gchar *pk_backend_get_description(PkBackend *backend)
{
    return "Nix - the purely functional package manager";
}

const gchar *pk_backend_get_author(PkBackend *backend)
{
    return "Matthew Bauer <mjbauer95@gmail.com>";
}

PkBitfield pk_backend_get_groups(PkBackend *backend)
{
    return 0;
}

PkBitfield pk_backend_get_filters(PkBackend *backend)
{
    return pk_bitfield_from_enums(
        PK_FILTER_ENUM_SUPPORTED,
        PK_FILTER_ENUM_NOT_SUPPORTED,
        PK_FILTER_ENUM_INSTALLED,
        PK_FILTER_ENUM_NOT_INSTALLED,
        -1);
}

PkBitfield pk_backend_get_roles(PkBackend *backend)
{
    return pk_bitfield_from_enums(
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
        -1);
}

gchar **pk_backend_get_mime_types(PkBackend *backend)
{
    const gchar *mime_types[] = {"application/nix-package", NULL};
    return g_strdupv((gchar **)mime_types);
}

static nix::OrSuggestions<nix::ref<nix::eval_cache::AttrCursor>> nix_get_attr_or_suggestions(
    nix::EvalState &state,
    std::string flake,
    std::string attrPath)
{
    nix::flake::LockFlags lockFlags;
    auto lockedFlake = nix::make_ref<nix::flake::LockedFlake>(
        nix::flake::lockFlake(nix::flakeSettings, state, nix::parseFlakeRef(nix::fetchSettings, flake), lockFlags));

    auto evalCache = nix::flake::openEvalCache(state, lockedFlake);

    return evalCache->getRoot()->findAlongAttrPath(nix::AttrPath::parse(state, attrPath));
}

static void pk_backend_get_details_thread(PkBackendJob *job, GVariant *params, gpointer p)
{
    gchar **package_ids;
    g_variant_get(params, "(^a&s)", &package_ids);

    gchar **parts;
    for (size_t i = 0; package_ids[i] != NULL; i++) {
        // we put the attr path in "PK_PACKAGE_ID_NAME" because that’s how we identify it
        parts = pk_package_id_split(package_ids[i]);
        std::string attrPath = std::string(parts[PK_PACKAGE_ID_NAME]);
        std::string flake = std::string(parts[PK_PACKAGE_ID_ORIGIN]);
        g_strfreev(parts);

        auto attrOrSuggestions = nix_get_attr_or_suggestions(
            *priv->state,
            flake,
            "legacyPackages." + nix::settings.thisSystem.get() + "." + attrPath);
        auto cursor = *attrOrSuggestions;

        if (pk_backend_job_is_cancelled(job))
            return;

        if (attrOrSuggestions && cursor->isDerivation()) {
            auto aMeta = cursor->maybeGetAttr("meta");

            auto aDescription = aMeta ? aMeta->maybeGetAttr("description") : NULL;
            std::string description = aDescription ? aDescription->getString() : "";
            std::replace(description.begin(), description.end(), '\n', ' ');

            std::string license = "unknown";
            auto licenseMeta = aMeta ? aMeta->maybeGetAttr("license") : NULL;
            if (licenseMeta) {
                auto fullName = licenseMeta->maybeGetAttr("fullName");
                if (fullName)
                    license = fullName->getString();
            }

            auto aLongDescription = aMeta ? aMeta->maybeGetAttr("longDescription") : NULL;
            std::string longDescription = aLongDescription ? aLongDescription->getString() : "";

            auto aHomepage = aMeta ? aMeta->maybeGetAttr("homepage") : NULL;
            std::string homepage = aHomepage ? aHomepage->getString() : "";

            pk_backend_job_details(
                job,
                package_ids[i],
                description.c_str(),
                license.c_str(),
                PK_GROUP_ENUM_UNKNOWN,
                longDescription.c_str(),
                homepage.c_str(),
                0,
                G_MAXUINT64);
        } else {
            pk_backend_job_error_code(job, PK_ERROR_ENUM_UNKNOWN, "%s is not a package", attrPath.c_str());
            return;
        }
    }

    pk_backend_job_set_percentage(job, 100);
}

static std::filesystem::path nix_get_user_profile(PkBackendJob *job)
{
    guint uid = pk_backend_job_get_uid(job);

    struct passwd *uid_ent = NULL;
    if ((uid_ent = getpwuid(uid)) == NULL)
        g_error("Failed to get HOME");

    return std::filesystem::path(uid_ent->pw_dir) / ".nix-profile";
}

static nix::PackageInfos nix_query_installed(const std::filesystem::path &profile)
{
    std::error_code ec;
    auto userEnv = std::filesystem::canonical(profile, ec);
    if (ec)
        return {};

    // evaluation is pure, so the user environment has to be allowed explicitly
    priv->state->allowClosure(priv->state->store->toStorePath(userEnv.string()).first);

    return nix::queryInstalled(*priv->state, userEnv);
}

static void nix_search_thread(PkBackendJob *job, GVariant *params, gpointer p)
{
    const gchar **search = NULL;
    PkBitfield filters = 0;

    PkRoleEnum role = pk_backend_job_get_role(job);

    switch (role) {
    case PK_ROLE_ENUM_GET_PACKAGES:
        g_variant_get(params, "(t)", &filters);
        break;
    case PK_ROLE_ENUM_SEARCH_NAME:
    case PK_ROLE_ENUM_SEARCH_DETAILS:
    case PK_ROLE_ENUM_RESOLVE:
        g_variant_get(params, "(t^a&s)", &filters, &search);
        break;
    default:
        break;
    }

    std::string attrPath = "legacyPackages." + nix::settings.thisSystem.get() + ".";
    auto attrOrSuggestions = nix_get_attr_or_suggestions(*priv->state, priv->defaultFlake, attrPath);
    auto cursor = *attrOrSuggestions;

    if (pk_backend_job_is_cancelled(job))
        return;

    std::vector<std::regex> regexes;
    if (search)
        for (; *search != NULL; search++)
            regexes.push_back(std::regex(*search, std::regex::extended | std::regex::icase));

    nix::PackageInfos installedDrvs;

    if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED)
        || pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
        installedDrvs = nix_query_installed(nix_get_user_profile(job));
        installedDrvs.splice(
            installedDrvs.end(),
            nix_query_installed(nix::settings.nixStateDir / "profiles" / "default"));
    }

    int totalDrvs = 0;
    int foundDrvs = 0;

    g_autoptr(GPtrArray) packages = g_ptr_array_new_with_free_func(g_object_unref);

    std::function<void(nix::eval_cache::AttrCursor & cursor, const nix::AttrPath &attrPath)> visit;
    visit = [&](nix::eval_cache::AttrCursor &cursor, const nix::AttrPath &attrPath) {
        try {
            if (pk_backend_job_is_cancelled(job))
                return;

            auto recurse = [&]() {
                auto attrs = cursor.getAttrs();

                totalDrvs += attrs.size();
                if (totalDrvs > 0)
                    pk_backend_job_set_percentage(job, 100 * foundDrvs / totalDrvs);

                for (const auto &attr : attrs) {
                    auto cursor2 = cursor.getAttr(attr);
                    auto attrPath2(attrPath);
                    attrPath2.push_back(attr);
                    visit(*cursor2, attrPath2);
                }
            };

            if (cursor.isDerivation()) {
                foundDrvs++;

                size_t found = 0;

                nix::DrvName name(cursor.getAttr("name")->getString());

                auto aMeta = cursor.maybeGetAttr("meta");
                auto aDescription = aMeta ? aMeta->maybeGetAttr("description") : NULL;

                auto description = aDescription ? aDescription->getString() : "";
                std::replace(description.begin(), description.end(), '\n', ' ');

                auto attrPath2 = attrPath.to_string(*priv->state);

                for (auto &regex : regexes) {
                    switch (role) {
                    case PK_ROLE_ENUM_SEARCH_NAME:
                    case PK_ROLE_ENUM_RESOLVE: {
                        std::smatch nameMatch;
                        std::regex_search(name.name, nameMatch, regex);
                        std::smatch attrMatch;
                        std::regex_search(attrPath2, attrMatch, regex);
                        if (!nameMatch.empty() || !attrMatch.empty())
                            found++;
                        break;
                    }
                    case PK_ROLE_ENUM_SEARCH_DETAILS: {
                        std::smatch descriptionMatch;
                        std::regex_search(description, descriptionMatch, regex);
                        if (!descriptionMatch.empty())
                            found++;
                        break;
                    }
                    default:
                        found++;
                        break;
                    }
                }

                if (found == regexes.size() || regexes.empty()) {
                    bool isInstalled = false;
                    for (auto drv : installedDrvs) {
                        if (nix::DrvName(drv.queryName()).matches(name)) {
                            isInstalled = true;
                            break;
                        }
                    }

                    if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED) && isInstalled)
                        return;
                    if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED) && !isInstalled)
                        return;

                    auto available = aMeta ? aMeta->maybeGetAttr("available") : NULL;
                    bool isSupported = available ? available->getBool() : true;

                    if (pk_bitfield_contain(filters, PK_FILTER_ENUM_SUPPORTED) && !isSupported)
                        return;
                    if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_SUPPORTED) && isSupported)
                        return;

                    std::string system = cursor.getAttr("system")->getString();

                    PkInfoEnum info = PK_INFO_ENUM_UNKNOWN;
                    if (isSupported)
                        info = PK_INFO_ENUM_AVAILABLE;
                    if (isInstalled)
                        info = PK_INFO_ENUM_INSTALLED;

                    if (totalDrvs > 0)
                        pk_backend_job_set_percentage(job, 100 * foundDrvs / totalDrvs);

                    g_autofree gchar *package_id = pk_package_id_build(
                        attrPath2.c_str(),
                        name.version.c_str(),
                        system.c_str(),
                        priv->defaultFlake.c_str(),
                        NULL);
                    pk_backend_packages_add(packages, info, package_id, description.c_str(), PK_SEVERITY_ENUM_NONE);
                }
            }

            else if (attrPath.size() == 0)
                recurse();

            else if (attrPath.size() >= 1) {
                auto attr = cursor.maybeGetAttr(priv->state->s.recurseForDerivations);
                if (attr && attr->getBool())
                    recurse();
            }
        } catch (nix::EvalError &e) {
        }
    };
    visit(*cursor, {});

    pk_backend_job_packages(job, packages);
    pk_backend_job_set_percentage(job, 100);
}

static void nix_refresh_thread(PkBackendJob *job, GVariant *params, gpointer p)
{
    nix::fetchSettings.tarballTtl = 0;
    nix_search_thread(job, params, p);
    nix::fetchSettings.tarballTtl = 60 * 60;

    pk_backend_job_set_percentage(job, 100);
}

static void nix_install_thread(PkBackendJob *job, GVariant *params, gpointer p)
{
    PkBitfield flags;
    gchar **package_ids;
    g_variant_get(params, "(t^a&s)", &flags, &package_ids);

    nix::PackageInfos newElems;
    gchar **parts;

    for (size_t i = 0; package_ids[i] != NULL; i++) {
        if (pk_backend_job_is_cancelled(job))
            return;

        // we put the attr path in "PK_PACKAGE_ID_NAME" because that’s how we identify it
        parts = pk_package_id_split(package_ids[i]);
        std::string attrPath = std::string(parts[PK_PACKAGE_ID_NAME]);
        std::string flake = std::string(parts[PK_PACKAGE_ID_ORIGIN]);
        g_strfreev(parts);

        auto attrOrSuggestions = nix_get_attr_or_suggestions(
            *priv->state,
            flake,
            "legacyPackages." + nix::settings.thisSystem.get() + "." + attrPath);
        auto cursor = *attrOrSuggestions;

        if (attrOrSuggestions && cursor->isDerivation()) {
            std::optional<nix::PackageInfo> drv;
            drv = nix::getDerivation(*priv->state, cursor->forceValue(), false);
            if (drv) {
                try {
                    drv->queryDrvPath();
                } catch (nix::Error &e) {
                    pk_backend_job_error_code(job, PK_ERROR_ENUM_UNKNOWN, "failed to evaluate %s", attrPath.c_str());
                    return;
                }

                pk_backend_job_package_status(job, package_ids[i], PK_INFO_ENUM_INSTALLING);
                newElems.push_back(*drv);
            } else {
                pk_backend_job_error_code(job, PK_ERROR_ENUM_UNKNOWN, "failed to evaluate %s", attrPath.c_str());
                return;
            }
        } else {
            pk_backend_job_error_code(job, PK_ERROR_ENUM_UNKNOWN, "%s is not a package", attrPath.c_str());
            return;
        }
    }

    auto profile = nix_get_user_profile(job);

    while (true) {
        if (pk_backend_job_is_cancelled(job)) {
            return;
        }

        std::string lockToken = nix::optimisticLockProfile(profile);

        nix::PackageInfos allElems(newElems);

        /* Add in the already installed derivations, unless they have
           the same name as a to-be-installed element. */
        nix::PackageInfos installedElems;
        try {
            installedElems = nix_query_installed(profile);
        } catch (nix::Error &e) {
            pk_backend_job_error_code(job, PK_ERROR_ENUM_UNKNOWN, "failed to create new environment: %s", e.what());
            return;
        }

        for (auto &i : installedElems) {
            bool found = false;

            for (auto &j : newElems) {
                if (nix::DrvName(i.queryName()).name == nix::DrvName(j.queryName()).name) {
                    found = true;
                    break;
                }
            }

            if (!found)
                allElems.push_back(i);
        }

        try {
            if (nix::createUserEnv(*priv->state, allElems, profile, false, lockToken))
                break;
        } catch (nix::Error &e) {
            pk_backend_job_error_code(job, PK_ERROR_ENUM_UNKNOWN, "failed to create new environment: %s", e.what());
            return;
        }
    }

    g_autoptr(GPtrArray) packages = g_ptr_array_new_with_free_func(g_object_unref);
    for (size_t i = 0; package_ids[i] != NULL; i++)
        pk_backend_packages_add(packages, PK_INFO_ENUM_INSTALLED, package_ids[i], NULL, PK_SEVERITY_ENUM_NONE);
    pk_backend_job_packages(job, packages);

    pk_backend_job_set_percentage(job, 100);
}

static void nix_remove_thread(PkBackendJob *job, GVariant *params, gpointer p)
{
    PkBitfield transaction_flags;
    gchar **package_ids;
    gboolean allow_deps, autoremove;
    g_variant_get(params, "(t^a&sbb)", &transaction_flags, &package_ids, &allow_deps, &autoremove);

    auto profile = nix_get_user_profile(job);

    nix::PackageInfos elemsToDelete;
    gchar **parts;

    for (size_t i = 0; package_ids[i] != NULL; i++) {
        // we put the attr path in "PK_PACKAGE_ID_NAME" because that’s how we identify it
        parts = pk_package_id_split(package_ids[i]);
        std::string attrPath = std::string(parts[PK_PACKAGE_ID_NAME]);
        std::string flake = std::string(parts[PK_PACKAGE_ID_ORIGIN]);
        g_strfreev(parts);

        auto attrOrSuggestions = nix_get_attr_or_suggestions(
            *priv->state,
            flake,
            "legacyPackages." + nix::settings.thisSystem.get() + "." + attrPath);
        auto cursor = *attrOrSuggestions;
        if (attrOrSuggestions && cursor->isDerivation()) {
            auto drv = nix::getDerivation(*priv->state, cursor->forceValue(), false);
            if (drv)
                elemsToDelete.push_back(*drv);
            else {
                pk_backend_job_error_code(job, PK_ERROR_ENUM_UNKNOWN, "failed to evaluate %s", attrPath.c_str());
                return;
            }
        } else {
            pk_backend_job_error_code(job, PK_ERROR_ENUM_UNKNOWN, "%s is not a package", attrPath.c_str());
            return;
        }

        pk_backend_job_package_status(job, package_ids[i], PK_INFO_ENUM_REMOVING);
    }

    while (true) {
        if (pk_backend_job_is_cancelled(job)) {
            return;
        }

        std::string lockToken = nix::optimisticLockProfile(profile);

        nix::PackageInfos installedElems = nix_query_installed(profile);
        nix::PackageInfos newElems;

        for (auto &i : installedElems) {
            bool found = false;

            for (auto &j : elemsToDelete) {
                if (nix::DrvName(i.queryName()).name == nix::DrvName(j.queryName()).name) {
                    found = true;
                    break;
                }
            }

            if (!found)
                newElems.push_back(i);
        }

        if (nix::createUserEnv(*priv->state, newElems, profile, false, lockToken))
            break;
    }

    g_autoptr(GPtrArray) packages = g_ptr_array_new_with_free_func(g_object_unref);
    for (size_t i = 0; package_ids[i] != NULL; i++)
        pk_backend_packages_add(packages, PK_INFO_ENUM_AVAILABLE, package_ids[i], NULL, PK_SEVERITY_ENUM_NONE);
    pk_backend_job_packages(job, packages);

    pk_backend_job_set_percentage(job, 100);
}

static void nix_get_updates_thread(PkBackendJob *job, GVariant *params, gpointer p)
{
    auto profile = nix_get_user_profile(job);

    nix::PackageInfos installedElems = nix_query_installed(profile);

    g_autoptr(GPtrArray) packages = g_ptr_array_new_with_free_func(g_object_unref);

    int progress = 0;
    for (auto &i : installedElems) {
        pk_backend_job_set_percentage(job, 100 * progress++ / installedElems.size());

        auto attrOrSuggestions = nix_get_attr_or_suggestions(
            *priv->state,
            priv->defaultFlake,
            "legacyPackages." + nix::settings.thisSystem.get() + "." + i.attrPath);
        auto cursor = *attrOrSuggestions;
        if (attrOrSuggestions && cursor->isDerivation()) {
            auto drv = nix::getDerivation(*priv->state, cursor->forceValue(), false);
            if (drv && drv->queryDrvPath() != i.queryDrvPath()) {
                nix::DrvName name(drv->queryName());
                g_autofree gchar *package_id = pk_package_id_build(
                    drv->attrPath.c_str(),
                    name.version.c_str(),
                    drv->querySystem().c_str(),
                    priv->defaultFlake.c_str(),
                    NULL);
                pk_backend_packages_add(
                    packages,
                    PK_INFO_ENUM_UPDATE,
                    package_id,
                    drv->queryMetaString("description").c_str(),
                    PK_SEVERITY_ENUM_NONE);
            }
        }
    }

    pk_backend_job_packages(job, packages);
    pk_backend_job_set_percentage(job, 100);
}

// the collector has to know about every thread that touches the evaluator,
// and the job threads are not created through it
static void nix_job_thread(PkBackendJob *job, GVariant *params, gpointer func)
{
#if NIX_USE_BOEHMGC
    GC_stack_base stackBase;
    GC_get_stack_base(&stackBase);
    GC_register_my_thread(&stackBase);
#endif

    ((PkBackendJobThreadFunc)func)(job, params, NULL);

#if NIX_USE_BOEHMGC
    GC_unregister_my_thread();
#endif
}

void pk_backend_run_job(PkBackend *backend, PkBackendJob *job)
{
    PkRoleEnum role = pk_backend_job_get_role(job);

    switch (role) {
    case PK_ROLE_ENUM_GET_DETAILS:
        pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
        pk_backend_job_thread_create(job, nix_job_thread, (gpointer)pk_backend_get_details_thread, NULL);
        break;
    case PK_ROLE_ENUM_GET_PACKAGES:
    case PK_ROLE_ENUM_SEARCH_NAME:
    case PK_ROLE_ENUM_SEARCH_DETAILS:
    case PK_ROLE_ENUM_RESOLVE:
        pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
        pk_backend_job_thread_create(job, nix_job_thread, (gpointer)nix_search_thread, NULL);
        break;
    case PK_ROLE_ENUM_REFRESH_CACHE:
        pk_backend_job_set_status(job, PK_STATUS_ENUM_REFRESH_CACHE);
        pk_backend_job_thread_create(job, nix_job_thread, (gpointer)nix_refresh_thread, NULL);
        break;
    case PK_ROLE_ENUM_INSTALL_PACKAGES:
        pk_backend_job_set_status(job, PK_STATUS_ENUM_INSTALL);
        pk_backend_job_thread_create(job, nix_job_thread, (gpointer)nix_install_thread, NULL);
        break;
    case PK_ROLE_ENUM_REMOVE_PACKAGES:
        pk_backend_job_set_status(job, PK_STATUS_ENUM_REMOVE);
        pk_backend_job_thread_create(job, nix_job_thread, (gpointer)nix_remove_thread, NULL);
        break;
    case PK_ROLE_ENUM_UPDATE_PACKAGES:
        pk_backend_job_set_status(job, PK_STATUS_ENUM_UPDATE);
        pk_backend_job_thread_create(job, nix_job_thread, (gpointer)nix_install_thread, NULL);
        break;
    case PK_ROLE_ENUM_GET_UPDATES:
        pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
        pk_backend_job_thread_create(job, nix_job_thread, (gpointer)nix_get_updates_thread, NULL);
        break;
    default:
        pk_backend_job_error_code(
            job,
            PK_ERROR_ENUM_NOT_SUPPORTED,
            "role %s is not supported",
            pk_role_enum_to_string(role));
        pk_backend_job_finished(job);
        break;
    }
}
