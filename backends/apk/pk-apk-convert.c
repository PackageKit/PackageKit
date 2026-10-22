/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
 */

#include "pk-apk-convert.h"

#include "matches.h"

#include <apk/apk_context.h>
#include <apk/apk_database.h>
#include <apk/apk_package.h>

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
  PkGroupEnum group_enum = try_match_pkgname_to_group(package);

  pk_backend_job_details(job, pkg_id, pkg_desc, pkg_license, group_enum, NULL,
                         pkg_url, package->installed_size, package->size);
}

void convert_apk_to_package(PkBackendJob *job, struct apk_database *db,
                            struct apk_package *package, PkInfoEnum info_enum) {
  g_autofree gchar *pkg_id = convert_apk_to_pkgid(package);
  g_autofree gchar *pkg_desc = apk_blob_cstr(*package->description);

  if (info_enum == PK_INFO_ENUM_UNKNOWN) {
    if (package->ipkg != NULL) {
      info_enum = PK_INFO_ENUM_INSTALLED;

    } else if (package->cached || package->filename_ndx ||
               apk_db_pkg_available(db, package)) {
      // from apk-tools/src/commit.c
      info_enum = PK_INFO_ENUM_AVAILABLE;
    }
  }

  pk_backend_job_package(job, info_enum, pkg_id, pkg_desc);
}

void convert_apk_to_files(PkBackendJob *job, struct apk_package *package,
                          gboolean use_mark) {
  g_autoptr(GStrvBuilder) strv_builder = NULL;
  g_auto(GStrv) strv = NULL;
  g_autofree gchar *package_id = NULL;

  if (use_mark) {
    if (package->marked) {
      return;
    }
    package->marked = TRUE;
  }

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
  strv = g_strv_builder_end(strv_builder);
  pk_backend_job_files(job, package_id, strv);
}

PkGroupEnum try_match_pkgname_to_group(struct apk_package *package) {
  // manual freeing atkavek out
  PkGroupEnum result = PK_GROUP_ENUM_UNKNOWN;
  // package->name->name, values from provider, NULL

  gchar **values =
      g_malloc0(sizeof(gchar *) * (1 + apk_array_len(package->provides) + 1));
  values[0] = package->name->name;

  {
    gsize counter = 1;
    apk_array_foreach(provides, package->provides) {
      values[counter] = provides->name->name;
      ++counter;
    }
    values[counter] = NULL;
  }

  for (gchar **idx = values; *idx != NULL && result == PK_GROUP_ENUM_UNKNOWN;
       ++idx) {
    for (const struct Match **try_match_ptr = MATCHES; *try_match_ptr != NULL;
         ++try_match_ptr) {
      const struct Match *try_match = *try_match_ptr;
      if (try_match->prefix != NULL) {
        for (gchar **try_prefix = try_match->prefix; *try_prefix != NULL;
             ++try_prefix) {
          if g_str_has_prefix (*idx, *try_prefix) {
            result = try_match->group_enum;
            goto out;
          }
        }
      }

      if (try_match->suffix != NULL) {
        for (gchar **try_suffix = try_match->suffix; *try_suffix != NULL;
             ++try_suffix) {
          if g_str_has_suffix (*idx, *try_suffix) {
            result = try_match->group_enum;
            goto out;
          }
        }
      }
    }
  }

out:
  g_free(values);

  return result;
}