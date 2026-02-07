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

#include "glibconfig.h"
#include "pk-apk-query.h"
#include "pk-backend.h"

#include "pk-backend-job.h"
#include "pk-bitfield.h"
#include "pk-debug.h"

#include "backend-apk-private.h"
#include "pk-apk-query.h"

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

void pk_backend_download_packages(PkBackend *backend, PkBackendJob *job,
                                  gchar **package_ids, const gchar *directory) {
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  _pk_apk_auto(apk_package_array) *package_array = NULL;
  _pk_apk_auto(apk_string_array) *failed_package_array = NULL;

  gint result;
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_READ | APK_OPENF_NO_STATE | APK_OPENF_NO_WORLD |
                   APK_OPENF_NO_INSTALLED | APK_OPENF_NO_AUTOUPDATE,
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

  apk_string_array_init(&failed_package_array);
  apk_package_array_init(&package_array);

  pk_apk_find_package_id(backend, job, ctx, db, package_ids, &package_array,
                         &failed_package_array);

  if (apk_array_len(failed_package_array) > 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR,
                              "pk_apk_find_package_id failed");
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
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  _pk_apk_auto(apk_package_array) *package_array = NULL;
  _pk_apk_auto(apk_string_array) *failed_package_array = NULL;

  gint result;
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE,
      .force_refresh_cache = FALSE,
  };

  assert(package_ids != NULL);

  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

  apk_string_array_init(&failed_package_array);
  apk_package_array_init(&package_array);

  result = pk_apk_find_package_id(backend, job, ctx, db, package_ids,
                                  &package_array, &failed_package_array);
  if (result != 0) {
    goto out;
  }
  if (apk_array_len(failed_package_array) > 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR,
                              "apk_query_packages failed");
    goto out;
  }

  {
    _pk_apk_auto(apk_dependency_array) *depends = NULL;
    _pk_apk_auto(apk_package_array) *recursive_packages = NULL;

    struct apk_changeset changeset = {0};

    apk_dependency_array_init(&depends);
    apk_package_array_init(&recursive_packages);

    apk_array_foreach_item(pkg, package_array) {
      struct apk_dependency dep;
      apk_dep_from_pkg(&dep, db, pkg);
      apk_dependency_array_add(&depends, dep);
    }

    result = apk_solver_solve(db, APK_SOLVERF_AVAILABLE, depends, &changeset);
    if (result != 0) {
      // TODO: error
      pk_backend_job_error_code(job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
                                "apk_solver_solve failed");
      goto out;
    }

    // only print direct depends of package_array in world.
    // we can't go the easy route in recursive and just print the entire world,
    // as we want to print a dependency that was already given to use via the
    // package_ids. hence swapping was deemed more correct.

  recursive_goback:
    apk_array_foreach_item(change, changeset.changes) {
      gboolean change_printed = FALSE;
      if (change.new_pkg == NULL) {
        continue;
      }

      apk_array_foreach_item(pkg, package_array) {
        apk_array_foreach_item(pkg_dep, pkg->depends) {
          int dep_result = apk_dep_analyze(change.new_pkg, &pkg_dep, pkg);
          if (dep_result != APK_DEP_SATISFIES) {
            continue;
          }

          // change was already printed
          if (pkg->marked) {
            change_printed = TRUE;
            break;
          }

          if (recursive) {
            apk_package_array_add(&package_array, change.new_pkg);
          }

          // marking packages in recursive mode, as we don't want to print
          // them multiple times
          pkg->marked = TRUE;
          convert_apk_to_package(job, change.new_pkg);

          change_printed = TRUE;
          break;
        }

        // ladder for change_printed
        if (change_printed)
          break;
      }
    }

    // swap arrays when needed and restart loop iteration
    if (recursive && apk_array_len(recursive_packages) > 0) {
      apk_package_array_free(&package_array);
      package_array = recursive_packages;
      recursive_packages = NULL;
      apk_package_array_init(&recursive_packages);
      goto recursive_goback;
    }
  }

out:
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

