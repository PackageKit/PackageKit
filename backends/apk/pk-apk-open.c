/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
 */

#include "pk-apk-open.h"
#include "matches.h"
#include "pk-backend.h"

#include <apk/apk_context.h>
#include <apk/apk_database.h>
#include <apk/apk_package.h>
#include <apk/apk_query.h>

int open_apk(PkBackend *backend, PkBackendJob *job, OpenApkOptions options,
             struct apk_ctx *ctx, struct apk_database *db) {
  int result = 0;

  apk_ctx_init(ctx);
  apk_db_init(db, ctx);

  ctx->open_flags = options.apk_flags;

  if (options.force_refresh_cache) {
    ctx->cache_max_age = 0;
  }

  if (options.cache_dir != NULL) {
    ctx->cache_dir = options.cache_dir;
    ctx->cache_dir_set = TRUE;
    ctx->cache_packages = TRUE;
  } else {
    ctx->cache_predownload = TRUE;
    ctx->cache_packages = TRUE;
  }

  if (!pk_backend_is_online(backend)) {
    ctx->flags |= APK_NO_NETWORK;
  }

  // TODO: usermode / running daemon as non-root
  // i don't know wheter this even makes sense, i never dealt with apk's
  // usermode. db->usermode = !!(ac->open_flags & APK_OPENF_USERMODE);

  result = apk_ctx_prepare(ctx);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION,
                              "apk_ctx_prepare failed with: %s",
                              apk_error_str(result));

    goto out;
  }

  result = apk_db_open(db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_FAILED_INITIALIZATION,
                              "apk_db_open failed with: %s",
                              apk_error_str(result));

    goto out;
  }

out:
  return result;
}

int check_world(PkBackendJob *job, struct apk_database *db) {
  gint result = 0;

  // this is effectivly copied from apk_db_check_world
  {
    GStrvBuilder *strv_builder = g_strv_builder_new();
    g_auto(GStrv) strv = NULL;

    apk_array_foreach(dep, db->world) {
      int tag = 0;
      gchar *str = NULL;
      size_t str_len;

      tag = dep->repository_tag;
      if (tag == 0 || db->repo_tags[tag].allowed_repos != 0)
        continue;

      str_len = strlen(dep->name->name) + 1 + db->repo_tags[tag].tag.len + 1;
      str = g_malloc(str_len);

      g_snprintf(str, str_len, "%s@" BLOB_FMT, dep->name->name,
                 BLOB_PRINTF(db->repo_tags[tag].tag));
      g_strv_builder_take(strv_builder, str);
    }

    strv = g_strv_builder_unref_to_strv(strv_builder);

    if (g_strv_length(strv) > 0) {
      gsize size = 0;
      g_autofree gchar *string = NULL;

      for (gchar **ptr = strv; *ptr != NULL; ++ptr) {
        size += strlen(*ptr) + 1;
      }

      string = g_malloc(size);
      {
        gchar *idx = string;
        for (gchar **ptr = strv; *ptr != NULL; ++ptr) {
          gsize len = strlen(*ptr);
          strncpy(idx, *ptr, len);
          idx[len] = ' ';
          idx += len;
        }
        idx[-1] = '\0';
      }

      pk_backend_job_error_code(
          job, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR,
          "unable to found repo tags for following packages: %s", string);
      return -1;
    }
  }

  result = apk_db_check_world(db, db->world);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR,
                              "unknown error during apk_db_check_world");
    return result;
  }

  // this is effectivly apk_db_repository_check
  {
    if (!db->repositories.stale && !db->repositories.unavailable) {
      pk_backend_job_error_code(
          job, PK_ERROR_ENUM_REPO_NOT_AVAILABLE,
          "not continuing due to stale/unavailable repositories");

      return -1;
    }
  }

  result = apk_db_repository_check(db);
  if (result != 0) {
    pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR,
                              "unknown error during apk_db_repository_check");
    return result;
  };

  return 0;
}
