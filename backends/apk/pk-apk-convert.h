/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
 */

#pragma once

// clang-format off
#include "pk-backend.h"
// clang-format on
#include "pk-backend-job.h"
#include <apk/apk_package.h>

struct apk_package;
struct apk_dependency;

gchar *convert_apk_to_pkgid(struct apk_package *package);
void convert_apk_to_job_details(PkBackendJob *job, struct apk_package *package);
void convert_apk_to_package(PkBackendJob *job, struct apk_database *db,
                            struct apk_package *package, PkInfoEnum info_enum);
void convert_apk_to_files(PkBackendJob *job, struct apk_package *package,
                          gboolean use_mark);
PkGroupEnum try_match_pkgname_to_group(struct apk_package *package);
