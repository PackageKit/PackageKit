/*
 * Copyright (C) Serenity Cybersecurity, LLC <license@futurecrew.ru>
 *               Author: Gleb Popov <arrowd@FreeBSD.org>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <gio/gio.h>

#include <pk-backend.h>
#include <pk-backend-job.h>

#include <pkg.h>

#include <optional>
#include <string>
#include <memory>
#include <vector>
#include <unordered_set>

#include "stolen.h"
#include "PackageView.hpp"
#include "PackageDatabase.hpp"
#include "PKJobFinisher.hpp"
#include "Jobs.hpp"

class PKJobCanceller;

typedef struct {
    GCancellable* cancellable;
    PKJobCanceller* canceller;
} PkBackendFreeBSDJobData;

#include "PKJobCanceller.hpp"

// TODO: Research pkg-audit
// TODO: Implement proper progress reporting everywhere
// TODO: Implement correct job status reporting everywhere

// These are groups that FreeBSD backend claims to support.
static PkBitfield advertised_groups;
static void InitAdvertisedGroups()
{
    advertised_groups =
        pk_bitfield_from_enums (PK_GROUP_ENUM_ACCESSIBILITY, //accessibility
            PK_GROUP_ENUM_COMMUNICATION, //comms
            PK_GROUP_ENUM_DESKTOP_GNOME, //gnome-* ports
            PK_GROUP_ENUM_DESKTOP_KDE, //plasma5-* ports
            PK_GROUP_ENUM_DESKTOP_OTHER, //budgie, enlightenment, etc.
            PK_GROUP_ENUM_DESKTOP_XFCE, //xfce-* ports
            PK_GROUP_ENUM_EDUCATION, //edu
            PK_GROUP_ENUM_FONTS, //x11-fonts
            PK_GROUP_ENUM_GAMES, //games
            PK_GROUP_ENUM_GRAPHICS, //graphics
            PK_GROUP_ENUM_INTERNET, //www
            PK_GROUP_ENUM_NETWORK, //net
            PK_GROUP_ENUM_PROGRAMMING, //devel
            PK_GROUP_ENUM_MULTIMEDIA, //multimedia
            PK_GROUP_ENUM_SECURITY, //security
            PK_GROUP_ENUM_SYSTEM, //sysutils
            PK_GROUP_ENUM_SCIENCE, //science
            PK_GROUP_ENUM_MAPS, //geography
            -1);
}

static std::unordered_set<std::string> portsCategories;
// These are all available categories (including virtual ones) supported by the
// Ports infrastructure. This list can be produced by running
// make -C /usr/ports/ports-mgmt/pkg -V '${VALID_CATEGORIES:O:U:S/^/|/:S/$/|,/:S/|/"/g:ts\n}'
static const char* portsCategoriesData[] = {
    "accessibility",
    "afterstep",
    "arabic",
    "archivers",
    "astro",
    "audio",
    "base",
    "benchmarks",
    "biology",
    "budgie",
    "cad",
    "chinese",
    "comms",
    "converters",
    "databases",
    "deskutils",
    "devel",
    "dns",
    "docs",
    "editors",
    "education",
    "elisp",
    "emulators",
    "enlightenment",
    "finance",
    "french",
    "ftp",
    "games",
    "geography",
    "german",
    "gnome",
    "gnustep",
    "graphics",
    "hamradio",
    "haskell",
    "hebrew",
    "hungarian",
    "irc",
    "japanese",
    "java",
    "kde",
    "kld",
    "korean",
    "lang",
    "linux",
    "lisp",
    "mail",
    "mate",
    "math",
    "mbone",
    "misc",
    "multimedia",
    "net",
    "net-im",
    "net-mgmt",
    "net-p2p",
    "net-vpn",
    "news",
    "parallel",
    "pear",
    "perl5",
    "plan9",
    "polish",
    "ports-mgmt",
    "portuguese",
    "print",
    "python",
    "ruby",
    "rubygems",
    "russian",
    "scheme",
    "science",
    "security",
    "shells",
    "spanish",
    "sysutils",
    "tcl",
    "textproc",
    "tk",
    "ukrainian",
    "vietnamese",
    "wayland",
    "windowmaker",
    "www",
    "x11",
    "x11-clocks",
    "x11-drivers",
    "x11-fm",
    "x11-fonts",
    "x11-servers",
    "x11-themes",
    "x11-toolkits",
    "x11-wm",
    "xfce",
    "zope"
};

static std::unordered_set<std::string> unmappedPrimaryCategories;
// These are Ports primary categories that do not correspond to any of the PackageKit ones.
// The whole list is produced by running
// ls -m /usr/ports | awk '{split($0,a,", "); for (i in a) printf "\"%s\",\n", a[i]}' | grep '"[a-z]'
// Then manually remove categories that are mapped to advertised_groups in PortsCategoriesToPKGroup()
static const char* unmappedPrimaryCategoriesData[] = {
    "arabic",
    "archivers",
    "astro",
    "audio",
    "benchmarks",
    "cad",
    "chinese",
    "converters",
    "databases",
    "distfiles",
    "dns",
    "finance",
    "french",
    "ftp",
    "deskutils",
    "german",
    "hebrew",
    "hungarian",
    "irc",
    "japanese",
    "korean",
    "lang",
    "java",
    "net-im",
    "news",
    "polish",
    "portuguese",
    "russian",
    "shells",
    "ports-mgmt",
    "textproc",
    "ukrainian",
    "vietnamese",
    "x11",
    "x11-clocks",
    "x11-drivers",
    "x11-fm",
    "x11-servers",
    "x11-themes",
    "x11-toolkits",
    "x11-wm"
};

static void InitCategories()
{
#define NELEMS(x) (sizeof(x) / sizeof((x)[0]))
    for (unsigned i = 0; i < NELEMS(portsCategoriesData); i++)
        portsCategories.insert(portsCategoriesData[i]);
    for (unsigned i = 0; i < NELEMS(unmappedPrimaryCategoriesData); i++)
        unmappedPrimaryCategories.insert(unmappedPrimaryCategoriesData[i]);
#undef NELEMS
}

static std::unordered_set<const char*> PKGroupToPortsCategories(PkGroupEnum pk_group) {
    // Is there a way to check this statically?
    g_assert (pk_bitfield_contain (advertised_groups, pk_group));

    if (portsCategories.empty())
        InitCategories();

    std::unordered_set<const char*> ret;
    // Is there a way to check this statically?
#define CHECKED_INSERT(str) \
        g_assert (portsCategories.count(str) != 0); \
        ret.insert (str)
#define CHECKED_INSERT_BREAK(str) \
        CHECKED_INSERT (str); \
        break
    switch (pk_group) {
        case PK_GROUP_ENUM_ACCESSIBILITY: CHECKED_INSERT_BREAK("accessibility");
        case PK_GROUP_ENUM_EDUCATION: CHECKED_INSERT_BREAK("education");
        case PK_GROUP_ENUM_GAMES: CHECKED_INSERT_BREAK("games");
        case PK_GROUP_ENUM_GRAPHICS: CHECKED_INSERT_BREAK("graphics");
        case PK_GROUP_ENUM_INTERNET:
            CHECKED_INSERT("mail");
            CHECKED_INSERT_BREAK("www");
        case PK_GROUP_ENUM_OFFICE:
            CHECKED_INSERT("editors");
            CHECKED_INSERT_BREAK("print");
        case PK_GROUP_ENUM_OTHER: CHECKED_INSERT_BREAK("misc");
        case PK_GROUP_ENUM_PROGRAMMING:
            CHECKED_INSERT("devel");
            CHECKED_INSERT("haskell");
            CHECKED_INSERT("ruby");
            CHECKED_INSERT("lisp");
            CHECKED_INSERT_BREAK("python");
        case PK_GROUP_ENUM_MULTIMEDIA: CHECKED_INSERT_BREAK("multimedia");
        case PK_GROUP_ENUM_SYSTEM: CHECKED_INSERT_BREAK("sysutils");
        case PK_GROUP_ENUM_DESKTOP_GNOME: CHECKED_INSERT_BREAK("gnome");
        case PK_GROUP_ENUM_DESKTOP_KDE: CHECKED_INSERT_BREAK("kde");
        case PK_GROUP_ENUM_DESKTOP_XFCE: CHECKED_INSERT_BREAK("xfce");
        case PK_GROUP_ENUM_DESKTOP_OTHER:
            CHECKED_INSERT("budgie");
            CHECKED_INSERT("enlightenment");
            CHECKED_INSERT_BREAK("mate");
        case PK_GROUP_ENUM_FONTS: CHECKED_INSERT_BREAK("x11-fonts");
        case PK_GROUP_ENUM_VIRTUALIZATION:
            CHECKED_INSERT("linux");
            CHECKED_INSERT_BREAK("emulators");
        case PK_GROUP_ENUM_SECURITY: CHECKED_INSERT_BREAK("security");
        case PK_GROUP_ENUM_COMMUNICATION: CHECKED_INSERT_BREAK("comms");
        case PK_GROUP_ENUM_NETWORK:
            CHECKED_INSERT("net");
            CHECKED_INSERT("net-mgmt");
            CHECKED_INSERT("net-vpn");
            CHECKED_INSERT_BREAK("net-p2p");
        case PK_GROUP_ENUM_MAPS: CHECKED_INSERT_BREAK("geography");
        case PK_GROUP_ENUM_SCIENCE:
            CHECKED_INSERT("biology");
            CHECKED_INSERT("math");
            CHECKED_INSERT_BREAK("science");
        default: break;
    }
#undef CHECKED_INSERT
#undef CHECKED_INSERT_BREAK
    return ret;
}

static PkGroupEnum PortsCategoriesToPKGroup(gchar** categories)
{
    std::unordered_set<std::string> cats;

    guint size = g_strv_length (categories);
    for (guint i = 0; i < size; i++)
        cats.insert(categories[i]);

    if (unmappedPrimaryCategories.empty())
        InitCategories();

    bool isPrimaryCategoryMapped = unmappedPrimaryCategories.count(categories[0]) == 0;

#define RETURN_CHECKED(val) \
        { \
        g_assert (pk_bitfield_contain (advertised_groups, val)); \
        return val; \
        }

    // hamradio is just probably about comms
    if (cats.count("hamradio"))
        RETURN_CHECKED(PK_GROUP_ENUM_COMMUNICATION);
    if (cats.count("gnome"))
        RETURN_CHECKED(PK_GROUP_ENUM_DESKTOP_GNOME);
    if (cats.count("kde"))
        RETURN_CHECKED(PK_GROUP_ENUM_DESKTOP_KDE);
    if (cats.count("xfce"))
        RETURN_CHECKED(PK_GROUP_ENUM_DESKTOP_XFCE);
    if (cats.count("budgie") || cats.count("enlightenment")
        || cats.count("mate"))
        RETURN_CHECKED(PK_GROUP_ENUM_DESKTOP_OTHER);
    // Packages with "afterstep" category that also don't fall into advertised_groups
    if (cats.count("afterstep") && !isPrimaryCategoryMapped)
        RETURN_CHECKED(PK_GROUP_ENUM_DESKTOP_OTHER);
    // Programming language packages with "devel" are probably libraries
    if (cats.count("devel") && (cats.count("java")
        || cats.count("haskell") || cats.count("python")
        || cats.count("ruby") || cats.count("lisp")))
        RETURN_CHECKED(PK_GROUP_ENUM_PROGRAMMING);
    // Linux packages without a primary category known to us go to generic VIRTUALIZATION
    if (cats.count("linux") && !isPrimaryCategoryMapped)
        RETURN_CHECKED(PK_GROUP_ENUM_VIRTUALIZATION);

    if (cats.count("accessibility"))
        RETURN_CHECKED(PK_GROUP_ENUM_ACCESSIBILITY);
    if (cats.count("comms"))
        RETURN_CHECKED(PK_GROUP_ENUM_COMMUNICATION);
    if (cats.count("education"))
        RETURN_CHECKED(PK_GROUP_ENUM_EDUCATION);
    if (cats.count("multimedia"))
        RETURN_CHECKED(PK_GROUP_ENUM_MULTIMEDIA);
    if (cats.count("x11-fonts"))
        RETURN_CHECKED(PK_GROUP_ENUM_FONTS);
    if (cats.count("games"))
        RETURN_CHECKED(PK_GROUP_ENUM_GAMES);
    if (cats.count("graphics"))
        RETURN_CHECKED(PK_GROUP_ENUM_GRAPHICS);
    if (cats.count("mail") || cats.count("www")
        || cats.count("dns"))
        RETURN_CHECKED(PK_GROUP_ENUM_INTERNET);
    if (cats.count("net") || cats.count("net-mgmt")
        || cats.count("net-vpn") || cats.count("net-p2p"))
        RETURN_CHECKED(PK_GROUP_ENUM_NETWORK);
    if (cats.count("geography"))
        RETURN_CHECKED(PK_GROUP_ENUM_MAPS);
    if (cats.count("biology") || cats.count("math")
        || cats.count("science"))
        RETURN_CHECKED(PK_GROUP_ENUM_SCIENCE);

#undef RETURN_CHECKED
    return PK_GROUP_ENUM_UNKNOWN;
}

static void backendJobPackageFromPkg (PkBackendJob *job, struct pkg* pkg, std::optional<PkInfoEnum> typeOverride = std::nullopt);

static void
pk_freebsd_search(PkBackendJob *job, PkBitfield filters, gchar **values);

void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
    InitAdvertisedGroups();
}

void
pk_backend_destroy (PkBackend *backend)
{
}

PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
    return advertised_groups;
}

PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
    return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
        PK_FILTER_ENUM_NOT_INSTALLED,
        -1);
}

gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
    const gchar *mime_types[] = {
                "application/x-xar",
                NULL };
    return g_strdupv ((gchar **) mime_types);
}

void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
    PkBackendFreeBSDJobData* job_data = reinterpret_cast<PkBackendFreeBSDJobData*> (pk_backend_job_get_user_data (job));

    if (job_data) {
        g_cancellable_cancel (job_data->cancellable);
    }
}

void
pk_backend_depends_on (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_allow_cancel (job, TRUE);

    // TODO
    if (recursive)
        g_warning ("depends_on: recursive is not yet supported");

    // TODO: what filters could we possibly get there?
    g_warning("depends_on: got filters %s", pk_filter_bitfield_to_string(filters));

    pkgdb_t db_type = PKGDB_MAYBE_REMOTE;
    // Open local DB only when filters require only installed packages
    // TODO: See below the "pkgdb_query" vs "pkgdb_repo_search" comment
    if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED)
        && !pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED))
        db_type = PKGDB_DEFAULT;

    PackageDatabase pkgDb (job, PKGDB_LOCK_READONLY, db_type);

    guint size = g_strv_length (package_ids);
    for (guint i = 0; i < size; i++) {
        PackageView pkgView(package_ids[i]);
        struct pkg *pkg = NULL;
        // TODO: take filters into account
        struct pkgdb_it* it = pkgdb_all_search (pkgDb.handle(), pkgView.nameversion(), MATCH_EXACT, FIELD_NAMEVER, FIELD_NAMEVER, NULL);

        while (pkgdb_it_next (it, &pkg, PKG_LOAD_BASIC | PKG_LOAD_DEPS) == EPKG_OK) {
            PackageView pkgView(pkg);

            char* dep_namevers0 = NULL;
            pkg_asprintf(&dep_namevers0, "%d%{%dn;%dv;%}", pkg);
            gchar** dep_namevers = g_strsplit (dep_namevers0, ";", 0);
            // delete both pointers by capturing a closure
            auto deps_deleter = deleted_unique_ptr<void>(reinterpret_cast<void*>(0xDEADC0DE), [dep_namevers0, dep_namevers](void* p) {
                free (dep_namevers0);
                g_strfreev (dep_namevers);
            });

            auto pk_type = db_type == PKGDB_DEFAULT ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;

            guint size2 = g_strv_length (dep_namevers);
            size2 -= size % 2;
            for (guint j = 0; j < size2; j+=2) {
                gchar* dep_id = pk_package_id_build (dep_namevers[j], dep_namevers[j+1], pkgView.arch(), pkgView.repository());

                pk_backend_job_package (job, pk_type, dep_id, ""); // TODO: we report an empty string instead of comment here

                g_free (dep_id);
                if (pk_backend_job_is_cancelled (job))
                    return;
            }
        }
    }
}

void
pk_backend_get_details_local (PkBackend *backend, PkBackendJob *job, gchar **files)
{
    g_error("pk_backend_get_details_local not implemented yet");
}

void
pk_backend_get_files_local (PkBackend *backend, PkBackendJob *job, gchar **_files)
{
    g_error("pk_backend_get_files_local not implemented yet");
}

void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    PackageDatabase pkgDb (job);

    guint size = g_strv_length (package_ids);
    for (guint i = 0; i < size; i++) {
        PackageView pkgView(package_ids[i]);
        pkgdb_it* it = pkgdb_all_search (pkgDb.handle(), pkgView.nameversion(), MATCH_EXACT, FIELD_NAMEVER, FIELD_NAMEVER, NULL);
        pkg* pkg = NULL;

        while (pkgdb_it_next (it, &pkg, PKG_LOAD_BASIC | PKG_LOAD_CATEGORIES | PKG_LOAD_LICENSES) == EPKG_OK) {
            PackageView pkgView(pkg);
            PkGroupEnum group = PortsCategoriesToPKGroup(pkgView.categories());
            pk_backend_job_details_full (job, package_ids[i],
                                    pkgView.comment(),
                                    pkgView.license(),
                                    group,
                                    pkgView.description(),
                                    pkgView.url(),
                                    pkgView.flatsize(),
                                    pkgView.compressedsize()); // TODO: check if already downloaded

            if (pk_backend_job_is_cancelled (job))
                break;
        }
    }
}

// TODO: This requires pkgbase support
// void
// pk_backend_get_distro_upgrades (PkBackend *backend, PkBackendJob *job)
// {
// }

void
pk_backend_get_files (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
    g_error("pk_backend_get_files not implemented yet");
}

void
pk_backend_required_by (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
    g_error("pk_backend_required_by not implemented yet");
}

void
pk_backend_get_update_detail (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
    PKJobFinisher jf (job);
    PackageDatabase pkgDb (job);

    Jobs jobs(PKG_JOBS_UPGRADE, pkgDb.handle(), "update_detail");
    jobs << PKG_FLAG_PKG_VERSION_TEST << PKG_FLAG_DRY_RUN;

    guint size = g_strv_length (package_ids);
    for (guint i = 0; i < size; i++) {
        PackageView pkg (package_ids[i]);
        gchar* pkg_namever = pkg.nameversion();
        jobs.add(MATCH_GLOB, &pkg_namever, 1);
    }

    // TODO: handle reponame?

    if (jobs.solve() == 0)
        return; // no updates available

    gchar_ptr_vector updates;
    gchar_ptr_vector obsoletes;
    gchar		**vendor_urls = NULL;
    gchar		**bugzilla_urls = NULL;
    gchar		**cve_urls = NULL;
    PkRestartEnum	 restart = PK_RESTART_ENUM_NONE;
    const gchar	*update_text = NULL;
    const gchar	*changelog = NULL;
    PkUpdateStateEnum state = PK_UPDATE_STATE_ENUM_UNKNOWN;
    const gchar	*issued = NULL;
    const gchar	*updated = issued;

#define SAFE_SHOW(str, it) g_warning(str, it.oldPkgHandle() ? it.oldPkgView().nameversion() : "NULL", it.newPkgHandle() ? it.newPkgView().nameversion() : "NULL")
    for (auto it = jobs.begin(); it != jobs.end(); ++it) {
        switch (it.itemType()) {
        case PKG_SOLVED_INSTALL:
            SAFE_SHOW("SOLVED_INSTALL, old: %s, new: %s", it);
            updates.push_back(g_strdup(it.newPkgView().packageKitId()));
            break;
        case PKG_SOLVED_DELETE:
            SAFE_SHOW("SOLVED_DELETE, old: %s, new: %s", it);
            obsoletes.push_back(g_strdup(it.oldPkgView().packageKitId()));
            break;
        case PKG_SOLVED_UPGRADE:
            SAFE_SHOW("SOLVED_UPGRADE, old: %s, new: %s", it);
            updates.push_back(g_strdup(it.oldPkgView().packageKitId()));
            break;
        case PKG_SOLVED_UPGRADE_REMOVE:
            SAFE_SHOW("SOLVED_UPGRADE_REMOVE, old: %s, new: %s", it);
            obsoletes.push_back(g_strdup(it.oldPkgView().packageKitId()));
            break;
        case PKG_SOLVED_FETCH:
            SAFE_SHOW("SOLVED_FETCH, old: %s, new: %s", it);
            break;
        case PKG_SOLVED_UPGRADE_INSTALL:
            SAFE_SHOW("SOLVED_UPGRADE_INSTALL, old: %s, new: %s", it);
            updates.push_back(g_strdup(it.oldPkgView().packageKitId()));
            break;
        default:
            break;
        }
    }

    updates.push_back(nullptr);
    obsoletes.push_back(nullptr);

    for (guint i = 0; i < size; i++) {
        pk_backend_job_update_detail (job, package_ids[i],
                                    updates.data(),
                                    obsoletes.data(),
                                    vendor_urls,
                                    bugzilla_urls,
                                    cve_urls,
                                    restart,
                                    update_text,
                                    changelog,
                                    state,
                                    issued,
                                    updated);
    }
}

void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    // TODO: what filters could we possibly get there?
    if (! (filters == 0
        || filters == pk_bitfield_value(PK_FILTER_ENUM_UNKNOWN)
        || filters == pk_bitfield_value(PK_FILTER_ENUM_NONE)
        || filters == pk_bitfield_value(PK_FILTER_ENUM_NEWEST)
    ))
        g_error("get_updates: unexpected filters %s", pk_filter_bitfield_to_string(filters));

    PackageDatabase pkgDb (job);
    Jobs jobs (PKG_JOBS_UPGRADE, pkgDb.handle(), "get_updates");

    jobs << PKG_FLAG_PKG_VERSION_TEST << PKG_FLAG_DRY_RUN;

    // TODO: handle reponame?

    if (jobs.solve() == 0)
        return; // no updates available

    for (auto it = jobs.begin(); it != jobs.end(); ++it) {
        // Do not report packages that will be removed by the upgrade
        // and that are installed for the first time
        if (it.itemType() == PKG_SOLVED_UPGRADE_REMOVE
            || it.itemType() == PKG_SOLVED_DELETE
            || it.itemType() == PKG_SOLVED_INSTALL)
            continue;
        backendJobPackageFromPkg (job, it.newPkgHandle(), PK_INFO_ENUM_NORMAL);
    }
}

void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
    PKJobFinisher jf (job);

    // TODO: handle all of these
    if (! (transaction_flags == 0
            || pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED)
            || pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)
            || pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD))
        )
        g_error("install_packages: unexpected transaction_flags %s", pk_transaction_flag_bitfield_to_string(transaction_flags));

    PackageDatabase pkgDb (job, PKGDB_LOCK_ADVISORY);
    Jobs jobs (PKG_JOBS_INSTALL, pkgDb.handle(), "install_packages");

    jobs << PKG_FLAG_PKG_VERSION_TEST;

    if (pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD))
        jobs << PKG_FLAG_SKIP_INSTALL;

    guint size = g_strv_length (package_ids);
    std::vector<gchar*> names;
    names.reserve (size);
    for (guint i = 0; i < size; i++) {
        PackageView pkg(package_ids[i]);
        names.push_back(g_strdup(pkg.nameversion()));
    }

    jobs.add (MATCH_GLOB, names);

    pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);

    if (jobs.solve() == 0)
        return; // nothing can be installed, for example because everything requested is already installed


    for (auto it = jobs.begin(); it != jobs.end(); ++it) {
        if (it.itemType() == PKG_SOLVED_DELETE) {
            g_warning ("install_packages: have to remove some packages");
            continue;
        }
        backendJobPackageFromPkg (job, it.newPkgHandle(), PK_INFO_ENUM_NORMAL);
    }

    if (pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE))
        return;

    pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);

    jobs.apply();

    for (auto* str : names)
        g_free (str);
}

void
pk_backend_install_signature (PkBackend *backend, PkBackendJob *job, PkSigTypeEnum type,
                              const gchar *key_id, const gchar *package_id)
{
    g_error("pk_backend_install_signature not implemented yet");
}

void
pk_backend_install_files (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **full_paths)
{
    g_error("pk_backend_install_files not implemented yet");
}

static void
pk_backend_refresh_cache_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
    PKJobCanceller jc (job);

    gboolean force;
    g_variant_get (params, "(b)", &force);

    PackageDatabase pkgDb(job, PKGDB_LOCK_EXCLUSIVE);

    uint repoCount;
    int repoNumber = 0;
    int progressStartNumber = -1;
    pkgDb.setEventHandler([job, &repoCount, &repoNumber, &progressStartNumber, &jc](pkg_event* ev) {
        const uint progressStartsPerRepo = 3; // TODO: fix this upstream somehow

        switch (ev->type) {
            case PKG_EVENT_PROGRESS_START:
                progressStartNumber++;
                if (progressStartNumber == progressStartsPerRepo) {
                    progressStartNumber = 0;
                    repoNumber++;
                }
                break;
            case PKG_EVENT_PROGRESS_TICK:
            {
                uint progress = (ev->e_progress_tick.current * 100) / ev->e_progress_tick.total;
                progress /= progressStartsPerRepo;
                progress += (100 * progressStartNumber) / progressStartsPerRepo;
                progress /= repoCount;
                progress += (100 * repoNumber) / repoCount;
                pk_backend_job_set_percentage (job, progress);
                break;
            }
            default:
                break;
        }
        // TODO: libpkg doesn't yet support cancelling
        //jc.cancelIfRequested();
        (void) jc;
    });

    int ret = pkgdb_access(PKGDB_MODE_WRITE|PKGDB_MODE_CREATE,
                PKGDB_DB_REPO);
    switch (ret) {
        case EPKG_OK:
            break;
        case EPKG_ENOACCESS:
            pk_backend_job_error_code (job, PK_ERROR_ENUM_CANNOT_WRITE_REPO_CONFIG,
                "The package DB directory isn't writable");
            return;
        case EPKG_INSECURE:
            pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR,
                "The package DB directory is writable by non-root users");
            return;
        default:
            pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR,
                "General libpkg failure");
            return;
    }

    pk_backend_job_set_percentage (job, 0);

    repoCount = pkg_repos_activated_count();
    if (repoCount == 0) {
        pk_backend_job_set_percentage (job, 100);
        g_warning("No active remote repositories configured");
        return;
    }

    struct pkg_repo *r = NULL;
    while (pkg_repos(&r) == EPKG_OK) {
        if (!pkg_repo_enabled(r))
                continue;
        if (jc.cancelIfRequested())
            break;
        pkg_update (r, force);
    }

    pk_backend_job_set_percentage (job, 100);
}

void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
    // No need for PKJobFinisher here as we are using pk_backend_job_thread_create
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);

    /* check network state */
    if (!pk_backend_is_online (backend)) {
        pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_NETWORK, "Cannot check when offline");
        return;
    }

    pk_backend_job_thread_create (job, pk_backend_refresh_cache_thread, NULL, NULL);
}

