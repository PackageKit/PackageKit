/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2012 Marcin Banasiak <megabajt@pld-linux.org>
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

/*
 * The poldek and poclidek contexts. They are set up on the first job, not at
 * startup, as the daemon waits for the helper's hello, and kept for the
 * helper's lifetime. Installed packages are reloaded when the rpm database
 * changed under us, anything else that changes the configuration or the
 * repositories marks the contexts stale so the next job rebuilds them.
 */

#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#include <pk-enum.h>

#include <poldek/poldek.h>
#include <poldek/poldek_ts.h>
#include <poldek/pm/pm.h>
#include <poldek/poclidek/cli.h>
#include <poldek/poclidek/cmd.h>
#include <poldek/poclidek/dent.h>
#include <poldek/poclidek/poclidek.h>
#include <poldek/sigint/sigint.h>

#include "ctx.h"
#include "job.h"
#include "messages.h"
#include "transaction.h"

/*
 * poldek's interrupt flag, which is process wide. A cancel comes from the
 * reader thread; the lock keeps it from racing the main thread's resets.
 * poldek takes an interrupted load of the rpm database for one of no
 * packages, and saves that to a cache the command line reads too, so such a
 * load is not interrupted: a cancel during it takes effect right after.
 */
static GMutex pk_poldek_ctx_sigint_lock;
/* set once sigint_init() ran: from then on sigint_emit() is safe from any thread */
static gboolean pk_poldek_ctx_sigint_ready = FALSE;
static gboolean pk_poldek_ctx_cancel_requested = FALSE;
static gboolean pk_poldek_ctx_loading_installed = FALSE;

static void
pk_poldek_ctx_begin_installed_load (void)
{
	g_mutex_lock (&pk_poldek_ctx_sigint_lock);
	pk_poldek_ctx_loading_installed = TRUE;
	if (pk_poldek_ctx_sigint_ready)
		sigint_reset ();
	g_mutex_unlock (&pk_poldek_ctx_sigint_lock);
}

static void
pk_poldek_ctx_end_installed_load (void)
{
	g_mutex_lock (&pk_poldek_ctx_sigint_lock);
	pk_poldek_ctx_loading_installed = FALSE;
	if (pk_poldek_ctx_sigint_ready && pk_poldek_ctx_cancel_requested)
		sigint_emit ();
	g_mutex_unlock (&pk_poldek_ctx_sigint_lock);
}

static gboolean
pk_poldek_ctx_load_installed (PkPoldekCtx *pc, unsigned flags)
{
	gboolean ok;

	pk_poldek_ctx_begin_installed_load ();
	ok = poclidek_load_packages (pc->cctx, POCLIDEK_LOAD_INSTALLED | flags);
	pk_poldek_ctx_end_installed_load ();
	return ok;
}

PkPoldekCtx *
pk_poldek_ctx_new (const gchar *config, const gchar *root)
{
	PkPoldekCtx *pc = g_new0 (PkPoldekCtx, 1);

	pc->config = g_strdup (config);
	pc->root = g_strdup (root != NULL ? root : "/");
	return pc;
}

/*
 * A fingerprint of the rpm database files: anything else writing to the
 * database changes it. Lock files and sqlite's shared memory file are left
 * out, as reading the database touches them.
 */
static gchar *
pk_poldek_ctx_rpmdb_stamp (const gchar *dbdir)
{
	g_autoptr(GDir) dir = g_dir_open (dbdir, 0, NULL);
	g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func (g_free);
	GString *stamp = g_string_new (NULL);
	const gchar *name;

	while (dir != NULL && (name = g_dir_read_name (dir)) != NULL) {
		if (name[0] != '.' && !g_str_has_suffix (name, "-shm"))
			g_ptr_array_add (names, g_strdup (name));
	}
	g_ptr_array_sort_values (names, (GCompareFunc) g_strcmp0);
	for (guint i = 0; i < names->len; i++) {
		g_autofree gchar *path = g_build_filename (dbdir,
							   g_ptr_array_index (names, i),
							   NULL);
		struct stat st;

		if (stat (path, &st) < 0 || !S_ISREG (st.st_mode))
			continue;
		g_string_append_printf (stamp,
					"%s:%lu:%lld:%lld.%09ld;",
					(const gchar *) g_ptr_array_index (names, i),
					(gulong) st.st_ino,
					(long long) st.st_size,
					(long long) st.st_mtim.tv_sec,
					st.st_mtim.tv_nsec);
	}
	return g_string_free (stamp, FALSE);
}

static void
pk_poldek_ctx_teardown (PkPoldekCtx *pc)
{
	if (pc->cctx != NULL && pc->dbdir != NULL) {
		g_autofree gchar *stamp = pk_poldek_ctx_rpmdb_stamp (pc->dbdir);

		/* poclidek_free() saves the installed packages to a cache the CLI also reads */
		if (g_strcmp0 (stamp, pc->rpmdb_stamp) != 0)
			pk_poldek_ctx_load_installed (pc, POCLIDEK_LOAD_RELOAD);
	}
	if (pc->cctx != NULL) {
		poclidek_free (pc->cctx);
		pc->cctx = NULL;
	}
	if (pc->ctx != NULL) {
		/* also undoes sigint_init () */
		poldek_free (pc->ctx);
		pc->ctx = NULL;
	}
	g_clear_pointer (&pc->dbdir, g_free);
	g_clear_pointer (&pc->rpmdb_stamp, g_free);
}

