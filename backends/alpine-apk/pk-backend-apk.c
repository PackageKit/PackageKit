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

#include <apk/apk_blob.h>
#include <apk/apk_context.h>
#include <apk/apk_crypto.h>
#include <apk/apk_database.h>
#include <apk/apk_defines.h>
#include <apk/apk_hash.h>
#include <apk/apk_package.h>
#include <apk/apk_print.h>
#include <apk/apk_query.h>
#include <apk/apk_solver.h>
#include <apk/apk_version.h>
#include <glib.h>
#include <gmodule.h>
#include <string.h>

#include "pk-backend.h"

#include "pk-backend-job.h"
#include "pk-bitfield.h"
#include "pk-debug.h"

#include "backend-apk-private.h"

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

void pk_backend_upgrade_system(PkBackend *backend, PkBackendJob *job,
                               PkBitfield transaction_flags,
                               const gchar *distro_id,
                               PkUpgradeKindEnum upgrade_kind) {

  pk_backend_job_finished(job);
}

void pk_backend_install_signature(PkBackend *backend, PkBackendJob *job,
                                  PkSigTypeEnum type, const gchar *key_id,
                                  const gchar *package_id) {
  pk_backend_job_finished(job);
}

struct query_context {
  struct apk_package_array **array;
  struct apk_string_array **failed;

  uint64_t done_bytes, total_bytes;
  unsigned long done_packages;
};

// ported from app_fetch.c
static int fetch_match_package(void *pctx, struct apk_query_match *qm) {
  struct query_context *ctx = pctx;
  struct apk_package *pkg = qm->pkg;

  if (pkg == NULL) {
    gchar *string = g_malloc0(qm->query.len + 1);
    strncpy(string, qm->query.ptr, qm->query.len);
    apk_string_array_add(ctx->failed, string);
    return 0;
  }

  ctx->total_bytes += pkg->size;
  apk_package_array_add(ctx->array, pkg);
  return 0;
}

void pk_backend_download_packages(PkBackend *backend, PkBackendJob *job,
                                  gchar **package_ids, const gchar *directory) {
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  _pk_apk_auto(apk_string_array) *string_array = NULL;
  _pk_apk_auto(apk_package_array) *package_array = NULL;
  _pk_apk_auto(apk_string_array) *failed_package_array = NULL;

  struct query_context query_context = {
      .array = &package_array,
      .failed = &failed_package_array,
  };

  gint result;
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_READ | APK_OPENF_NO_STATE,
      .force_refresh_cache = FALSE,
      // also accepts null, kinda checky
      .cache_dir = directory,
  };

  assert(package_ids != NULL);

  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

  apk_string_array_init(&string_array);
  apk_package_array_init(&package_array);

  ctx->query.match = BIT(APK_Q_FIELD_PACKAGE);

  for (gchar **package_ids_idx = package_ids; *package_ids_idx != NULL;
       ++package_ids_idx) {
    g_auto(GStrv) sections = NULL;
    g_autofree gchar *string = NULL;
    gsize string_size = 0;

    if (!pk_package_id_check(*package_ids_idx)) {
      pk_backend_job_error_code(job, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "%s",
                                *package_ids_idx);
      goto out;
    }
    sections = g_strsplit(*package_ids_idx, ";", 3);

    string_size = strlen(sections[PK_PACKAGE_ID_NAME]) + 1 +
                  strlen(sections[PK_PACKAGE_ID_VERSION]) + 1;
    string = g_malloc(string_size);
    g_snprintf(string, string_size, "%s-%s", sections[PK_PACKAGE_ID_NAME],
               sections[PK_PACKAGE_ID_VERSION]);
    apk_string_array_add(&string_array, g_steal_pointer(&string));
  }

  apk_query_matches(ctx, &ctx->query, string_array, fetch_match_package,
                    &query_context);
  if (apk_array_len(failed_package_array) > 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR,
                              "apk_query_packages failed");
    goto out;
  }

  apk_array_foreach_item(package, package_array) {
    g_autofree gchar *package_id = convert_apk_to_pkgid(package);
    struct apk_repository *repo = apk_db_select_repo(db, package);

    if (pk_backend_job_is_cancelled(job)) {
      goto out;
    }

    pk_backend_job_set_item_progress(job, package_id, PK_STATUS_ENUM_DOWNLOAD,
                                     0);

    apk_cache_download(db, repo, package, NULL);

    pk_backend_job_set_item_progress(job, package_id, PK_STATUS_ENUM_FINISHED,
                                     100);
  }