void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **packages)
{
    PKJobFinisher jf (job);

    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    match_t match = MATCH_EXACT;
    guint size = g_strv_length (packages);
    gchar_ptr_vector names;
    names.reserve(size);
    for (guint i = 0; i < size; i++) {
        gchar* package = packages[i];

        if (pk_package_id_check(package) == false) {
            // if pkgid isn't a valid package ID, treat it as glob "pkgname-*"
            names.push_back(g_strconcat(package, "-*", NULL));
            match = MATCH_GLOB;
        }
        else {
            PackageView pkgView (package);
            names.push_back(g_strdup(pkgView.nameversion()));
        }
    }

    pkgdb_t dbType = PKGDB_MAYBE_REMOTE;
    // save ourselves some work by skipping remote DBs if we only want installed packages
    if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED)
        && !pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED))
        dbType = PKGDB_DEFAULT;

    if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED))
        dbType = PKGDB_REMOTE;

    PackageDatabase pkgDb (job, PKGDB_LOCK_READONLY, dbType);

    for (auto* name : names) {
        pkgdb_it* it = pkgdb_all_search (pkgDb.handle(), name, match, FIELD_NAMEVER, FIELD_NAMEVER, NULL);
        struct pkg *pkg = NULL;

        while (pkgdb_it_next (it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
            // We'll be always getting installed packages from pkgdb_it_next,
            // but PackageKit sometimes asks only about available ones
            if (filters && !pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED) && pkg_type(pkg) == PKG_INSTALLED)
                continue;
            backendJobPackageFromPkg (job, pkg);
        }
    }
}

