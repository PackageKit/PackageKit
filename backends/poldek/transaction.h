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

#include "ctx.h"
#include "job.h"

G_BEGIN_DECLS

void	 pk_poldek_install_packages (PkPoldekCtx *pc,
				     PkPoldekJob *job,
				     json_t	 *req);
void	 pk_poldek_update_packages (PkPoldekCtx *pc,
				    PkPoldekJob *job,
				    json_t	*req);
void	 pk_poldek_remove_packages (PkPoldekCtx *pc,
				    PkPoldekJob *job,
				    json_t	*req);
void	 pk_poldek_download_packages (PkPoldekCtx *pc,
				      PkPoldekJob *job,
				      json_t	  *req);
void	 pk_poldek_install_files (PkPoldekCtx *pc,
				  PkPoldekJob *job,
				  json_t      *req);
void	 pk_poldek_upgrade_system (PkPoldekCtx *pc,
				   PkPoldekJob *job,
				   json_t      *req);
void	 pk_poldek_refresh_cache (PkPoldekCtx *pc,
				  PkPoldekJob *job,
				  json_t      *req);

int	 pk_poldek_ts_confirm_cb (void			 *user_data,
				  const struct poldek_ts *ts);

gboolean pk_poldek_upgrade_resolves (PkPoldekCtx *pc,
				     GPtrArray	 *pkgs);

G_END_DECLS
