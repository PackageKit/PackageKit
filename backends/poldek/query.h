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

#include <sys/types.h>

#include <jansson.h>
#include <pk-enum.h>

#include <poldek/pkg.h>

#include "ctx.h"
#include "job.h"

G_BEGIN_DECLS

void	    pk_poldek_search_name (PkPoldekCtx *pc,
				   PkPoldekJob *job,
				   json_t      *req);
void	    pk_poldek_search_details (PkPoldekCtx *pc,
				      PkPoldekJob *job,
				      json_t	  *req);
void	    pk_poldek_search_group (PkPoldekCtx *pc,
				    PkPoldekJob *job,
				    json_t	*req);
void	    pk_poldek_search_file (PkPoldekCtx *pc,
				   PkPoldekJob *job,
				   json_t      *req);
void	    pk_poldek_what_provides (PkPoldekCtx *pc,
				     PkPoldekJob *job,
				     json_t	 *req);
void	    pk_poldek_resolve (PkPoldekCtx *pc,
			       PkPoldekJob *job,
			       json_t	   *req);
void	    pk_poldek_get_packages (PkPoldekCtx *pc,
				    PkPoldekJob *job,
				    json_t	*req);
void	    pk_poldek_get_details (PkPoldekCtx *pc,
				   PkPoldekJob *job,
				   json_t      *req);
void	    pk_poldek_get_files (PkPoldekCtx *pc,
				 PkPoldekJob *job,
				 json_t	     *req);
void	    pk_poldek_get_dependencies (PkPoldekCtx *pc,
					PkPoldekJob *job,
					json_t	    *req);
void	    pk_poldek_get_dependents (PkPoldekCtx *pc,
				      PkPoldekJob *job,
				      json_t	  *req);

void	    pk_poldek_queue_package (PkPoldekJob      *job,
				     const struct pkg *pkg,
				     gboolean	       installed,
				     PkInfoEnum	       info);
void	    pk_poldek_queue_update (PkPoldekJob	     *job,
				    const struct pkg *pkg,
				    gboolean	      installed,
				    PkInfoEnum	      info,
				    PkSeverityEnum    severity);
json_t	   *pk_poldek_json_text (const gchar *text);
void	    pk_poldek_files_add (json_t	     *files,
				 const gchar *path,
				 mode_t	      mode);
void	    pk_poldek_json_set_text (json_t	 *obj,
				     const gchar *key,
				     const gchar *text);
gint	    pk_poldek_pkg_cmp (gconstpointer a,
			       gconstpointer b);
gchar	  **pk_poldek_require_strv (PkPoldekJob *job,
				    json_t	*req,
				    const gchar *key);

struct pkg *pk_poldek_lookup (PkPoldekCtx *pc,
			      const gchar *package_id,
			      gboolean	  *installed,
			      GError	 **error);

G_END_DECLS