void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job,
                            PkBitfield transaction_flags,
                            gchar **package_ids,
                            gboolean allow_deps,
                            gboolean autoremove)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);

    guint size = g_strv_length (package_ids);
    // TODO: What should happen when remove_packages() is called with an empty package_ids?
    g_assert (size > 0);

    PackageDatabase pkgDb (job, PKGDB_LOCK_ADVISORY);
    Jobs jobs(PKG_JOBS_DEINSTALL, pkgDb.handle(), "remove_packages");

    if (allow_deps)
        jobs << PKG_FLAG_RECURSIVE;

    std::vector<gchar*> names;
    names.reserve (size);
    for (guint i = 0; i < size; i++) {
        PackageView pkg(package_ids[i]);
        names.push_back(g_strdup(pkg.nameversion()));
    }

    jobs.add(MATCH_GLOB, names);

    pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);

    jobs.solve();

    // TODO: handle locked pkgs
    g_assert (!jobs.hasLockedPackages());
    g_assert (jobs.count() != 0);

    // TODO: We need https://github.com/freebsd/pkg/issues/1271 to be fixed
    // to support "autoremove"

    if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE))
        return;

    pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);

    jobs.apply();

    pkgdb_compact (pkgDb.handle());
}

void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_freebsd_search(job, filters, values);
}

