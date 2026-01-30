/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2014 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
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

#include <apk/apk_crypto.h>
#include <apk/apk_defines.h>
#include <apk/apk_hash.h>
#include <apk/apk_print.h>
#include <apk/apk_query.h>
#include <glib.h>
#include <packagekit-glib2/pk-debug.h>
#include <pk-backend.h>

#include <apk-polkit-client.h>
#include <apk/apk_blob.h>
#include <apk/apk_context.h>
#include <apk/apk_database.h>
#include <apk/apk_package.h>

#include "pk-backend-alpine-apk-private.h"
#include "pk-backend-job.h"

#include <gmodule.h>
#include <string.h>

/*
g_module_symbol (handle, "pk_backend_cancel", (gpointer *)&desc->cancel);
                g_module_symbol (handle, "pk_backend_destroy", (gpointer
*)&desc->destroy); g_module_symbol (handle, "pk_backend_download_packages",
(gpointer *)&desc->download_packages); g_module_symbol (handle,
"pk_backend_get_categories", (gpointer *)&desc->get_categories); g_module_symbol
(handle, "pk_backend_depends_on", (gpointer *)&desc->depends_on);
                g_module_symbol (handle, "pk_backend_get_details", (gpointer
*)&desc->get_details); g_module_symbol (handle, "pk_backend_get_details_local",
(gpointer *)&desc->get_details_local); g_module_symbol (handle,
"pk_backend_get_files_local", (gpointer *)&desc->get_files_local);
                g_module_symbol (handle, "pk_backend_get_distro_upgrades",
(gpointer *)&desc->get_distro_upgrades); g_module_symbol (handle,
"pk_backend_get_files", (gpointer *)&desc->get_files); g_module_symbol (handle,
"pk_backend_get_filters", (gpointer *)&desc->get_filters); g_module_symbol
(handle, "pk_backend_get_groups", (gpointer *)&desc->get_groups);
                g_module_symbol (handle, "pk_backend_get_mime_types", (gpointer
*)&desc->get_mime_types); g_module_symbol (handle,
"pk_backend_supports_parallelization", (gpointer
*)&desc->supports_parallelization); g_module_symbol (handle,
"pk_backend_get_packages", (gpointer *)&desc->get_packages); g_module_symbol
(handle, "pk_backend_get_repo_list", (gpointer *)&desc->get_repo_list);
                g_module_symbol (handle, "pk_backend_required_by", (gpointer
*)&desc->required_by); g_module_symbol (handle, "pk_backend_get_roles",
(gpointer *)&desc->get_roles); g_module_symbol (handle,
"pk_backend_get_provides", (gpointer *)&desc->get_provides); g_module_symbol
(handle, "pk_backend_get_update_detail", (gpointer *)&desc->get_update_detail);
                g_module_symbol (handle, "pk_backend_get_updates", (gpointer
*)&desc->get_updates); g_module_symbol (handle, "pk_backend_initialize",
(gpointer *)&desc->initialize); g_module_symbol (handle,
"pk_backend_install_files", (gpointer *)&desc->install_files); g_module_symbol
(handle, "pk_backend_install_packages", (gpointer *)&desc->install_packages);
                g_module_symbol (handle, "pk_backend_install_signature",
(gpointer *)&desc->install_signature); g_module_symbol (handle,
"pk_backend_refresh_cache", (gpointer *)&desc->refresh_cache); g_module_symbol
(handle, "pk_backend_remove_packages", (gpointer *)&desc->remove_packages);
                g_module_symbol (handle, "pk_backend_repo_enable", (gpointer
*)&desc->repo_enable); g_module_symbol (handle, "pk_backend_repo_set_data",
(gpointer *)&desc->repo_set_data); g_module_symbol (handle,
"pk_backend_repo_remove", (gpointer *)&desc->repo_remove); g_module_symbol
(handle, "pk_backend_resolve", (gpointer *)&desc->resolve); g_module_symbol
(handle, "pk_backend_search_details", (gpointer *)&desc->search_details);
                g_module_symbol (handle, "pk_backend_search_files", (gpointer
*)&desc->search_files); g_module_symbol (handle, "pk_backend_search_groups",
(gpointer *)&desc->search_groups); g_module_symbol (handle,
"pk_backend_search_names", (gpointer *)&desc->search_names); g_module_symbol
(handle, "pk_backend_start_job", (gpointer *)&desc->job_start); g_module_symbol
(handle, "pk_backend_stop_job", (gpointer *)&desc->job_stop); g_module_symbol
(handle, "pk_backend_update_packages", (gpointer *)&desc->update_packages);
                g_module_symbol (handle, "pk_backend_what_provides", (gpointer
*)&desc->what_provides); g_module_symbol (handle, "pk_backend_upgrade_system",
(gpointer *)&desc->upgrade_system); g_module_symbol (handle,
"pk_backend_repair_system", (gpointer *)&desc->repair_system);

                ret = g_module_symbol (handle, "pk_backend_get_author",
(gpointer *)&backend_vfunc); if (ret) desc->author = backend_vfunc (backend);
                ret = g_module_symbol (handle, "pk_backend_get_description",
(gpointer *)&backend_vfunc); if (ret) desc->description = backend_vfunc
(backend);

*/
// https://blog.sebastianwick.net/posts/glib-ownership-best-practices/