/*
 * A context like @pc's with one more source @name, the directory of packages
 * @path. Its configuration @config includes @pc's and adds the source with
 * the lowest priority value, so its packages win over the same ones in the
 * repositories.
 */
PkPoldekCtx *
pk_poldek_ctx_new_with_source (PkPoldekCtx *pc,
			       const gchar *name,
			       const gchar *path,
			       const gchar *config,
			       GError **error)
{
	/* where poldek looks for its configuration when it is given none */
	const gchar *base = pc->config != NULL ? pc->config : "/etc/poldek/poldek.conf";
	g_autofree gchar *text = g_strdup_printf ("%%include %s\n\n"
						  "[source]\n"
						  "name = %s\n"
						  "type = dir\n"
						  "pri = -1000\n"
						  "path = %s/\n",
						  base,
						  name,
						  path);

	if (!g_file_set_contents (config, text, -1, error))
		return NULL;
	return pk_poldek_ctx_new (config, pc->root);
}

void
pk_poldek_ctx_free (PkPoldekCtx *pc)
{
	pk_poldek_ctx_teardown (pc);
	g_free (pc->config);
	g_free (pc->root);
	g_free (pc);
}

/* Frees what poldek holds now; the next job sets it up again. Only one context may be set up at a time. */
void
pk_poldek_ctx_release (PkPoldekCtx *pc)
{
	pk_poldek_ctx_teardown (pc);
	pc->stale = FALSE;
}

/* poldek with its configuration and sources, no packages loaded yet */
static gboolean
pk_poldek_ctx_setup (PkPoldekCtx *pc, GError **error)
{
	static gboolean lib_ready = FALSE;

	if (!lib_ready) {
		poldeklib_init ();
		/* rpm runs with -vh then, which the progress comes from */
		poldek_set_verbose (1);
		lib_ready = TRUE;
	}
	pc->ctx = poldek_new (0);
	g_mutex_lock (&pk_poldek_ctx_sigint_lock);
	sigint_init ();
	pk_poldek_ctx_sigint_ready = TRUE;
	/* sigint_init() clears the flag of a cancel that came before */
	if (pk_poldek_ctx_cancel_requested)
		sigint_emit ();
	g_mutex_unlock (&pk_poldek_ctx_sigint_lock);
	pk_poldek_messages_setup (pc->ctx);
	poldek_configure (pc->ctx, POLDEK_CONF_TSCONFIRM_CB, pk_poldek_ts_confirm_cb, NULL);
	poldek_configure (pc->ctx, POLDEK_CONF_ROOTDIR, pc->root);
	poldek_configure (pc->ctx, POLDEK_CONF_OPT, POLDEK_OP_USESUDO, 0);
	if (!poldek_load_config (pc->ctx, pc->config, NULL, 0)) {
		g_set_error (error,
			     PK_POLDEK_ERROR,
			     PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
			     "cannot load the poldek configuration %s",
			     pc->config != NULL ? pc->config : "(default)");
		return FALSE;
	}
	/* one candidate per NEVRA, not per name: ids name exact versions */
	poldek_configure (pc->ctx, POLDEK_CONF_OPT, POLDEK_OP_UNIQN, 0);
	if (!poldek_setup (pc->ctx)) {
		g_set_error_literal (error,
				     PK_POLDEK_ERROR,
				     PK_ERROR_ENUM_FAILED_INITIALIZATION,
				     "cannot set up poldek");
		return FALSE;
	}
	return TRUE;
}

/* The repositories' and the installed packages. */
static gboolean
pk_poldek_ctx_load (PkPoldekCtx *pc, GError **error)
{
	char dbpath[PATH_MAX];

	pc->cctx = poclidek_new (pc->ctx);
	if (pm_dbpath (poldek_get_pmctx (pc->ctx), dbpath, sizeof (dbpath)) == NULL) {
		g_set_error_literal (error,
				     PK_POLDEK_ERROR,
				     PK_ERROR_ENUM_FAILED_INITIALIZATION,
				     "cannot get the rpm database path");
		return FALSE;
	}
	pc->dbdir = g_build_filename (pc->root, dbpath, NULL);
	pc->rpmdb_stamp = pk_poldek_ctx_rpmdb_stamp (pc->dbdir);
	/* an interrupted load of the repositories can look like a successful one of fewer packages */
	if (!poclidek_load_packages (pc->cctx, POCLIDEK_LOAD_AVAILABLE) ||
	    pk_poldek_ctx_interrupted ()) {
		gboolean cancelled = pk_poldek_ctx_interrupted ();

		g_set_error_literal (error,
				     PK_POLDEK_ERROR,
				     cancelled ? PK_ERROR_ENUM_TRANSACTION_CANCELLED
					       : PK_ERROR_ENUM_REPO_NOT_AVAILABLE,
				     cancelled ? "Cancelled" : "cannot load packages");
		return FALSE;
	}
	/* poldek 0.45.0 and older read the descriptions and file lists of installed
	 * packages from their cache in / even under another root: load them from the
	 * rpm database then. Only the tests use another root. */
	if (!pk_poldek_ctx_load_installed (pc,
					   g_strcmp0 (pc->root, "/") != 0 ? POCLIDEK_LOAD_RELOAD
									  : 0)) {
		g_set_error_literal (error,
				     PK_POLDEK_ERROR,
				     PK_ERROR_ENUM_FAILED_INITIALIZATION,
				     "cannot load the installed packages");
		return FALSE;
	}
	return TRUE;
}