void
pk_backend_search_files (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_freebsd_search(job, filters, values);
}

void
pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_freebsd_search(job, filters, values);
}

void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_freebsd_search (job, filters, values);
}

void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);

    PackageDatabase pkgDb (job, PKGDB_LOCK_ADVISORY, PKGDB_REMOTE);

    Jobs jobs (PKG_JOBS_UPGRADE, pkgDb.handle(), "update_packages");
    jobs << PKG_FLAG_PKG_VERSION_TEST;

    uint size = g_strv_length (package_ids);
    for (uint i = 0; i < size; i++) {
        PackageView pkg (package_ids[i]);
        gchar* pkg_namever = pkg.nameversion();

        jobs.add (MATCH_EXACT, &pkg_namever, 1);
    }

    if (jobs.solve() == 0)
        return;

    jobs.apply();
}

void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    g_assert(!pkg_initialized());
    if (pkg_ini(NULL, NULL, PKG_INIT_FLAG_USE_IPV4) != EPKG_OK)
        g_error("get_repo_list: pkg_ini failure");

    pkg_repo* repo = NULL;
    while (pkg_repos(&repo) == EPKG_OK) {
        const gchar* id = pkg_repo_name (repo);
        const gchar* descr = pkg_repo_url (repo);
        gboolean enabled = pkg_repo_enabled (repo);

        pk_backend_job_repo_detail (job, id, descr, enabled);
    }

    pkg_shutdown();
}