static int open_apk(guint apk_flags, ApkContext **ctx_writeback,
                    ApkDatabase **db_writeback) {
  g_autoptr(ApkContext) ctx = g_malloc(sizeof(struct apk_ctx));
  g_autoptr(ApkDatabase) db = g_malloc(sizeof(struct apk_database));
  int result = 0;

  apk_ctx_init(ctx);
  apk_db_init(db, ctx);

  ctx->open_flags = apk_flags;

  result = apk_ctx_prepare(ctx);
  if (result != 0) {
    goto out;
  }

  result = apk_db_open(db);
  if (result != 0) {
    goto out;
  }

  *ctx_writeback = g_steal_pointer(&ctx);
  *db_writeback = g_steal_pointer(&db);
out:
  return result;
}

void free_apk_ctx(struct apk_ctx *ctx) {
  apk_ctx_free(ctx);

  g_free(ctx);
}

void free_apk_database(struct apk_database *db) {
  apk_db_close(db);

  g_free(db);
}

void free_apk_package_array(struct apk_package_array *array) {
  apk_package_array_free(&array);
}

void free_apk_string_array(struct apk_string_array *array) {
  apk_string_array_free(&array);
}

static gchar *convert_apk_package_id(struct apk_package *package) {
  g_autofree gchar *pkg_version = apk_blob_cstr(*package->version);
  // TODO: convert apk-arch to pkgkit arch
  g_autofree gchar *pkg_arch = apk_blob_cstr(*package->arch);
  size_t pkg_name_strlen = strlen(package->name->name) + 1 +
                           strlen(pkg_version) + 1 + strlen(pkg_arch) + 1 + 1;
  g_autofree gchar *pkg_name = g_malloc0(pkg_name_strlen);

  g_snprintf(pkg_name, pkg_name_strlen, "%s;%s;%s;", package->name->name,
             pkg_version, pkg_arch);

  return g_steal_pointer(&pkg_name);
}