/*
 * Makes the contexts ready for a job that needs @need: sets them up on
 * first use or when stale, and reloads the installed packages if the rpm
 * database changed.
 */
gboolean
pk_poldek_ctx_prepare (PkPoldekCtx *pc, PkPoldekNeed need, GError **error)
{
	g_autofree gchar *stamp = NULL;

	if (pc->stale)
		pk_poldek_ctx_teardown (pc);
	pc->stale = FALSE;
	if (pc->ctx == NULL && !pk_poldek_ctx_setup (pc, error)) {
		pk_poldek_ctx_teardown (pc);
		return FALSE;
	}
	if (need == PK_POLDEK_NEED_SOURCES)
		return TRUE;
	if (pc->cctx == NULL) {
		if (pk_poldek_ctx_load (pc, error))
			return TRUE;
		pk_poldek_ctx_teardown (pc);
		return FALSE;
	}

	stamp = pk_poldek_ctx_rpmdb_stamp (pc->dbdir);
	if (g_strcmp0 (stamp, pc->rpmdb_stamp) == 0)
		return TRUE;
	g_debug ("rpm database changed, reloading installed packages");
	g_free (pc->rpmdb_stamp);
	pc->rpmdb_stamp = g_steal_pointer (&stamp);
	if (!pk_poldek_ctx_load_installed (pc, POCLIDEK_LOAD_RELOAD)) {
		g_set_error_literal (error,
				     PK_POLDEK_ERROR,
				     PK_ERROR_ENUM_FAILED_INITIALIZATION,
				     "cannot reload installed packages");
		pk_poldek_ctx_teardown (pc);
		return FALSE;
	}
	return TRUE;
}

/* After a transaction of ours: what it changed is applied without reloading the database. */
void
pk_poldek_ctx_transaction_done (PkPoldekCtx *pc, struct poldek_ts *ts)
{
	poclidek_apply_iinf (pc->cctx, ts);
	g_free (pc->rpmdb_stamp);
	pc->rpmdb_stamp = pk_poldek_ctx_rpmdb_stamp (pc->dbdir);
}

/*
 * Interrupts what poldek is doing, from any thread. poldek's callback stack
 * is never freed, so sigint_emit() is safe once any context was set up; a
 * cancel before that takes effect when one is.
 */
void
pk_poldek_ctx_interrupt (void)
{
	g_mutex_lock (&pk_poldek_ctx_sigint_lock);
	pk_poldek_ctx_cancel_requested = TRUE;
	if (pk_poldek_ctx_sigint_ready && !pk_poldek_ctx_loading_installed)
		sigint_emit ();
	g_mutex_unlock (&pk_poldek_ctx_sigint_lock);
}

/* at the start of every job, as a cancel of an earlier one leaves the flag set */
void
pk_poldek_ctx_reset_interrupt (void)
{
	g_mutex_lock (&pk_poldek_ctx_sigint_lock);
	pk_poldek_ctx_cancel_requested = FALSE;
	if (pk_poldek_ctx_sigint_ready)
		sigint_reset ();
	g_mutex_unlock (&pk_poldek_ctx_sigint_lock);
}

/* Whether the job was cancelled since the last reset. */
gboolean
pk_poldek_ctx_interrupted (void)
{
	gboolean requested;

	g_mutex_lock (&pk_poldek_ctx_sigint_lock);
	requested = pk_poldek_ctx_cancel_requested;
	g_mutex_unlock (&pk_poldek_ctx_sigint_lock);
	return requested;
}

/* The next job sets everything up again, after a change of configuration or repositories. */
void
pk_poldek_ctx_invalidate (PkPoldekCtx *pc)
{
	pc->stale = TRUE;
}

/* A new array the caller frees with n_array_free(). */
tn_array *
pk_poldek_ctx_get_installed (PkPoldekCtx *pc)
{
	tn_array *pkgs = poclidek_get_dent_packages (pc->cctx, POCLIDEK_INSTALLEDDIR, 0);

	return pkgs != NULL ? pkgs : pkgs_array_new (1);
}

/* A new array the caller frees with n_array_free(). */
tn_array *
pk_poldek_ctx_get_available (PkPoldekCtx *pc)
{
	tn_array *pkgs = poldek_get_avail_packages (pc->ctx);

	return pkgs != NULL ? pkgs : pkgs_array_new (1);
}