void
pk_backend_repo_set_data (PkBackend *backend, PkBackendJob *job, const gchar *rid, const gchar *parameter, const gchar *value)
{
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REQUEST);
    g_warning ("REPO '%s' PARAMETER '%s' TO '%s'", rid, parameter, value);
    pk_backend_job_finished (job);
    g_error("pk_backend_repo_set_data not implemented yet");
}

void
pk_backend_what_provides (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REQUEST);
    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_percentage (job, 0);
    g_error("pk_backend_what_provides not implemented yet");
}

void
pk_backend_get_packages (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    gchar* values[2] = { (gchar*)(".*"), NULL };
    pk_freebsd_search (job, filters, values);
}

void
pk_backend_download_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory0)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
    pk_backend_job_set_allow_cancel (job, TRUE);

    PackageDatabase pkgDb (job);

    std::string directory (directory0);
    std::string cacheDir = "/var/cache/pkg"; // TODO: query this from libpkg

    uint size = g_strv_length (package_ids);
    for (uint i = 0; i < size; i++) {
        Jobs jobs(PKG_JOBS_FETCH, pkgDb.handle(), "download_packages");

        // TODO: set reponame when libpkg start reporting it
        // if (reponame != NULL && pkg_jobs_set_repository(jobs, reponame) != EPKG_OK)
        // 	goto cleanup;

        if (!directory.empty()) {
            // This flag is required to convince libpkg to download
            // into an arbitrary directory
            jobs << PKG_FLAG_FETCH_MIRROR;
            jobs.setDestination(directory);
        }

        PackageView pkg (package_ids[i]);
        gchar* pkg_namever = pkg.nameversion();

        jobs.add(MATCH_EXACT, &pkg_namever, 1);

        if (jobs.solve() == 0)
            continue; // the package doesn't need to be downloaded

        jobs.apply();

        std::string filepath = directory.empty()
                             ? cacheDir + "/" + pkg_namever + ".pkg"
                             : directory + "/All/" + pkg_namever + ".pkg";

        gchar* files[] = {NULL, NULL};
        files[0] = const_cast<gchar*>(filepath.c_str());
        pk_backend_job_files(job, pkg.packageKitId(), files);

        if (pk_backend_job_is_cancelled (job))
            break;
    }
}

