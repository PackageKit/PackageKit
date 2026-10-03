/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
 */

#include "pk-apk-install.h"

#include "pk-apk-auto.h"
#include "pk-apk-open.h"
#include <apk/apk_database.h>
#include <apk/apk_solver.h>

struct InstallContext {
  PkBackend *backend;
  PkBackendJob *job;
  GStrv *package_ids_split;

  struct apk_ctx *ctx;
  struct apk_database *db;
  struct apk_dependency_array *new_world;
  struct apk_changeset changeset;

  gboolean as_update;
  gboolean just_reinstall;
  gboolean only_download;
  gboolean simulate;
  gboolean only_trusted;
  gboolean allow_reinstall;
  gboolean allow_downgrade;
};

// assume only package_ids was copied, so backend and job aren't ref'd and
// unref'd
static inline void __free_InstallContext(struct InstallContext *ctx) {
  for (GStrv *idx = ctx->package_ids_split; *idx != NULL; ++idx) {
    g_strfreev((GStrv)*idx);
  }
  g_free(ctx->package_ids_split);
}

static int db_foreach_name_cb(struct apk_database *db, const char *match,
                              struct apk_name *name, void *pctx) {

  struct InstallContext *ctx = pctx;
  GStrv found = NULL;
  gint result = 0;

  for (GStrv *idx = ctx->package_ids_split; *idx != NULL; ++idx) {
    GStrv package_id = (*idx);
    if (g_strcmp0(package_id[PK_PACKAGE_ID_NAME], match) == 0) {
      found = package_id;
      break;
    }
  }
  if (found == NULL) {
    pk_backend_job_error_code(
        ctx->job, PK_ERROR_ENUM_INTERNAL_ERROR,
        "apk's match not found in requested package-ids: %s", match);
    result = -EINVAL;
    goto out;
  }

  if (!ctx->as_update && ctx->just_reinstall) {
    apk_solver_set_name_flags(name, APK_SOLVERF_REINSTALL, 0);

  } else if (ctx->as_update && ctx->allow_downgrade) {
    apk_solver_set_name_flags(name, APK_SOLVERF_UPGRADE | APK_SOLVERF_AVAILABLE,
                              0);
  } else if (ctx->as_update) {
    apk_solver_set_name_flags(name, APK_SOLVERF_UPGRADE | APK_SOLVERF_LATEST,
                              0);
  } else {
    apk_solver_set_name_flags(name, APK_SOLVERF_AVAILABLE, 0);
  }

out:
  return result;
}

static void copy_old_world_to_context(struct InstallContext *uctx) {
  _pk_apk_auto(apk_dependency_array) *new_world = {};

  apk_dependency_array_init(&new_world);
  apk_dependency_array_copy(&new_world, uctx->db->world);

  if (uctx->allow_downgrade) {
    apk_array_foreach(dep, new_world) {
      if (dep->op == APK_DEPMASK_CHECKSUM) {
        dep->op = APK_DEPMASK_ANY;
        dep->version = &apk_atom_null;
      }
    }
  }
  apk_array_foreach_item(dep, new_world) {
    apk_solver_set_name_flags(dep.name, APK_SOLVERF_INSTALLED, 0);
  }
}

static void copy_package_ids_to_context(struct InstallContext *uctx,
                                        gchar **package_ids) {
  size_t len = 0;
  // copy package_ids already split into new uctx.package_ids
  for (gchar **idx = package_ids; *idx != NULL; ++idx) {
    ++len;
  }

  uctx->package_ids_split = g_malloc0((len + 1) * sizeof(GStrv *));
  for (size_t counter = 0; counter < len; ++counter) {
    uctx->package_ids_split[counter] =
        pk_package_id_split(package_ids[counter]);
  }
  uctx->package_ids_split[len] = NULL;
}

