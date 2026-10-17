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

#include <glib.h>

#include <trurl/narray.h>

G_BEGIN_DECLS

struct poldek_ctx;
struct poclidek_ctx;
struct poldek_ts;

/* what a job needs of poldek */
typedef enum {
	/* nothing: it reads package files with librpm, or sets up a context of its own */
	PK_POLDEK_NEED_NOTHING,
	/* the configuration and its sources */
	PK_POLDEK_NEED_SOURCES,
	/* the repositories' and the installed packages too */
	PK_POLDEK_NEED_PACKAGES,
} PkPoldekNeed;

typedef struct
{
	gchar		    *config;
	gchar		    *root;
	struct poldek_ctx   *ctx;
	struct poclidek_ctx *cctx;
	gboolean	     stale;
	gchar		    *dbdir;
	gchar		    *rpmdb_stamp;
} PkPoldekCtx;

PkPoldekCtx *pk_poldek_ctx_new (const gchar *config,
				const gchar *root);
PkPoldekCtx *pk_poldek_ctx_new_with_source (PkPoldekCtx *pc,
					    const gchar *name,
					    const gchar *path,
					    const gchar *config,
					    GError     **error);
void	     pk_poldek_ctx_free (PkPoldekCtx *pc);
void	     pk_poldek_ctx_release (PkPoldekCtx *pc);
gboolean     pk_poldek_ctx_prepare (PkPoldekCtx *pc,
				    PkPoldekNeed need,
				    GError     **error);
void	     pk_poldek_ctx_invalidate (PkPoldekCtx *pc);
void	     pk_poldek_ctx_transaction_done (PkPoldekCtx      *pc,
					     struct poldek_ts *ts);
void	     pk_poldek_ctx_interrupt (void);
void	     pk_poldek_ctx_reset_interrupt (void);
gboolean     pk_poldek_ctx_interrupted (void);
tn_array    *pk_poldek_ctx_get_installed (PkPoldekCtx *pc);
tn_array    *pk_poldek_ctx_get_available (PkPoldekCtx *pc);

G_END_DECLS