// TODO: Do we want "freebsd-update" support here?
// void
// pk_backend_upgrade_system (PkBackend *backend,
//                            PkBackendJob *job,
//                            PkBitfield transaction_flags,
//                            const gchar *distro_id,
//                            PkUpgradeKindEnum upgrade_kind)
// {
//     pk_backend_job_finished (job);
//     g_error("pk_backend_upgrade_system not implemented yet");
// }

void
pk_backend_repair_system (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags)
{
    pk_backend_job_finished (job);
    g_error("pk_backend_repair_system not implemented yet");
}

void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
}

void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
    PkBackendFreeBSDJobData *job_data = reinterpret_cast<PkBackendFreeBSDJobData*> (pk_backend_job_get_user_data (job));

    // only cancellable jobs allocate job_data
    if (job_data) {
        reinterpret_cast<PKJobCanceller*>(job_data->canceller)->abort();
        g_object_unref (job_data->cancellable);
        g_free (job_data);
    }
}

gboolean
pk_backend_supports_parallelization (PkBackend *backend)
{
    return FALSE;
}

const gchar *
pk_backend_get_description (PkBackend *backend)
{
    return "FreeBSD";
}

const gchar *
pk_backend_get_author (PkBackend *backend)
{
    return "Gleb Popov <arrowd@FreeBSD.org>";
}

