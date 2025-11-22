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
#include <glib/gi18n.h>
#include <packagekit-glib2/packagekit.h>
#include <packagekit-glib2/packagekit-private.h>

G_BEGIN_DECLS

/* Exit codes */
#define PKGCTL_EXIT_SUCCESS	       0
#define PKGCTL_EXIT_FAILURE	       1
#define PKGCTL_EXIT_SYNTAX_ERROR       2
#define PKGCTL_EXIT_PERMISSION_DENIED  4
#define PKGCTL_EXIT_TRANSACTION_FAILED 5

/**
 * OutputMode:
 * @PKGCTL_MODE_NORMAL: Normal output mode
 * @PKGCTL_MODE_QUIET: Minimal output
 * @PKGCTL_MODE_JSON: Output in JSON format
 * @PKGCTL_MODE_VERBOSE: Verbose output
 */
typedef enum {
	PKGCTL_MODE_NORMAL,
	PKGCTL_MODE_QUIET,
	PKGCTL_MODE_JSON,
	PKGCTL_MODE_VERBOSE
} PkgctlMode;

/**
 * PkgctlContext:
 *
 * Context structure for pkgctl
 */
typedef struct {
	PkControl     *control;
	PkTaskText    *task;
	GCancellable  *cancellable;
	GMainLoop     *loop;

	PkProgressBar *progressbar;
	GPtrArray     *commands;

	/* Automatic Flags */
	gboolean       simulate;
	gboolean       is_tty;

	/* Global Options */
	PkgctlMode     output_mode;
	gboolean       no_color;
	gboolean       noninteractive;
	gboolean       only_download;
	gboolean       allow_downgrade;
	gboolean       allow_reinstall;
	gboolean       allow_untrusted;
	gint	       cache_age;

	PkBitfield     filters;
	gboolean	   user_filters_set;

	/* State */
	gint	       exit_code;
	gboolean       transaction_running;

} PkgctlContext;

/**
 * PkgctlCommand:
 *
 * Structure defining a pkgctl command
 */
typedef struct PkgctlCommand PkgctlCommand;
struct PkgctlCommand {
	gchar *name;
	gchar *summary;
	gchar *param_summary;

	gint (*handler) (PkgctlContext *ctx,
					 PkgctlCommand *cmd,
					 gint argc,
					 gchar **argv);
};

PkgctlContext	    *pkgc_context_new (void);
void		     pkgc_context_free (PkgctlContext *ctx);
gboolean	     pkgc_context_init (PkgctlContext *ctx, GError **error);
void		     pkgc_context_apply_settings (PkgctlContext *ctx);

void		     pkgc_context_register_command (PkgctlContext *ctx,
						    const gchar	  *name,
						    gint (*handler) (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv),
						    const gchar *summary);
PkgctlCommand	*pkgc_context_find_command (PkgctlContext *ctx, const char *name);

void pkgc_context_on_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data);

G_END_DECLS
