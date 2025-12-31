/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2025 Neal Gompa <neal@gompa.dev>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <pk-backend.h>
#include <glib.h>

void dnf5_query_thread(PkBackendJob *job, GVariant *params, gpointer user_data);
void dnf5_transaction_thread(PkBackendJob *job, GVariant *params, gpointer user_data);
void dnf5_repo_thread(PkBackendJob *job, GVariant *params, gpointer user_data);