static void
pk_freebsd_search(PkBackendJob *job, PkBitfield filters, gchar **values)
{
    PKJobFinisher jf (job);

    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    // TODO: what can we possibly get in filters?
    // We ignore ~installed as there is no support in libpkg
    // We ignore arch for now
    if (! (filters == 0
        || pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED)
        || pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED)
        || pk_bitfield_contain(filters, PK_FILTER_ENUM_ARCH)
    )) {
        g_error("freebsd_search: unexpected filters %s", pk_filter_bitfield_to_string(filters));
    }

    pkgdb_t db_type = PKGDB_REMOTE;
    // Open local DB only when filters require only installed packages
    // TODO: I don't like it
    if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED)
        && !pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED))
        db_type = PKGDB_DEFAULT;

    PackageDatabase pkgDb (job, PKGDB_LOCK_READONLY, db_type);

    g_free_deleted_unique_ptr<gchar> pattern = g_free_deleted_unique_ptr<gchar> (g_strjoinv ("|", values));
    match_t match_type = MATCH_REGEX;
    pkgdb_field searched_field = FIELD_NAMEVER;

    switch (pk_backend_job_get_role (job))
    {
    case PK_ROLE_ENUM_SEARCH_DETAILS:
        // TODO: can we search both comment and pkg-descr? https://github.com/freebsd/pkg/issues/2118
        searched_field = FIELD_COMMENT;
        break;
    case PK_ROLE_ENUM_SEARCH_GROUP:
    {
        searched_field = FIELD_ORIGIN;
        gchar_ptr_vector sanitizedGroups;

        guint size = g_strv_length (values);
        for (guint i = 0; i < size; i++) {
            PkGroupEnum pk_group = pk_group_enum_from_string (values[i]);
            if (pk_group != PK_GROUP_ENUM_UNKNOWN)
                for (const char* category : PKGroupToPortsCategories(pk_group))
                    sanitizedGroups.push_back(g_strdup (category));
            else {
                if (portsCategories.count (values[i]) == 0)
                    g_warning ("freebsd_search: Unknown group requested: %s", values[i]);
                sanitizedGroups.push_back(g_strdup (values[i]));
            }
        }

        // this NULL is for g_strjoinv
        sanitizedGroups.push_back(NULL);
        pattern = g_free_deleted_unique_ptr<gchar> (g_strjoinv ("|", sanitizedGroups.data()));
        break;
    }
    case PK_ROLE_ENUM_SEARCH_FILE:
        // TODO: we don't support searching for packages that provide given file
        return;
    default: break;
    }

    // TODO: take filters into account
    struct pkgdb_it* it = pkgdb_all_search (pkgDb.handle(), pattern.get(), match_type, searched_field, FIELD_NAMEVER, NULL);
    struct pkg *pkg = NULL;

    while (pkgdb_it_next (it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
        // TODO: should we deduplicate two identical items - installed and available one?
        backendJobPackageFromPkg (job, pkg, std::nullopt);

        if (pk_backend_job_is_cancelled (job))
            break;
    }
}

static void backendJobPackageFromPkg (PkBackendJob *job, struct pkg* pkg, std::optional<PkInfoEnum> typeOverride) {
	PkInfoEnum pk_type = pkg_type (pkg) == PKG_INSTALLED
						? PK_INFO_ENUM_INSTALLED
						: PK_INFO_ENUM_AVAILABLE;

	if (typeOverride.has_value())
		pk_type = typeOverride.value();

    PackageView pkgView(pkg);

	pk_backend_job_package (job, pk_type, pkgView.packageKitId(), pkgView.comment());
}
