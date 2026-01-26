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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <sys/ioctl.h>

#include "pkgc-util.h"

/**
 * pkgc_error_quark:
 *
 * An error quark for pkgcli.
 *
 * Return value: an error quark.
 **/
G_DEFINE_QUARK (pkgc-error-quark, pkgc_error)

/**
 * pkgc_command_free:
 */
static void
pkgc_command_free (gpointer data)
{
	PkgcliCommand *cmd = data;
	if (cmd == NULL)
		return;

	g_free (cmd->name);
	g_free (cmd->summary);
	g_free (cmd->param_summary);
	g_free (cmd);
}

/**
 * pkc_context_new:
 *
 * Create a new PkgctlContext with default values.
 *
 * Returns: (transfer full): a new #PkgctlContext
 */
PkgcliContext *
pkgc_context_new (void)
{
	PkgcliContext *ctx = g_new0 (PkgcliContext, 1);

	ctx->cache_age = -1; /* use default cache age */
	ctx->output_mode = PKGCLI_MODE_NORMAL;
	ctx->exit_code = PKGC_EXIT_SUCCESS;
	ctx->is_tty = isatty (fileno (stdout));

	ctx->loop = g_main_loop_new (NULL, FALSE);
	ctx->cancellable = g_cancellable_new ();

	ctx->commands = g_ptr_array_new_with_free_func (pkgc_command_free);

	/* assume arch filter and newest packages by default */
	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_ARCH);
	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NEWEST);

	return ctx;
}

/**
 * pkc_context_free:
 * @ctx: a valid #PkgctlContext
 *
 * Free a #PkgctlContext and all its associated resources.
 */
void
pkgc_context_free (PkgcliContext *ctx)
{
	if (ctx == NULL)
		return;

	if (ctx->control)
		g_object_unref (ctx->control);
	if (ctx->task)
		g_object_unref (ctx->task);
	if (ctx->progressbar)
		g_object_unref (ctx->progressbar);
	if (ctx->cancellable)
		g_object_unref (ctx->cancellable);
	if (ctx->loop)
		g_main_loop_unref (ctx->loop);

	g_free (ctx);
}

/**
 * pkgc_context_notify_connected_cb:
 * @control: a valid #PkControl
 * @pspec: the property spec
 * @data: (closure): a #PkgctlContext
 *
 * Callback for changes to the "connected" property of #PkControl.
 */
static void
pkgc_context_notify_connected_cb (PkControl *control, GParamSpec *pspec, gpointer data)
{
	PkgcliContext *ctx = (PkgcliContext *) data;
	gboolean connected;

	/* if the daemon crashed, don't hang around */
	g_object_get (control,
			  "connected", &connected,
			  NULL);
	if (!connected) {
		/* TRANSLATORS: This is when the daemon crashed, and we are up
		 * shit creek without a paddle */
		g_print ("%s\n", _("The daemon crashed mid-transaction!"));
		g_main_loop_quit (ctx->loop);
	}
}

/**
 * pkc_context_init:
 * @ctx: a valid #PkgctlContext
 * @error: (out): return location for a #GError, or %NULL
 *
 * Initialize the #PkgctlContext with the currently set parameters
 * and connect to PackageKit.
 *
 * Returns: %TRUE on success
 */
gboolean
pkgc_context_init (PkgcliContext *ctx, GError **error)
{
	struct winsize w;
	int bar_size = 40;

	if (ctx->control != NULL) {
		g_critical ("Tried to initialize an already initialized PkgctlContext");
		return FALSE;
	}

	/* create progress bar for TTY */
	g_clear_object (&ctx->progressbar);
	if (ctx->is_tty && ctx->output_mode != PKGCLI_MODE_JSON &&
		ctx->output_mode != PKGCLI_MODE_QUIET) {
		/* adjust progress bar size for small terminals */
		if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
			gint col_diff = (gint) w.ws_col - 52;
			bar_size = MAX (8, MIN (col_diff, bar_size));
		}

		ctx->progressbar = pk_progress_bar_new ();
		pk_progress_bar_set_size (ctx->progressbar, (guint) bar_size);

		/* unless we are verbose, we just use one progress bar and update it many times */
		pk_progress_bar_set_allow_restart (ctx->progressbar,
			ctx->output_mode != PKGCLI_MODE_VERBOSE);
	}

	/* create control object */
	ctx->control = pk_control_new ();
	if (!pk_control_get_properties (ctx->control, ctx->cancellable, error)) {
		g_clear_object (&ctx->control);
		return FALSE;
	}

	/* watch when the daemon aborts */
	g_signal_connect (ctx->control, "notify::connected",
			  G_CALLBACK (pkgc_context_notify_connected_cb), ctx);

	/* create task object */
	ctx->task = pk_task_text_new ();
	pkgc_context_apply_settings (ctx);

	/* setup proxy if needed */
	if (!pkgc_util_setup_proxy (ctx, error)) {
		g_clear_object (&ctx->control);
		g_clear_object (&ctx->task);
		return FALSE;
	}

	return TRUE;
}

