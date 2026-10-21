/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <jansson.h>
#include <pk-enum.h>

#include <poldek/pkg.h>

#include "ctx.h"
#include "job.h"

G_BEGIN_DECLS

void	      pk_poldek_get_updates (PkPoldekCtx *pc,
				     PkPoldekJob *job,
				     json_t	 *req);
void	      pk_poldek_get_update_detail (PkPoldekCtx *pc,
					   PkPoldekJob *job,
					   json_t      *req);

PkRestartEnum pk_poldek_restart_for (const struct pkg *pkg);

G_END_DECLS