out:
  pk_backend_job_finished(job);
}

void pk_backend_depends_on(PkBackend *backend, PkBackendJob *job,
                           PkBitfield filters, gchar **package_ids,
                           gboolean recursive) {
  pk_backend_job_finished(job);
}
void pk_backend_get_details(PkBackend *backend, PkBackendJob *job,
                            gchar **package_ids) {
  pk_backend_job_finished(job);
}
void pk_backend_get_details_local(PkBackend *backend, PkBackendJob *job,
                                  gchar **files) {
  pk_backend_job_finished(job);
}
void pk_backend_get_files_local(PkBackend *backend, PkBackendJob *job,
                                gchar **files) {
  pk_backend_job_finished(job);
}

void pk_backend_get_files(PkBackend *backend, PkBackendJob *job,
                          gchar **package_ids) {
  pk_backend_job_finished(job);
}
void pk_backend_required_by(PkBackend *backend, PkBackendJob *job,
                            PkBitfield filters, gchar **package_ids,
                            gboolean recursive) {
  pk_backend_job_finished(job);
}

void pk_backend_get_update_detail(PkBackend *backend, PkBackendJob *job,
                                  gchar **package_ids) {

  pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

  for (; *package_ids != NULL; ++package_ids) {
    const gchar *pkg_id = *package_ids;

    pk_backend_job_update_detail(job, pkg_id, NULL, NULL, NULL, NULL, NULL,
                                 PK_RESTART_ENUM_SYSTEM, NULL, NULL,
                                 PK_UPDATE_STATE_ENUM_UNKNOWN, NULL, NULL);
  }

  pk_backend_job_finished(job);
}

void pk_backend_get_updates(PkBackend *backend, PkBackendJob *job,
                            PkBitfield filters) {
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  _pk_apk_auto(apk_dependency_array) *world = NULL;
  struct apk_changeset changeset = {0};
  gint result = 0;
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_WRITE,
      .force_refresh_cache = FALSE,
      .cache_dir = NULL,
  };

  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  result = check_world(job, db);
  if (result != 0) {
    goto out;
  }

  apk_dependency_array_init(&world);
  apk_dependency_array_copy(&world, db->world);

  result = apk_solver_solve(
      db, APK_SOLVERF_AVAILABLE | APK_SOLVERF_LATEST | APK_SOLVERF_UPGRADE,
      world, &changeset);
  if (result != 0) {
    goto out;
  }

  apk_array_foreach_item(change, changeset.changes) {
    g_autofree gchar *pkg_id = convert_apk_to_pkgid(change.new_pkg);
    g_autofree gchar *pkg_desc = apk_blob_cstr(*change.new_pkg->description);

    pk_backend_job_package(job, PK_INFO_ENUM_NORMAL, pkg_id, pkg_desc);
  }

out:
  pk_backend_job_finished(job);
}
void pk_backend_install_packages(PkBackend *backend, PkBackendJob *job,
                                 PkBitfield transaction_flags,
                                 gchar **package_ids) {
  pk_backend_job_finished(job);
}

void pk_backend_install_files(PkBackend *backend, PkBackendJob *job,
                              PkBitfield transaction_flags,
                              gchar **full_paths) {
  pk_backend_job_finished(job);
}
void pk_backend_refresh_cache(PkBackend *backend, PkBackendJob *job,
                              gboolean force) {

  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;

  OpenApkOptions options = {.apk_flags = APK_OPENF_WRITE,
                            .force_refresh_cache = force,
                            .cache_dir = NULL};
  gint result;
  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_FINISHED);
out:
  pk_backend_job_finished(job);
}
void pk_backend_remove_packages(PkBackend *backend, PkBackendJob *job,
                                PkBitfield transaction_flags,
                                gchar **package_ids, gboolean allow_deps,
                                gboolean autoremove) {
  pk_backend_job_finished(job);
}
void pk_backend_resolve(PkBackend *backend, PkBackendJob *job,
                        PkBitfield filters, gchar **packages) {
  pk_backend_job_finished(job);
}