/**
 * pkgc_context_apply_settings:
 * @ctx: a valid #PkgctlContext
 *
 * Apply the global settings from the #PkgctlContext to the underlying
 * #PkTaskText.
 */
void
pkgc_context_apply_settings (PkgcliContext *ctx)
{
	gboolean do_simulate;

	/* Always simulate if interactive and not download-only */
	do_simulate = !ctx->noninteractive && !ctx->only_download;

	g_object_set (ctx->task,
		      "simulate",
		      do_simulate,
		      "interactive",
		      !ctx->noninteractive,
		      "only-download",
		      ctx->only_download,
		      "allow-downgrade",
		      ctx->allow_downgrade,
		      "allow-reinstall",
		      ctx->allow_reinstall,
		      "cache-age",
		      ctx->cache_age,
		      "only-trusted",
		      !ctx->allow_untrusted,
		      NULL);
}

/**
 * pkgc_context_register_command:
 * @ctx: a valid #PkgctlContext
 * @name: the command name
 * @handler: function pointer to the command handler
 * @summary: short description of the command
 *
 * Register a command in the given #PkgctlContext.
 */
void
pkgc_context_register_command (PkgcliContext *ctx,
							   const gchar *name,
							   gint (*handler) (PkgcliContext *ctx, PkgcliCommand *cmd, gint argc, gchar **argv),
							   const gchar *summary)
{
	PkgcliCommand *cmd;

	cmd = g_new0 (PkgcliCommand, 1);
	cmd->name = g_strdup (name);
	cmd->handler = handler;
	cmd->summary = g_strdup (summary);

	g_ptr_array_add (ctx->commands, cmd);
}

/**
 * pkgc_context_find_command:
 * @ctx: a valid #PkgctlContext
 * @name: the command name to find
 *
 * Find a registered command by name in the given #PkgctlContext.
 *
 * Returns: (transfer none): a pointer to the #PkgctlCommand, or %NULL if not found
 */
PkgcliCommand *
pkgc_context_find_command (PkgcliContext *ctx, const char *name)
{
	if (!name)
		return NULL;

	for (guint i = 0; i < ctx->commands->len; i++) {
		PkgcliCommand *cmd = g_ptr_array_index (ctx->commands, i);
		if (g_strcmp0 (cmd->name, name) == 0)
			return cmd;
	}

	return NULL;
}

/**
 * pkgc_context_stop_progress_bar:
 * @ctx: a valid #PkgctlContext
 *
 * Stop the progress bar in the given #PkgctlContext.
 */
void pkgc_context_stop_progress_bar (PkgcliContext* ctx)
{
	if (ctx->progressbar != NULL)
		pk_progress_bar_end (ctx->progressbar);
}

/**
 * pkgc_context_on_progress_cb:
 * @progress: a valid #PkProgress
 * @type: the type of progress update
 * @user_data: (closure): a #PkgctlContext
 *
 * Callback for progress updates.
 */
void
pkgc_context_on_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
{
	PkgcliContext *ctx = user_data;
	gint percentage;
	PkStatusEnum status;
	PkRoleEnum role;
	guint64 transaction_flags;

	if (ctx->progressbar == NULL || ctx->output_mode == PKGCLI_MODE_JSON ||
	    ctx->output_mode == PKGCLI_MODE_QUIET)
		return;

	/* role */
	if (type == PK_PROGRESS_TYPE_ROLE) {
		g_object_get (progress,
			      "role", &role,
			      "transaction-flags", &transaction_flags,
			      NULL);
		if (role == PK_ROLE_ENUM_UNKNOWN)
			return;

		/* don't show the role when simulating */
		if (pk_bitfield_contain (transaction_flags,
					 PK_TRANSACTION_FLAG_ENUM_SIMULATE))
			return;

		/* show new status on the bar */
		pk_progress_bar_start (ctx->progressbar, pk_role_enum_to_localised_present (role));
	}

	/* status */
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
			      "role", &role,
			      "status", &status,
			      "transaction-flags", &transaction_flags,
			      NULL);

		/* don't show finished multiple times in the output */
		if (role == PK_ROLE_ENUM_RESOLVE &&
		    status == PK_STATUS_ENUM_FINISHED)
			return;

		/* defer most status actions for 50ms */
		if (status != PK_STATUS_ENUM_FINISHED) {
			if (!pk_bitfield_contain (transaction_flags,
						 PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
				pk_progress_bar_start (ctx->progressbar, pk_status_enum_to_localised_text (status));
			}
		}
	}

	/* percentage */
	if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		g_object_get (progress,
				  "percentage", &percentage,
				  NULL);
		pk_progress_bar_set_percentage (ctx->progressbar, percentage);
	}
}
