/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
 */

#include "pk-apk-query.h"

#include "pk-apk-auto.h"
#include "pk-apk-convert.h"
#include <apk/apk_database.h>

gint pk_apk_query(PkBackend *backend, PkBackendJob *job, PkBitfield filters,
                  struct apk_ctx *ctx, struct apk_database *db, gchar **search,
                  guint64 apk_query_flags, gboolean mode_search,
                  gboolean as_details) {
  _pk_apk_auto(apk_package_array) *package_array = NULL;
  _pk_apk_auto(apk_string_array) *string_array = NULL;
  _pk_apk_auto(apk_string_array) *failed_string_array = NULL;
  gint result = 0;

  pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

  // copy search strings into array
  apk_string_array_init(&string_array);
  apk_string_array_init(&failed_string_array);
  apk_package_array_init(&package_array);
  for (; *search != NULL; ++search) {
    if (pk_package_id_check(*search)) {
      gchar *array[] = {*search, NULL};

      result = pk_apk_find_package_id(backend, job, ctx, db, array,
                                      &package_array, &failed_string_array);
      if (result < 0) {
        pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR,
                                  "pk_apk_find_package_id failed: %s",
                                  apk_error_str(result));

        goto out;
      }
    } else {
      apk_string_array_add(&string_array, *search);
    }
  }

  if (apk_array_len(string_array) > 0) {
    // from app_search.c
    ctx->query.match = apk_query_flags;
    ctx->query.mode.search = mode_search;

    result = apk_query_packages(ctx, &ctx->query, string_array, &package_array);
    if (result >= 0) {
      result = 0;
    } else {
      pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR,
                                "query failed: %s", apk_error_str(result));
      goto out;
    }
  }

  apk_array_foreach_item(pkg, package_array) {
    if (as_details) {
      if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED)) {
        if (pkg->ipkg == NULL) {
          continue;
        }
      }
      convert_apk_to_job_details(job, pkg);
    } else {
      convert_apk_to_package(job, db, pkg, PK_INFO_ENUM_UNKNOWN);
    }
  }

out:
  return result;
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

gint pk_apk_find_package_id(PkBackend *backend, PkBackendJob *job,
                            struct apk_ctx *ctx, struct apk_database *db,
                            gchar **package_ids, struct apk_package_array **out,
                            struct apk_string_array **failed_out) {

  _pk_apk_auto(apk_string_array) *string_array = NULL;
  struct query_context query_context = {
      .array = out,
      .failed = failed_out,
  };

  apk_string_array_init(&string_array);

  ctx->query.match = BIT(APK_Q_FIELD_PACKAGE);

  for (; *package_ids != NULL; ++package_ids) {
    g_auto(GStrv) sections = NULL;
    g_autofree gchar *string = NULL;
    gsize string_size = 0;

    if (!pk_package_id_check(*package_ids)) {
      pk_backend_job_error_code(job, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "%s",
                                *package_ids);
      return -1;
    }
    sections = g_strsplit(*package_ids, ";", 3);

    string_size = strlen(sections[PK_PACKAGE_ID_NAME]) + 1 +
                  strlen(sections[PK_PACKAGE_ID_VERSION]) + 1;
    string = g_malloc(string_size);
    if (sections[PK_PACKAGE_ID_VERSION][0] == '\0') {
      g_snprintf(string, string_size, "%s", sections[PK_PACKAGE_ID_NAME]);
    } else {
      g_snprintf(string, string_size, "%s-%s", sections[PK_PACKAGE_ID_NAME],
                 sections[PK_PACKAGE_ID_VERSION]);
    }
    apk_string_array_add(&string_array, g_steal_pointer(&string));
  }

  apk_query_matches(ctx, &ctx->query, string_array, fetch_match_package,
                    &query_context);

  return 0;
}

void pk_apk_query_from_files(PkBackend *backend, PkBackendJob *job,
                             struct apk_ctx *ctx, struct apk_database *db,
                             gchar **files, gboolean as_details) {
  struct apk_query_match qm;
  char buf[PATH_MAX];

  for (; *files != NULL; ++files) {
    apk_query_who_owns(db, *files, &qm, buf, sizeof buf);
    if (qm.pkg == NULL) {
      continue;
    }
    if (as_details) {
      convert_apk_to_job_details(job, qm.pkg);
    } else {
      convert_apk_to_files(job, qm.pkg, TRUE);
    }
  }
}