// from app_info.c: info_who_owns
void pk_backend_get_files_local(PkBackend *backend, PkBackendJob *job,
                                gchar **files) {

  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  _pk_apk_auto(apk_dependency_array) *world = NULL;
  struct apk_query_match qm;
  char buf[PATH_MAX];

  gint result = 0;
  OpenApkOptions options = {
      .apk_flags =
          APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE | APK_OPENF_NO_REPOS,
      .force_refresh_cache = FALSE,
      .cache_dir = NULL,
  };

  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  for (; *files != NULL; ++files) {
    apk_query_who_owns(db, *files, &qm, buf, sizeof buf);
    if (qm.pkg == NULL) {
      continue;
    }
    convert_apk_to_files(job, qm.pkg, TRUE);
  }

out:
  pk_backend_job_finished(job);
}

void pk_backend_get_files(PkBackend *backend, PkBackendJob *job,
                          gchar **package_ids) {

  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  _pk_apk_auto(apk_package_array) *package_array = NULL;
  _pk_apk_auto(apk_string_array) *failed_package_array = NULL;

  gint result;
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE,
      .force_refresh_cache = FALSE,
  };

  assert(package_ids != NULL);

  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

  apk_string_array_init(&failed_package_array);
  apk_package_array_init(&package_array);

  result = pk_apk_find_package_id(backend, job, ctx, db, package_ids,
                                  &package_array, &failed_package_array);
  if (result != 0) {
    goto out;
  }
  if (apk_array_len(failed_package_array) > 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR,
                              "apk_query_packages failed");
    goto out;
  }

  apk_array_foreach_item(package, package_array) {
    g_autofree gchar *package_id = NULL;
    g_autoptr(GStrvBuilder) strv_builder = NULL;
    g_auto(GStrv) strv = NULL;
    if (package->marked) {
      continue;
    }
    package->marked = TRUE;
    package_id = convert_apk_to_pkgid(package);
    strv_builder = g_strv_builder_new();

    apk_array_foreach_item(diri, package->ipkg->diris) {
      apk_array_foreach_item(file, diri->files) {
        // a bit overallocation doesn't hurt
        gsize size = 1 + diri->dir->namelen + 1 + file->namelen + 1;
        g_autofree gchar *string = g_malloc0(size);
        g_snprintf(string, size, "/" DIR_FILE_FMT,
                   DIR_FILE_PRINTF(diri->dir, file));
        g_strv_builder_take(strv_builder, g_steal_pointer(&string));
      }
    }

    strv = g_strv_builder_unref_to_strv(g_steal_pointer(&strv_builder));
    pk_backend_job_files(job, package_id, strv);
  }

out:
  pk_backend_job_finished(job);
}

struct _required_by_context {
  PkBackendJob *job;
  gboolean recursive;
};

static void _required_by_handler(struct apk_package *pkg0,
                                 struct apk_dependency *dep0,
                                 struct apk_package *pkg, void *pctx) {
  struct _required_by_context *context = pctx;
  if (pkg0->marked)
    return;

  pkg0->marked = TRUE;

  convert_apk_to_package(context->job, pkg0);

  if (context->recursive) {
    apk_pkg_foreach_reverse_dependency(
        pkg,
        APK_FOREACH_INSTALLED | APK_FOREACH_NO_CONFLICTS | APK_DEP_SATISFIES |
            apk_foreach_genid(),
        _required_by_handler, context);
  }
}

void pk_backend_required_by(PkBackend *backend, PkBackendJob *job,
                            PkBitfield filters, gchar **package_ids,
                            gboolean recursive) {
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  _pk_apk_auto(apk_package_array) *package_array = NULL;
  _pk_apk_auto(apk_string_array) *failed_package_array = NULL;

  gint result;
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE,
      .force_refresh_cache = FALSE,
  };

  assert(package_ids != NULL);

  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

  apk_string_array_init(&failed_package_array);
  apk_package_array_init(&package_array);

  result = pk_apk_find_package_id(backend, job, ctx, db, package_ids,
                                  &package_array, &failed_package_array);
  if (result != 0) {
    goto out;
  }
  if (apk_array_len(failed_package_array) > 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR,
                              "apk_query_packages failed");
    goto out;
  }

  {
    struct _required_by_context context = {
        .job = job,
        .recursive = recursive,
    };
    apk_array_foreach_item(pkg, package_array) {
      apk_pkg_foreach_reverse_dependency(
          pkg,
          APK_FOREACH_INSTALLED | APK_FOREACH_NO_CONFLICTS | APK_DEP_SATISFIES |
              apk_foreach_genid(),
          _required_by_handler, &context);
    }
  }