void pk_backend_search_details(PkBackend *backend, PkBackendJob *job,
                               PkBitfield filters, gchar **search) {
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  _pk_apk_auto(apk_package_array) *package_array = NULL;
  _pk_apk_auto(apk_string_array) *string_array = NULL;
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE,
      .cache_dir = NULL,
      .force_refresh_cache = FALSE,
  };
  gint result;

  g_return_if_fail(search != NULL);

  result = open_apk(options, &ctx, &db);
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
      convert_apk_to_job_details(job, pkg);
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
                             PkBitfield filters, gchar **search) {
  pk_backend_job_finished(job);
}
void pk_backend_search_groups(PkBackend *backend, PkBackendJob *job,
                              PkBitfield filters, gchar **search) {
  pk_backend_job_finished(job);
}
void pk_backend_search_names(PkBackend *backend, PkBackendJob *job,
                             PkBitfield filters, gchar **search) {
  pk_backend_job_finished(job);
}
void pk_backend_update_packages(PkBackend *backend, PkBackendJob *job,
                                PkBitfield transaction_flags,
                                gchar **package_ids) {
  gboolean simulate =
      pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE);
  gboolean only_download = pk_bitfield_contain(
      transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD);
  _pk_apk_auto(apk_dependency_array) *world = NULL;
  struct apk_changeset changeset = {0};
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  OpenApkOptions options = {
      .apk_flags =
          simulate ? APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE : APK_OPENF_WRITE,
      .cache_dir = NULL,
      .force_refresh_cache = FALSE,
  };
  gint result;

  goto out;

  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

  apk_solver_solve(db, APK_SOLVERF_AVAILABLE | APK_SOLVERF_LATEST, world,
                   &changeset);

out:
  pk_backend_job_finished(job);
}

void pk_backend_get_repo_list(PkBackend *backend, PkBackendJob *job,
                              PkBitfield filters) {
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  OpenApkOptions options = {
      .apk_flags =
          APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE | APK_OPENF_CACHE_WRITE,
      .cache_dir = NULL,
      .force_refresh_cache = FALSE,
  };
  gint result;

  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
  pk_backend_job_set_backend(job, backend);

  // TODO: change repo_id
  // _repo->hash is just _repo->url_index hashed, hence prefer just store the
  // human name use an actual repo_descritpion first and if NULL, use
  // _repo->url_base
  apk_db_foreach_repository(_repo, db) {
    apk_blob_t blob_repo_id = {0};
    g_autofree gchar *repo_id;
    g_autofree gchar *repo_description;
    apk_digest_push(&blob_repo_id, &_repo->hash);
    repo_id = apk_blob_cstr(blob_repo_id);
    repo_description = apk_blob_cstr(_repo->url_base);

    g_assert_nonnull(repo_id);
    g_assert_nonnull(repo_description);

    pk_backend_job_repo_detail(job, repo_id, repo_description, true);
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

  convert_apk_to_package(job, package);
  return 0;
}

void pk_backend_get_packages(PkBackend *backend, PkBackendJob *job,
                             PkBitfield filters) {

  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  g_autofree gchar *filters_str = pk_filter_bitfield_to_string(filters);
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE,
      .cache_dir = NULL,
      .force_refresh_cache = FALSE,

  };
  gint result;

  result = open_apk(options, &ctx, &db);
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
      convert_apk_to_package(job, package);
    }
  }

out:
  pk_backend_job_finished(job);
}

void pk_backend_repair_system(PkBackend *backend, PkBackendJob *job,
                              PkBitfield transaction_flags) {

  gboolean simulate =
      pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE);

  pk_backend_job_finished(job);
}

void pk_backend_get_categories(PkBackend *backend, PkBackendJob *job) {
  pk_backend_job_finished(job);
}

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
  roles = pk_bitfield_from_enums(
      PK_ROLE_ENUM_GET_PACKAGES, PK_ROLE_ENUM_GET_REPO_LIST,
      PK_ROLE_ENUM_DOWNLOAD_PACKAGES, PK_ROLE_ENUM_GET_UPDATES,
      PK_ROLE_ENUM_GET_UPDATE_DETAIL, PK_ROLE_ENUM_REFRESH_CACHE,
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