static void convert_apk_details(PkBackendJob *job,
                                struct apk_package *package) {
  g_autofree gchar *pkg_id = convert_apk_package_id(package);
  g_autofree gchar *pkg_desc = apk_blob_cstr(*package->description);
  g_autofree gchar *pkg_license = apk_blob_cstr(*package->license);
  g_autofree gchar *pkg_url = apk_blob_cstr(*package->url);
  PkGroupEnum group_enum = PK_GROUP_ENUM_UNKNOWN;

  if (g_str_has_prefix(package->name->name, "font-")) {
    group_enum = PK_GROUP_ENUM_FONTS;
    goto have_guess;
  } else if g_str_has_prefix (package->name->name, "postmarketos-") {
    if (g_strcmp0(package->name->name, "postmarketos-nightly") == 0) {
      group_enum = PK_GROUP_ENUM_REPOS;
    } else {
      group_enum = PK_GROUP_ENUM_VENDOR;
    }

    goto have_guess;
  }

  // locate last part for suffix matching (e.g. -dbg, -dev, -lang)
  do {
    gchar *suffix = package->name->name;
    // goto end of string
    while (*suffix != '\0') {
      ++suffix;
    }
    // walkback unless beginning or '-' is hit
    while (suffix != package->name->name && *suffix != '-') {
      --suffix;
    }
    /* if we're at the beginning, then there's no suffix to match
     * this is allowed as apk's starting letter is more restricted than the
     * rest
     */
    if (suffix == package->name->name) {
      break;
    }

    // swallow '-'
    ++suffix;
    if (g_strcmp0(suffix, "lang") == 0) {
      group_enum = PK_GROUP_ENUM_LOCALIZATION;
      goto have_guess;
    } else if (g_strcmp0(suffix, "dev") == 0 || g_strcmp0(suffix, "dbg") == 0 ||
               g_strcmp0(suffix, "static") == 0 ||
               g_strcmp0(suffix, "libs") == 0) {
      group_enum = PK_GROUP_ENUM_PROGRAMMING;
      goto have_guess;
    } else if (g_strcmp0(suffix, "completion") == 0) {
      if (g_str_has_suffix(package->name->name, "-bash-completion") ||
          g_str_has_suffix(package->name->name, "-zsh-completion") ||
          g_str_has_suffix(package->name->name, "-fish-completion")) {
        group_enum = PK_GROUP_ENUM_PROGRAMMING;
        goto have_guess;
      }
    } else if (g_strcmp0(suffix, "doc") == 0 || g_strcmp0(suffix, "devhelp")) {
      group_enum = PK_GROUP_ENUM_DOCUMENTATION;
      goto have_guess;
    } else if (g_strcmp0(suffix, "openrc") == 0 ||
               g_strcmp0(suffix, "systemd") == 0 ||
               g_strcmp0(suffix, "udev") == 0 ||
               g_strcmp0(suffix, "pyc") == 0) {
      group_enum = PK_GROUP_ENUM_SYSTEM;
      goto have_guess;
    } else if (g_strcmp0(suffix, "nftrules") == 0) {
      group_enum = PK_GROUP_ENUM_SECURITY;
      goto have_guess;
    }

    // guess based on dependencies and provides
    apk_array_foreach_item(provides_item, package->provides) {
      if (g_str_has_prefix(provides_item.name->name, "font-")) {
        group_enum = PK_GROUP_ENUM_FONTS;
        goto have_guess;
      }
    }

  } while (false);

have_guess:

  g_warning("%s", pkg_id);
  pk_backend_job_details(job, pkg_id, pkg_desc, pkg_license, group_enum, NULL,
                         pkg_url, package->installed_size, package->size);
}

static void convert_apk_package(PkBackendJob *job,
                                struct apk_package *package) {
  g_autofree gchar *pkg_id = convert_apk_package_id(package);
  g_autofree gchar *pkg_desc = apk_blob_cstr(*package->description);

  pk_backend_job_package(job,
                         package->ipkg != NULL ? PK_INFO_ENUM_INSTALLED
                                               : PK_INFO_ENUM_UNKNOWN,
                         pkg_id, pkg_desc);
}

void pk_backend_initialize(GKeyFile *conf, PkBackend *backend) {
  pk_debug_add_log_domain(G_LOG_DOMAIN);
}

void pk_backend_destroy(PkBackend *backend) {}

void pk_backend_start_job(PkBackend *backend, PkBackendJob *job) {}
void pk_backend_stop_job(PkBackend *backend, PkBackendJob *job) {}
void pk_backend_cancel(PkBackend *backend, PkBackendJob *job) {}

void pk_backend_get_distro_upgrades(PkBackend *backend, PkBackendJob *job) {
  pk_backend_job_finished(job);
}