out:
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
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE,
      .cache_dir = NULL,
      .force_refresh_cache = FALSE,
  };
  gint result = 0;

  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  result = pk_apk_query(backend, job, filters, ctx, db, packages,
                        BIT(APK_Q_FIELD_NAME), FALSE, FALSE);
  if (result != 0) {
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_FINISHED);
out:
  pk_backend_job_finished(job);
}

void pk_backend_search_details(PkBackend *backend, PkBackendJob *job,
                               PkBitfield filters, gchar **search) {

  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE,
      .cache_dir = NULL,
      .force_refresh_cache = FALSE,
  };
  gint result = 0;

  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }
  result = pk_apk_query(backend, job, filters, ctx, db, search,
                        BIT(APK_Q_FIELD_PACKAGE) | BIT(APK_Q_FIELD_NAME) |
                            BIT(APK_Q_FIELD_URL) | BIT(APK_Q_FIELD_REPLACES) |
                            BIT(APK_Q_FIELD_PROVIDES),
                        TRUE, TRUE);
  if (result != 0) {
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_FINISHED);
out:
  pk_backend_job_finished(job);
}

void pk_backend_search_files(PkBackend *backend, PkBackendJob *job,
                             PkBitfield filters, gchar **search) {

  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE,
      .cache_dir = NULL,
      .force_refresh_cache = FALSE,
  };
  gint result = 0;

  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  for (gchar **item = search; *item != NULL; ++item) {
    if ((*item)[0] == '/') {
      // full path
      struct apk_query_match qm = {0};
      char buf[PATH_MAX];

      // TODO: handle int apk_query_who_owns()
      apk_query_who_owns(db, *item, &qm, buf, sizeof(buf));
      if (qm.pkg == NULL) {
        continue;
      }
      convert_apk_to_files(job, qm.pkg, TRUE);
    } else {
      // do individual queries
      gsize basename_strlen = 2 + strlen(*item) + 1;
      g_autofree gchar *basename_search = NULL;
      _pk_apk_auto(apk_package_array) *package_array = NULL;
      // holds one string, the basename_search. no copy occurs,
      // so be careful when moving this.
      _pk_apk_auto(apk_string_array) *string_array = NULL;
      apk_package_array_init(&package_array);
      apk_string_array_init(&string_array);
      if (basename_strlen <= 3) {
        // empty basename, ignore
        continue;
      }

      basename_search = g_malloc0(basename_strlen);
      g_snprintf(basename_search, basename_strlen, "/*%s", *item);
      apk_string_array_add(&string_array, basename_search);

      ctx->query.match = BIT(APK_Q_FIELD_CONTENTS) | BIT(APK_Q_FIELD_OWNER);
      ctx->query.mode.search = TRUE;

      result =
          apk_query_packages(ctx, &ctx->query, string_array, &package_array);
      if (result < 0) {
        // TODO: error
        goto out;
      }
      apk_array_foreach_item(package, package_array) {
        convert_apk_to_files(job, package, TRUE);
      }
    }
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_FINISHED);
out:
  pk_backend_job_finished(job);
}
void pk_backend_search_groups(PkBackend *backend, PkBackendJob *job,
                              PkBitfield filters, gchar **search) {
  pk_backend_job_finished(job);
}
void pk_backend_search_names(PkBackend *backend, PkBackendJob *job,
                             PkBitfield filters, gchar **search) {
  g_autoptr(ApkContext) ctx = NULL;
  g_autoptr(ApkDatabase) db = NULL;
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE,
      .cache_dir = NULL,
      .force_refresh_cache = FALSE,
  };
  gint result = 0;

  result = open_apk(options, &ctx, &db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s",
                              apk_error_str(result));
    goto out;
  }

  result = pk_apk_query(backend, job, filters, ctx, db, search,
                        BIT(APK_Q_FIELD_NAME) | BIT(APK_Q_FIELD_PROVIDES), TRUE,
                        FALSE);
  if (result != 0) {
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_FINISHED);
out:
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
      .apk_flags = simulate ? (APK_OPENF_READ | APK_OPENF_NO_AUTOUPDATE)
                            : APK_OPENF_WRITE,
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
                              PkBitfield filters, gchar **search) {
  pk_backend_job_finished(job);
}

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
      PK_ROLE_ENUM_RESOLVE, PK_ROLE_ENUM_SEARCH_DETAILS,
      PK_ROLE_ENUM_SEARCH_FILE, PK_ROLE_ENUM_SEARCH_NAME,
      PK_ROLE_ENUM_GET_FILES_LOCAL, -1);
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
