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

#include "backend-apk-private.h"

#include "pk-backend.h"

#include "pk-backend-job.h"

#include <apk/apk_context.h>
#include <apk/apk_database.h>
#include <apk/apk_package.h>
#include <apk/apk_query.h>

int open_apk(OpenApkOptions options, ApkContext **ctx_writeback,
             ApkDatabase **db_writeback) {
  g_autoptr(ApkContext) ctx = g_malloc(sizeof(struct apk_ctx));
  g_autoptr(ApkDatabase) db = g_malloc(sizeof(struct apk_database));
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
  }

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

gchar *convert_apk_to_pkgid(struct apk_package *package) {
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

void convert_apk_to_job_details(PkBackendJob *job,
                                struct apk_package *package) {
  g_autofree gchar *pkg_id = convert_apk_to_pkgid(package);
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
        group_enum = PK_GROUP_ENUM_SYSTEM;
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
  pk_backend_job_details(job, pkg_id, pkg_desc, pkg_license, group_enum, NULL,
                         pkg_url, package->installed_size, package->size);
}

void convert_apk_to_package(PkBackendJob *job, struct apk_package *package) {
  g_autofree gchar *pkg_id = convert_apk_to_pkgid(package);
  g_autofree gchar *pkg_desc = apk_blob_cstr(*package->description);

  pk_backend_job_package(job,
                         package->ipkg != NULL ? PK_INFO_ENUM_INSTALLED
                                               : PK_INFO_ENUM_UNKNOWN,
                         pkg_id, pkg_desc);
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

  return result;
}