void pk_backend_download_packages(PkBackend *backend, PkBackendJob *job,
                                  gchar **package_ids, const gchar *directory) {
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  g_autoptr(ApkPackageArray) package_array = NULL;
  gint result;

  result =
      open_apk(APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE | APK_OPENF_CACHE_WRITE,
               &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

out:
  pk_backend_job_finished(job);
}
void pk_backend_get_categories(PkBackend *backend, PkBackendJob *job) {}
void pk_backend_depends_on(PkBackend *backend, PkBackendJob *job,
                           PkBitfield filters, gchar **package_ids,
                           gboolean recursive) {}
void pk_backend_get_details(PkBackend *backend, PkBackendJob *job,
                            gchar **package_ids) {}
void pk_backend_get_details_local(PkBackend *backend, PkBackendJob *job,
                                  gchar **files) {}
void pk_backend_get_files_local(PkBackend *backend, PkBackendJob *job,
                                gchar **files) {}

void pk_backend_get_files(PkBackend *backend, PkBackendJob *job,
                          gchar **package_ids) {}
void pk_backend_required_by(PkBackend *backend, PkBackendJob *job,
                            PkBitfield filters, gchar **package_ids,
                            gboolean recursive) {}
void pk_backend_get_update_detail(PkBackend *backend, PkBackendJob *job,
                                  gchar **package_ids) {}
void pk_backend_get_updates(PkBackend *backend, PkBackendJob *job,
                            PkBitfield filters) {}
void pk_backend_install_packages(PkBackend *backend, PkBackendJob *job,
                                 PkBitfield transaction_flags,
                                 gchar **package_ids) {}
void pk_backend_install_signature(PkBackend *backend, PkBackendJob *job,
                                  PkSigTypeEnum type, const gchar *key_id,
                                  const gchar *package_id) {}
void pk_backend_install_files(PkBackend *backend, PkBackendJob *job,
                              PkBitfield transaction_flags,
                              gchar **full_paths) {}
void pk_backend_refresh_cache(PkBackend *backend, PkBackendJob *job,
                              gboolean force) {}
void pk_backend_remove_packages(PkBackend *backend, PkBackendJob *job,
                                PkBitfield transaction_flags,
                                gchar **package_ids, gboolean allow_deps,
                                gboolean autoremove) {}
void pk_backend_resolve(PkBackend *backend, PkBackendJob *job,
                        PkBitfield filters, gchar **packages) {}

void pk_backend_search_details(PkBackend *backend, PkBackendJob *job,
                               PkBitfield filters, gchar **search) {
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  g_autoptr(ApkPackageArray) package_array = NULL;
  g_autoptr(ApkStringArray) string_array = NULL;
  gint result;

  g_return_if_fail(search != NULL);

  result = open_apk(APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

  // copy search strings into array
  apk_string_array_init(&string_array);
  while (*search != NULL) {
    apk_string_array_add(&string_array, *search);
    search++;
  }

  // from app_search.c
  apk_package_array_init(&package_array);
  ctx->query.match = BIT(APK_Q_FIELD_PACKAGE) | BIT(APK_Q_FIELD_NAME) |
                     BIT(APK_Q_FIELD_URL) | BIT(APK_Q_FIELD_REPLACES) |
                     BIT(APK_Q_FIELD_PROVIDES);
  ctx->query.mode.search = true;

  result = apk_query_packages(ctx, &ctx->query, string_array, &package_array);
  if (result >= 0) {
    apk_array_foreach_item(pkg, package_array) {
      if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED)) {
        if (pkg->ipkg == NULL) {
          continue;
        }
      }
      convert_apk_details(job, pkg);
    }
  } else {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR,
                              "query failed: %s", apk_error_str(result));
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_FINISHED);
out:
  pk_backend_job_finished(job);
}

void pk_backend_search_files(PkBackend *backend, PkBackendJob *job,
                             PkBitfield filters, gchar **search) {}
void pk_backend_search_groups(PkBackend *backend, PkBackendJob *job,
                              PkBitfield filters, gchar **search) {}
void pk_backend_search_names(PkBackend *backend, PkBackendJob *job,
                             PkBitfield filters, gchar **search) {}
void pk_backend_update_packages(PkBackend *backend, PkBackendJob *job,
                                PkBitfield transaction_flags,
                                gchar **package_ids) {}

