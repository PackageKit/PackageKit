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

/* exit codes */
#define PKGC_EXIT_SUCCESS		0
#define PKGC_EXIT_FAILURE		1
#define PKGC_EXIT_SYNTAX_ERROR		2
#define PKGC_EXIT_PERMISSION_DENIED	3
#define PKGC_EXIT_NOT_FOUND		4
#define PKGC_EXIT_TRANSACTION_FAILED	5

/* error domain */
#define PKGC_ERROR	(pkgc_error_quark ())

/* default cache age (3 days) */
#define PKGC_DEFAULT_CACHE_AGE_SEC (3 * 24 * 60 * 60)

/**
 * OutputMode:
 * @PKGCLI_MODE_NORMAL:		Normal output mode
 * @PKGCLI_MODE_QUIET:		Minimal output
 * @PKGCLI_MODE_JSON:		Output in JSON format
 * @PKGCLI_MODE_VERBOSE:	Verbose output
 */
typedef enum {
	PKGCLI_MODE_NORMAL,
	PKGCLI_MODE_QUIET,
	PKGCLI_MODE_JSON,
	PKGCLI_MODE_VERBOSE
} PkgcliMode;

/**
 * PkgctlContext:
 *
 * Context structure for pkgcli
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
	PkgcliMode     output_mode;
	gboolean       no_color;
	gboolean       noninteractive;
	gboolean       only_download;
	gboolean       allow_downgrade;
	gboolean       allow_reinstall;
	gboolean       allow_untrusted;
	guint	       cache_age;

	PkBitfield     filters;
	gboolean       user_filters_set;

	/* State */
	gint	       exit_code;
	gboolean       transaction_running;

} PkgcliContext;

/**
 * PkgctlCommand:
 *
 * Structure defining a pkgcli command
 */
typedef struct PkgcliCommand PkgcliCommand;
struct PkgcliCommand {
	gchar *name;
	gchar *summary;
	gchar *param_summary;

	gint (*handler) (PkgcliContext *ctx,
					 PkgcliCommand *cmd,
					 gint argc,
					 gchar **argv);
};

GQuark			 pkgc_error_quark (void);
PkgcliContext	    *pkgc_context_new (void);
void		     pkgc_context_free (PkgcliContext *ctx);
gboolean	     pkgc_context_init (PkgcliContext *ctx, GError **error);
void		     pkgc_context_apply_settings (PkgcliContext *ctx);

void		     pkgc_context_register_command (PkgcliContext *ctx,
						    const gchar	  *name,
						    gint (*handler) (PkgcliContext *ctx,
						    				 PkgcliCommand *cmd,
						    				 gint argc,
						    				 gchar **argv),
						    const gchar *summary);
PkgcliCommand	*pkgc_context_find_command (PkgcliContext *ctx, const char *name);
void			pkgc_context_stop_progress_bar (PkgcliContext *ctx);

void			pkgc_context_on_progress_cb (PkProgress *progress,
											 PkProgressType type,
											 gpointer user_data);

G_END_DECLS
