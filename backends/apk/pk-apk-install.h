/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
 */

#pragma once

#include "pk-backend.h"

gint pk_apk_apply_packages(PkBackend *backend, PkBackendJob *job,
                           PkBitfield transaction_flags, gchar **package_ids,
                           gboolean as_update);