void pk_backend_get_repo_list(PkBackend *backend, PkBackendJob *job,
                              PkBitfield filters) {
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  gint result;

  result =
      open_apk(APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE | APK_OPENF_CACHE_WRITE,
               &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
  pk_backend_job_set_backend(job, backend);

  apk_db_foreach_repository(_repo, db) {
    apk_blob_t blob_repo_id = {0};
    gchar *repo_id;
    gchar *repo_description;
    apk_digest_push(&blob_repo_id, &_repo->hash);
    repo_id = apk_blob_cstr(blob_repo_id);
    repo_description = apk_blob_cstr(_repo->url_base);

    g_assert_nonnull(repo_id);
    g_assert_nonnull(repo_description);

    pk_backend_job_repo_detail(job, repo_id, repo_description, true);

    g_free(repo_id);
    g_free(repo_description);
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_FINISHED);
out:
  pk_backend_job_finished(job);
}

void pk_backend_repo_enable(PkBackend *backend, PkBackendJob *job,
                            const gchar *repo_id, gboolean enabled) {
  pk_backend_job_finished(job);
}
void pk_backend_repo_set_data(PkBackend *backend, PkBackendJob *job,
                              const gchar *repo_id, const gchar *parameter,
                              const gchar *value) {
  pk_backend_job_finished(job);
}
void pk_backend_repo_remove(PkBackend *backend, PkBackendJob *job,
                            PkBitfield transaction_flags, const gchar *repo_id,
                            gboolean autoremove) {
  pk_backend_job_finished(job);
}

void pk_backend_what_provides(PkBackend *backend, PkBackendJob *job,
                              PkBitfield filters, gchar **search) {}

static int enumerate_available(apk_hash_item item, void *ctx) {
  PkBackendJob *job = ctx;
  struct apk_package *package = item;

  convert_apk_package(job, package);
  return 0;
}

void pk_backend_get_packages(PkBackend *backend, PkBackendJob *job,
                             PkBitfield filters) {

  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  g_autofree gchar *filters_str = pk_filter_bitfield_to_string(filters);
  gint result;

  result = open_apk(APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_REQUEST);

  if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NEWEST)) {
    apk_hash_foreach(&db->available.packages, enumerate_available, job);
  }

  if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED)) {
    struct apk_package_array *package_array =
        apk_db_sorted_installed_packages(db);
    apk_array_foreach_item(package, package_array) {
      convert_apk_package(job, package);
    }
  }

  // if (pk_bitfield_contain(filters, PK_FILTER_ENUM_BASENAME)) {
  //   struct apk_name_array *sorted_names = apk_db_sorted_names(db);

  //   apk_array_foreach_item(name, sorted_names) {
  //     struct apk_package *package = apk_pkg_get_installed(name);
  //     if (package != NULL) {
  //       convert_apk_package(job, package);
  //     } else {
  //       size_t pkg_name_strlen = strlen(name->name) + 1 + 1 + 1 + 1;
  //       g_autofree gchar *pkg_name = g_malloc0(pkg_name_strlen);
  //       g_snprintf(pkg_name, pkg_name_strlen, "%s;;;", name->name);

  //       pk_backend_job_package(job, PK_INFO_ENUM_UNKNOWN, pkg_name, NULL);
  //     }
  //   }
  // }

out:
  pk_backend_job_finished(job);
}

void pk_backend_upgrade_system(PkBackend *backend, PkBackendJob *job,
                               PkBitfield transaction_flags,
                               const gchar *distro_id,
                               PkUpgradeKindEnum upgrade_kind) {}

void pk_backend_repair_system(PkBackend *backend, PkBackendJob *job,
                              PkBitfield transaction_flags) {}

PkBitfield pk_backend_get_groups(PkBackend *backend) {
  PkBitfield groups;
  groups =
      pk_bitfield_from_enums(PK_GROUP_ENUM_SYSTEM, PK_GROUP_ENUM_UNKNOWN, -1);
  return groups;
}
PkBitfield pk_backend_get_filters(PkBackend *backend) {
  PkBitfield filters;
  filters =
      pk_bitfield_from_enums(PK_FILTER_ENUM_NONE, PK_FILTER_ENUM_INSTALLED, -1);
  return filters;
}

PkBitfield pk_backend_get_roles(PkBackend *backend) {
  PkBitfield roles;
  roles = pk_bitfield_from_enums(PK_ROLE_ENUM_GET_PACKAGES,
                                 PK_ROLE_ENUM_GET_REPO_LIST,
                                 PK_ROLE_ENUM_SEARCH_DETAILS, -1);
  return roles;
}
gchar **pk_backend_get_mime_types(PkBackend *backend) {
  const gchar *mime_types[] = {NULL};
  return g_strdupv((gchar **)mime_types);
}
gboolean pk_backend_supports_parallelization(PkBackend *backend) {
  return TRUE;
}

const gchar *pk_backend_get_author(PkBackend *backend) {
  return "Jane Rachinger <jane400@postmarketos.org>";
}

const gchar *pk_backend_get_description(PkBackend *backend) {
  return "apk-tools v3 via apk-polkit-rs";
}