static gint apply_solver_flags_on_package_ids(struct InstallContext *uctx) {
  gint result = 0;
  _pk_apk_auto(apk_string_array) *filters = NULL;
  apk_string_array_init(&filters);

  result = apk_db_foreach_matching_name(uctx->db, filters, db_foreach_name_cb,
                                        &uctx);
  // TODO: error
  if (result != 0) {
    goto out;
  }

out:
  return result;
}

gint pk_apk_apply_packages(PkBackend *backend, PkBackendJob *job,
                           PkBitfield transaction_flags, gchar **package_ids,
                           gboolean as_update) {
  _pk_apk_auto(apk_ctx) ctx = {};
  struct apk_database db = {};
  gint result;

  _pk_apk_auto(InstallContext) uctx = {
      .backend = backend,
      .job = job,
      .package_ids_split = NULL,
      .db = &db,
      .ctx = &ctx,
      .changeset = {},
      .as_update = as_update,
      .just_reinstall =
          pk_bitfield_from_enums(PK_TRANSACTION_FLAG_ENUM_JUST_REINSTALL),
      .only_download =
          pk_bitfield_from_enums(PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD),
      .simulate = pk_bitfield_from_enums(PK_TRANSACTION_FLAG_ENUM_SIMULATE),
      .only_trusted =
          pk_bitfield_from_enums(PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED),
      .allow_reinstall =
          pk_bitfield_from_enums(PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL),
      .allow_downgrade =
          pk_bitfield_from_enums(PK_TRANSACTION_FLAG_ENUM_ALLOW_DOWNGRADE),
  };
  OpenApkOptions options = {
      .apk_flags = APK_OPENF_NO_AUTOUPDATE |
                   (uctx.simulate ? APK_OPENF_READ : APK_OPENF_WRITE),

      .cache_dir = NULL,
      .force_refresh_cache = FALSE,
  };

  result = open_apk(backend, job, options, &ctx, &db);
  if (result != 0) {
    goto out;
  }

  pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

  result = check_world(job, &db);
  if (result != 0) {
    goto out;
  }

  copy_old_world_to_context(&uctx);
  copy_package_ids_to_context(&uctx, package_ids);
  apply_solver_flags_on_package_ids(&uctx);

  result = apk_solver_solve(&db, 0, uctx.new_world, &uctx.changeset);
  {
    if (result != 0) {
      // TODO: Error while resolving world
      pk_backend_job_error_code(job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "");
      goto out;
    }

    for (GStrv *idx = uctx.package_ids_split; *idx != NULL; ++idx) {
      struct apk_package *found = NULL;
      apk_blob_t expected_version;
      int compare_result = 0;

      if (*idx[PK_PACKAGE_ID_VERSION][0] == '\0') {
        continue;
      }

      expected_version = APK_BLOB_STR((*idx)[PK_PACKAGE_ID_VERSION]);
      apk_array_foreach(change, uctx.changeset.changes) {
        struct apk_package *pkg = change->new_pkg;
        if (g_strcmp0((*idx)[PK_PACKAGE_ID_NAME], pkg->name->name) == 0) {
          found = pkg;
          break;
        }
      }

      if (found == NULL) {
        goto out;
      }

      compare_result = apk_version_compare(expected_version, *found->version);
      if (compare_result != APK_VERSION_EQUAL) {
        // TODO: Error
        goto out;
      }
    }

    if (!uctx.simulate) {
      result = apk_solver_precache_changeset(&db, &uctx.changeset, TRUE);
      if (result != 0) {
        pk_backend_job_error_code(job, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED,
                                  "");
        goto out;
      }

      if (uctx.only_download) {
        goto out;
      }

      result =
          apk_solver_commit_changeset(&db, &uctx.changeset, uctx.new_world);
      if (result != 0) {
        pk_backend_job_error_code(job, PK_ERROR_ENUM_PACKAGE_FAILED_TO_INSTALL,
                                  "");
        goto out;
      }
    }
  }

out:
  return result;
}
