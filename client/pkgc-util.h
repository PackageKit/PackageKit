/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2025 Matthias Klumpp <matthias@tenstral.net>
 * Copyright (C) 2007-2014 Richard Hughes <richard@hughsie.com>
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
#include <jansson.h>

#include "pkgc-context.h"

G_BEGIN_DECLS

/**
 * PkgcColor:
 *
 * Terminal color codes.
 */
typedef enum {
	PKGC_COLOR_RESET,
	PKGC_COLOR_BOLD,
	PKGC_COLOR_RED,
	PKGC_COLOR_GREEN,
	PKGC_COLOR_YELLOW,
	PKGC_COLOR_BLUE,
	PKGC_COLOR_MAGENTA,
	PKGC_COLOR_CYAN,
	PKGC_COLOR_GRAY
} PkgcColor;

gboolean     pkgc_util_setup_proxy (PkgctlContext *ctx, GError **error);
gboolean     pkgc_util_check_connection (PkgctlContext *ctx, GError **error);
gchar	    *pkgc_util_format_size (guint64 size);
gchar	    *pkgc_util_format_time (guint seconds);

void		 pkgc_print_json_decref (json_t *root);

const gchar *pkgc_get_ansi_color (PkgctlContext *ctx, PkgcColor color);

void	     pkgc_print_package (PkgctlContext *ctx, PkPackage *package);
void	     pkgc_print_package_detail (PkgctlContext *ctx, PkDetails *details);
void	     pkgc_print_update_detail (PkgctlContext *ctx, PkUpdateDetail *update);
void	     pkgc_print_repo (PkgctlContext *ctx, PkRepoDetail *repo);
void	     pkgc_print_transaction (PkgctlContext *ctx, PkTransactionPast *transaction);
void	     pkgc_print_error (PkgctlContext *ctx, const char *format, ...) G_GNUC_PRINTF (2, 3);
void	     pkgc_print_warning (PkgctlContext *ctx, const char *format, ...) G_GNUC_PRINTF (2, 3);
void	     pkgc_print_info (PkgctlContext *ctx, const char *format, ...) G_GNUC_PRINTF (2, 3);
void	     pkgc_print_success (PkgctlContext *ctx, const char *format, ...) G_GNUC_PRINTF (2, 3);
void	     pkgc_println (const char *format, ...) G_GNUC_PRINTF (1, 2);

GOptionContext	*pkgc_option_context_for_command (PkgctlContext *ctx,
												  PkgctlCommand *cmd,
												  const gchar *parameter_summary,
												  const gchar *description);
gboolean		pkgc_parse_command_options (PkgctlContext  *ctx,
											PkgctlCommand  *cmd,
											GOptionContext *option_context,
											gint		   *argc,
											gchar          ***argv,
											gint		   min_arg_count);

gchar	    *pkgc_resolve_package (PkgctlContext *ctx,
				   PkBitfield	  filters,
				   const gchar	 *package_name,
				   GError	**error);
gchar	   **pkgc_resolve_packages (PkgctlContext *ctx,
				    PkBitfield	   filters,
				    gchar	 **packages,
				    GError	 **error);

G_END_DECLS
