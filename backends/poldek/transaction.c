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
 * Transactions. Every one is planned first with a test run of poldek, which
 * fills the summary of what it would install (I), pull in (D) and remove (R).
 * Then the packages to fetch are downloaded into poldek's cache and, for
 * only-trusted, checked against the root's keyring; the real run installs
 * from the cache without fetching again. Its ts_confirm sees the plan once
 * more and stops it if it is not the one checked.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <pk-enum.h>

#include <poldek/pkgcmp.h>
#include <poldek/pkgdir/pkgdir.h>
#include <poldek/pkgdir/source.h>
#include <poldek/pkgmisc.h>
#include <poldek/poldek.h>
#include <poldek/poldek_ts.h>
#include <poldek/sigint/sigint.h>
#include <trurl/nhash.h>
#include <vfile/vfile.h>

#include "errors.h"
#include "messages.h"
#include "pkgid.h"
#include "progress.h"
#include "protocol.h"
#include "query.h"
#include "transaction.h"
#include "verify.h"

/* what installing a package does to the installed ones */
typedef enum {
	PK_POLDEK_CHANGE_NEW,
	PK_POLDEK_CHANGE_UPGRADE,
	PK_POLDEK_CHANGE_REINSTALL,
	PK_POLDEK_CHANGE_DOWNGRADE,
} PkPoldekChange;

typedef struct
{
	PkPoldekCtx *pc;
	PkPoldekJob *job;
	PkBitfield flags;
	enum poldek_ts_type type;
	uint32_t tsflags;
	gboolean allow_deps;
	gboolean autoremove;
	/* struct pkg, each a reference */
	GPtrArray *requested;
	GPtrArray *install;
	GPtrArray *remove;
	/* the plan as package ids, to compare the real run's with */
	gchar *plan;
	gboolean plan_changed;
	gchar *cachedir;
	/* progress */
	PkPoldekRpmParser rpm;
	GHashTable *started;
	GHashTable *finished;
	guint steps;
	guint steps_done;
	guint percentage_from;
	const struct pkg *downloading;
	/* the packages download_total counts */
	GHashTable *counted;
	guint64 download_total;
	guint64 download_done;
} PkPoldekTx;

/* for poldek's ts_confirm callback, which has no data of its own here */
static PkPoldekTx *pk_poldek_tx_running = NULL;

static GPtrArray *
pk_poldek_pkgs_new (void)
{
	return g_ptr_array_new_with_free_func ((GDestroyNotify) pkg_free);
}

static void
pk_poldek_tx_init (PkPoldekTx *tx, PkPoldekCtx *pc, PkPoldekJob *job, PkBitfield flags)
{
	memset (tx, 0, sizeof (*tx));
	tx->pc = pc;
	tx->job = job;
	tx->flags = flags;
	tx->requested = pk_poldek_pkgs_new ();
	tx->install = pk_poldek_pkgs_new ();
	tx->remove = pk_poldek_pkgs_new ();
	tx->started = g_hash_table_new (NULL, NULL);
	tx->finished = g_hash_table_new (NULL, NULL);
	tx->counted = g_hash_table_new (NULL, NULL);
}

static void
pk_poldek_tx_clear (PkPoldekTx *tx)
{
	g_ptr_array_unref (tx->requested);
	g_ptr_array_unref (tx->install);
	g_ptr_array_unref (tx->remove);
	g_hash_table_unref (tx->started);
	g_hash_table_unref (tx->finished);
	g_hash_table_unref (tx->counted);
	g_free (tx->plan);
	g_free (tx->cachedir);
}

static PkBitfield
pk_poldek_request_get_flags (json_t *req)
{
	g_auto(GStrv) names = pk_poldek_request_get_strv (req, "flags");
	PkBitfield flags = 0;

	for (guint i = 0; names != NULL && names[i] != NULL; i++)
		pk_bitfield_add (flags, pk_transaction_flag_enum_from_string (names[i]));
	return flags;
}

static gboolean
pk_poldek_tx_has (PkPoldekTx *tx, PkTransactionFlagEnum flag)
{
	return pk_bitfield_contain (tx->flags, flag);
}

/*
 * Whether @pkgs has a package of the name, version and arch of @pkg: the
 * installed packages of a plan are other objects than the ones looked up.
 */
static gboolean
pk_poldek_pkgs_have_nevra (GPtrArray *pkgs, const struct pkg *pkg)
{
	for (guint i = 0; i < pkgs->len; i++) {
		if (pk_poldek_pkg_same_nevra (g_ptr_array_index (pkgs, i), pkg))
			return TRUE;
	}
	return FALSE;
}

/* the packages a transaction installs are available ones, those it removes installed ones */
static gchar *
pk_poldek_tx_pkg_id (PkPoldekTx *tx, const struct pkg *pkg)
{
	gboolean installed = g_ptr_array_find (tx->remove, pkg, NULL) ||
			     (tx->type == POLDEK_TS_TYPE_UNINSTALL &&
			      g_ptr_array_find (tx->requested, pkg, NULL));

	return pk_poldek_pkgid_build (pkg,
				      installed ? PK_POLDEK_PKG_INSTALLED
						: PK_POLDEK_PKG_AVAILABLE);
}

static void
pk_poldek_tx_status (PkPoldekTx *tx, PkStatusEnum status)
{
	pk_poldek_job_emit (tx->job,
			    "status",
			    json_pack ("{s:s}", "status", pk_status_enum_to_string (status)));
}

static void
pk_poldek_tx_percentage (PkPoldekTx *tx, guint value)
{
	pk_poldek_job_emit (tx->job, "percentage", json_pack ("{s:i}", "value", MIN (value, 100)));
}

static void
pk_poldek_tx_allow_cancel (PkPoldekTx *tx, gboolean allow)
{
	pk_poldek_job_emit (tx->job, "allow-cancel", json_pack ("{s:b}", "value", allow));
}

static void
pk_poldek_tx_package_status (PkPoldekTx *tx, const struct pkg *pkg, PkInfoEnum info)
{
	g_autofree gchar *id = pk_poldek_tx_pkg_id (tx, pkg);

	pk_poldek_job_emit (
	    tx->job,
	    "package-status",
	    json_pack ("{s:s, s:s}", "package_id", id, "info", pk_info_enum_to_string (info)));
}

static void
pk_poldek_tx_item_progress (PkPoldekTx *tx, const struct pkg *pkg, PkStatusEnum status, guint value)
{
	g_autofree gchar *id = pk_poldek_tx_pkg_id (tx, pkg);

	pk_poldek_job_emit (tx->job,
			    "item-progress",
			    json_pack ("{s:s, s:s, s:i}",
				       "package_id",
				       id,
				       "status",
				       pk_status_enum_to_string (status),
				       "value",
				       MIN (value, 100)));
}

static void
pk_poldek_tx_fail (PkPoldekTx *tx, PkErrorEnum fallback, const gchar *what)
{
	g_autofree gchar *messages = pk_poldek_messages_take_errors ();
	PkErrorEnum code = pk_poldek_error_from_messages (messages, fallback);

	if (pk_poldek_job_is_cancelled (tx->job) || sigint_reached ())
		code = PK_ERROR_ENUM_TRANSACTION_CANCELLED;
	pk_poldek_job_error (tx->job,
			     code,
			     "%s%s%s",
			     what,
			     messages != NULL ? ": " : "",
			     messages != NULL ? messages : "");
}

/* How installing @pkg changes what is installed. */
static PkPoldekChange
pk_poldek_change_of (tn_array *installed, const struct pkg *pkg)
{
	PkPoldekChange change = PK_POLDEK_CHANGE_NEW;

	for (int i = 0; i < n_array_size (installed); i++) {
		const struct pkg *ipkg = n_array_nth (installed, i);
		gint cmp;

		if (!pkg_is_kind_of (pkg, ipkg) || !pkg_is_arch_compat (pkg, ipkg))
			continue;
		cmp = pkg_cmp_evr (pkg, ipkg);
		if (cmp == 0)
			return PK_POLDEK_CHANGE_REINSTALL;
		if (cmp < 0)
			change = PK_POLDEK_CHANGE_DOWNGRADE;
		else if (change == PK_POLDEK_CHANGE_NEW)
			change = PK_POLDEK_CHANGE_UPGRADE;
	}
	return change;
}

static struct poldek_ts *
pk_poldek_tx_new_ts (PkPoldekTx *tx, gboolean test, gboolean greedy)
{
	struct poldek_ts *ts = poldek_ts_new (tx->pc->ctx, 0);

	if (tx->type == POLDEK_TS_TYPE_INSTALL) {
		poldek_ts_set_type (ts, POLDEK_TS_TYPE_INSTALL, "install");
		poldek_ts_setf (ts, tx->tsflags);
	} else {
		poldek_ts_set_type (ts, POLDEK_TS_TYPE_UNINSTALL, "uninstall");
		if (greedy)
			poldek_ts_setop (ts, POLDEK_OP_GREEDY, 1);
	}
	/* one set, all or nothing: particle install, poldek's default, runs and confirms
	 * the independent parts of a transaction one by one */
	poldek_ts_setop (ts, POLDEK_OP_PARTICLE, 0);
	if (test)
		poldek_ts_setop (ts, POLDEK_OP_TEST, 1);
	for (guint i = 0; i < tx->requested->len; i++)
		poldek_ts_add_pkg (ts, g_ptr_array_index (tx->requested, i));
	return ts;
}

static void
pk_poldek_summary_add (GPtrArray *to, const struct poldek_ts *ts, const gchar *mark)
{
	tn_array *pkgs = ts->ts_summary != NULL ? n_hash_get (ts->ts_summary, mark) : NULL;

	for (int i = 0; pkgs != NULL && i < n_array_size (pkgs); i++)
		g_ptr_array_add (to, pkg_link (n_array_nth (pkgs, i)));
}

/* The plan of @ts as sorted package ids, comparable between runs. */
static gchar *
pk_poldek_summary_ids (const struct poldek_ts *ts)
{
	const gchar *marks[] = { "I", "D", "R" };
	g_autoptr(GPtrArray) ids = g_ptr_array_new_with_free_func (g_free);

	for (guint m = 0; m < G_N_ELEMENTS (marks); m++) {
		tn_array *pkgs = ts->ts_summary != NULL ? n_hash_get (ts->ts_summary, marks[m])
							: NULL;

		for (int i = 0; pkgs != NULL && i < n_array_size (pkgs); i++) {
			const struct pkg *pkg = n_array_nth (pkgs, i);
			g_autofree gchar *evr = pk_poldek_pkg_evr (pkg);

			g_ptr_array_add (ids,
					 g_strdup_printf ("%s %s-%s.%s",
							  marks[m],
							  pkg->name,
							  evr,
							  pkg_arch (pkg)));
		}
	}
	g_ptr_array_sort_values (ids, (GCompareFunc) g_strcmp0);
	g_ptr_array_add (ids, NULL);
	return g_strjoinv ("\n", (gchar **) ids->pdata);
}

int
pk_poldek_ts_confirm_cb (void *user_data, const struct poldek_ts *ts)
{
	PkPoldekTx *tx = pk_poldek_tx_running;
	g_autofree gchar *plan = NULL;

	if (tx == NULL)
		return 1;
	plan = pk_poldek_summary_ids (ts);
	if (g_strcmp0 (plan, tx->plan) == 0)
		return 1;
	g_debug ("plan changed:\n%s\ninstead of\n%s", plan, tx->plan);
	tx->plan_changed = TRUE;
	/* poldek reports success then, without changing anything */
	return 0;
}

/* A test run of the transaction, which fills the plan; FALSE once the job has failed. */
static gboolean
pk_poldek_tx_plan_run (PkPoldekTx *tx, gboolean greedy)
{
	struct poldek_ts *ts = pk_poldek_tx_new_ts (tx, TRUE, greedy);
	int rc;

	g_ptr_array_set_size (tx->install, 0);
	g_ptr_array_set_size (tx->remove, 0);
	g_clear_pointer (&tx->plan, g_free);
	g_clear_pointer (&tx->cachedir, g_free);
	rc = poldek_ts_run (ts, POLDEK_TS_TRACK);
	if (rc) {
		/* D are the packages marked by dependencies: pulled in, or removed with the rest */
		pk_poldek_summary_add (tx->install, ts, "I");
		pk_poldek_summary_add (tx->type == POLDEK_TS_TYPE_INSTALL ? tx->install
									  : tx->remove,
				       ts,
				       "D");
		pk_poldek_summary_add (tx->remove, ts, "R");
		tx->plan = pk_poldek_summary_ids (ts);
		tx->cachedir = g_strdup (ts->cachedir);
	}
	poldek_ts_free (ts);
	if (!rc)
		pk_poldek_tx_fail (tx,
				   PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
				   "cannot resolve the transaction");
	return rc;
}

/* Plans the transaction and checks the plan; FALSE once the job has failed. */
static gboolean
pk_poldek_tx_plan (PkPoldekTx *tx)
{
	/* a greedy removal also takes the requirements nobody else needs, which are no
	 * dependents: those are checked on a plan without it */
	gboolean check_dependents = tx->type == POLDEK_TS_TYPE_UNINSTALL && !tx->allow_deps;
	g_autoptr(GString) dependents = g_string_new (NULL);

	pk_poldek_tx_status (tx, PK_STATUS_ENUM_DEP_RESOLVE);
	if (!pk_poldek_tx_plan_run (tx, tx->autoremove && !check_dependents))
		return FALSE;
	for (guint i = 0; i < tx->requested->len; i++) {
		const struct pkg *pkg = g_ptr_array_index (tx->requested, i);
		GPtrArray *planned = tx->type == POLDEK_TS_TYPE_INSTALL ? tx->install : tx->remove;

		/* poldek skips a package silently, e.g. one older than the installed one */
		if (tx->type == POLDEK_TS_TYPE_INSTALL
			? !g_ptr_array_find (planned, pkg, NULL)
			: !pk_poldek_pkgs_have_nevra (planned, pkg)) {
			g_autofree gchar *id = pk_poldek_tx_pkg_id (tx, pkg);

			pk_poldek_job_error (tx->job,
					     PK_ERROR_ENUM_TRANSACTION_ERROR,
					     "%s is not in the plan poldek made",
					     id);
			return FALSE;
		}
	}
	if (!check_dependents)
		return TRUE;
	for (guint i = 0; i < tx->remove->len; i++) {
		const struct pkg *pkg = g_ptr_array_index (tx->remove, i);

		if (!pk_poldek_pkgs_have_nevra (tx->requested, pkg))
			g_string_append_printf (dependents, " %s", pkg_id (pkg));
	}
	if (dependents->len > 0) {
		pk_poldek_job_error (tx->job,
				     PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
				     "removing them also removes, as they need them:%s",
				     dependents->str);
		return FALSE;
	}
	return !tx->autoremove || pk_poldek_tx_plan_run (tx, TRUE);
}

/* What a simulation reports: what each planned package would go through. */
static void
pk_poldek_tx_simulate (PkPoldekTx *tx)
{
	tn_array *installed = pk_poldek_ctx_get_installed (tx->pc);
	g_autoptr(GPtrArray) install = g_ptr_array_new ();
	g_autoptr(GPtrArray) remove = g_ptr_array_new ();

	/* copies without the references' free function */
	g_ptr_array_extend (install, tx->install, NULL, NULL);
	g_ptr_array_extend (remove, tx->remove, NULL, NULL);
	g_ptr_array_sort (install, pk_poldek_pkg_cmp);
	g_ptr_array_sort (remove, pk_poldek_pkg_cmp);
	for (guint i = 0; i < install->len; i++) {
		const struct pkg *pkg = g_ptr_array_index (install, i);
		PkInfoEnum info = PK_INFO_ENUM_INSTALL;

		switch (pk_poldek_change_of (installed, pkg)) {
		case PK_POLDEK_CHANGE_NEW:
			break;
		case PK_POLDEK_CHANGE_UPGRADE:
			info = PK_INFO_ENUM_UPDATE;
			break;
		case PK_POLDEK_CHANGE_REINSTALL:
			info = PK_INFO_ENUM_REINSTALL;
			break;
		case PK_POLDEK_CHANGE_DOWNGRADE:
			info = PK_INFO_ENUM_DOWNGRADE;
			break;
		}
		pk_poldek_queue_package (tx->job, pkg, FALSE, info);
	}
	for (guint i = 0; i < remove->len; i++) {
		const struct pkg *pkg = g_ptr_array_index (remove, i);
		gboolean replaced = FALSE;

		/* the old version of an upgrade goes with it */
		for (guint j = 0; j < install->len && !replaced; j++)
			replaced = pkg_is_kind_of (g_ptr_array_index (install, j), pkg);
		if (!replaced)
			pk_poldek_queue_package (tx->job,
						 pkg,
						 TRUE,
						 tx->type == POLDEK_TS_TYPE_UNINSTALL
						     ? PK_INFO_ENUM_REMOVE
						     : PK_INFO_ENUM_OBSOLETE);
	}
	n_array_free (installed);
}

/* vfile labels a download "[N/M] SOURCE::FILE" */
static const struct pkg *
pk_poldek_tx_find_download (PkPoldekTx *tx, const gchar *label)
{
	const gchar *file = g_strrstr (label, "::");

	file = file != NULL ? file + 2 : label;
	for (guint i = 0; i < tx->install->len; i++) {
		const struct pkg *pkg = g_ptr_array_index (tx->install, i);
		char name[PATH_MAX];

		if (pkg_filename (pkg, name, sizeof (name)) != NULL && strcmp (file, name) == 0)
			return pkg;
	}
	return NULL;
}

static void
pk_poldek_tx_download_done (PkPoldekTx *tx)
{
	if (tx->downloading == NULL)
		return;
	pk_poldek_tx_item_progress (tx, tx->downloading, PK_STATUS_ENUM_DOWNLOAD, 100);
	tx->download_done += tx->downloading->fsize;
	tx->downloading = NULL;
}

static void
pk_poldek_tx_download_cb (gpointer user_data, const gchar *label, glong total, glong amount)
{
	PkPoldekTx *tx = user_data;
	const struct pkg *pkg = pk_poldek_tx_find_download (tx, label);
	guint64 remaining;

	/* what vfile still reports once it is interrupted is no progress */
	if (pkg == NULL || pk_poldek_job_is_cancelled (tx->job) || sigint_reached ())
		return;
	/* vfile's end of the file */
	if (amount < 0)
		amount = total;
	if (pkg != tx->downloading) {
		pk_poldek_tx_download_done (tx);
		tx->downloading = pkg;
		/* a file in the cache that did not match its digests, downloaded again */
		if (g_hash_table_add (tx->counted, (gpointer) pkg))
			tx->download_total += pkg->fsize;
		pk_poldek_tx_package_status (tx, pkg, PK_INFO_ENUM_DOWNLOADING);
		pk_poldek_tx_item_progress (tx, pkg, PK_STATUS_ENUM_DOWNLOAD, 0);
	}
	if (total <= 0)
		return;
	pk_poldek_tx_item_progress (tx, pkg, PK_STATUS_ENUM_DOWNLOAD, amount * 100 / total);
	remaining = tx->download_total - MIN (tx->download_total, tx->download_done + amount);
	pk_poldek_job_emit (tx->job,
			    "download-size-remaining",
			    json_pack ("{s:I}", "value", (json_int_t) remaining));
	if (tx->download_total > 0)
		pk_poldek_tx_percentage (tx,
					 (tx->download_total - remaining) * 40 /
					     tx->download_total);
}

/*
 * Fetches the planned packages from remote repositories into @destdir, or
 * all of them with @copy_local for download-packages.
 */
static gboolean
pk_poldek_tx_download (PkPoldekTx *tx, const gchar *destdir, gboolean copy_local)
{
	PkPoldekSink sink = { NULL, pk_poldek_tx_download_cb, tx };
	tn_array *pkgs = pkgs_array_new (8);
	gboolean ok = TRUE;

	for (guint i = 0; i < tx->install->len; i++) {
		struct pkg *pkg = g_ptr_array_index (tx->install, i);
		char path[PATH_MAX];

		/* poldek reads packages in place from a path only, a cdrom:// source it fetches too */
		if (vf_url_type (pkg->pkgdir->path) == VFURL_PATH) {
			if (copy_local)
				n_array_push (pkgs, pkg_link (pkg));
			continue;
		}
		/* poldek takes a file from the cache only if it matches its digests, and
		 * downloads it again otherwise; it counts as already there */
		if ((copy_local || pkg_localpath (pkg, path, sizeof (path), destdir) == NULL ||
		     !g_file_test (path, G_FILE_TEST_EXISTS)) &&
		    g_hash_table_add (tx->counted, pkg))
			tx->download_total += pkg->fsize;
		n_array_push (pkgs, pkg_link (pkg));
	}
	if (n_array_size (pkgs) > 0) {
		pk_poldek_tx_status (tx, PK_STATUS_ENUM_DOWNLOAD);
		pk_poldek_job_emit (tx->job,
				    "download-size-remaining",
				    json_pack ("{s:I}", "value", (json_int_t) tx->download_total));
		pk_poldek_messages_set_sink (&sink);
		ok = packages_fetch (poldek_get_pmctx (tx->pc->ctx), pkgs, destdir, copy_local);
		pk_poldek_messages_set_sink (NULL);
		if (ok && !sigint_reached ())
			pk_poldek_tx_download_done (tx);
		ok = ok && !sigint_reached () && !pk_poldek_job_is_cancelled (tx->job);
		if (ok)
			pk_poldek_job_emit (tx->job,
					    "download-size-remaining",
					    json_pack ("{s:i}", "value", 0));
		else
			pk_poldek_tx_fail (tx,
					   PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED,
					   "cannot download the packages");
	}
	n_array_free (pkgs);
	return ok;
}

/* only-trusted: every planned package file has to be signed with a key the root trusts */
static gboolean
pk_poldek_tx_verify (PkPoldekTx *tx)
{
	pk_poldek_tx_status (tx, PK_STATUS_ENUM_SIG_CHECK);
	for (guint i = 0; i < tx->install->len; i++) {
		const struct pkg *pkg = g_ptr_array_index (tx->install, i);
		GError *error = NULL;
		char path[PATH_MAX];

		if (pkg_localpath (pkg, path, sizeof (path), tx->cachedir) == NULL) {
			pk_poldek_job_error (tx->job,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     "no local path for %s",
					     pkg_id (pkg));
			return FALSE;
		}
		if (!pk_poldek_verify_package (tx->pc->root, path, &error)) {
			pk_poldek_job_take_error (tx->job, error);
			return FALSE;
		}
	}
	return TRUE;
}

/* the planned package rpm says it works on */
static const struct pkg *
pk_poldek_tx_find_rpm_pkg (PkPoldekTx *tx, PkPoldekRpmPhase phase, const gchar *label)
{
	GPtrArray *pkgs = phase == PK_POLDEK_RPM_CLEANUP ? tx->remove : tx->install;

	for (guint i = 0; i < pkgs->len; i++) {
		const struct pkg *pkg = g_ptr_array_index (pkgs, i);

		/* the same label for several arches comes once per arch, in some order */
		if (pk_poldek_rpm_label_names (label, pkg->name, pkg->epoch, pkg->ver, pkg->rel) &&
		    !g_hash_table_contains (tx->finished, pkg))
			return pkg;
	}
	return NULL;
}

static void
pk_poldek_tx_step_done (PkPoldekTx *tx)
{
	tx->steps_done++;
	pk_poldek_tx_percentage (tx,
				 tx->percentage_from + (100 - tx->percentage_from) *
							   MIN (tx->steps_done, tx->steps) /
							   tx->steps);
}

static void
pk_poldek_tx_rpm_phase_cb (gpointer user_data, PkPoldekRpmPhase phase)
{
	PkPoldekTx *tx = user_data;
	const PkStatusEnum statuses[] = {
		[PK_POLDEK_RPM_VERIFY] = PK_STATUS_ENUM_SIG_CHECK,
		[PK_POLDEK_RPM_PREPARE] = PK_STATUS_ENUM_TEST_COMMIT,
		[PK_POLDEK_RPM_INSTALL] = PK_STATUS_ENUM_INSTALL,
		[PK_POLDEK_RPM_CLEANUP] = PK_STATUS_ENUM_CLEANUP,
	};

	pk_poldek_tx_status (tx, statuses[phase]);
}

static void
pk_poldek_tx_rpm_progress_cb (gpointer user_data,
			      PkPoldekRpmPhase phase,
			      const gchar *label,
			      guint percentage)
{
	PkPoldekTx *tx = user_data;
	PkStatusEnum status = phase == PK_POLDEK_RPM_CLEANUP ? PK_STATUS_ENUM_CLEANUP
							     : PK_STATUS_ENUM_INSTALL;
	const struct pkg *pkg;

	if (label == NULL) {
		if (percentage == 100)
			pk_poldek_tx_step_done (tx);
		return;
	}
	pkg = pk_poldek_tx_find_rpm_pkg (tx, phase, label);
	if (pkg == NULL)
		return;
	if (g_hash_table_add (tx->started, (gpointer) pkg)) {
		pk_poldek_tx_package_status (tx,
					     pkg,
					     phase == PK_POLDEK_RPM_CLEANUP
						 ? PK_INFO_ENUM_CLEANUP
						 : PK_INFO_ENUM_INSTALLING);
		pk_poldek_tx_item_progress (tx, pkg, status, 0);
	}
	pk_poldek_tx_item_progress (tx, pkg, status, percentage);
	if (percentage == 100) {
		g_hash_table_add (tx->finished, (gpointer) pkg);
		pk_poldek_tx_step_done (tx);
	}
}

static void
pk_poldek_tx_rpm_output_cb (gpointer user_data, const gchar *text)
{
	PkPoldekTx *tx = user_data;

	pk_poldek_rpm_parser_feed (&tx->rpm, text);
}

/* The real run, which installs or removes. */
static gboolean
pk_poldek_tx_commit (PkPoldekTx *tx)
{
	PkPoldekSink sink = { pk_poldek_tx_rpm_output_cb, NULL, tx };
	struct poldek_ts *ts = pk_poldek_tx_new_ts (tx, FALSE, tx->autoremove);
	gboolean uninstall = tx->type == POLDEK_TS_TYPE_UNINSTALL;
	int rc;

	if (pk_poldek_job_is_cancelled (tx->job) || sigint_reached ()) {
		poldek_ts_free (ts);
		pk_poldek_job_error (tx->job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Cancelled");
		return FALSE;
	}
	/* rpm cannot be stopped halfway */
	pk_poldek_tx_allow_cancel (tx, FALSE);
	if (pk_poldek_tx_has (tx, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED))
		poldek_ts_configure (ts, POLDEK_CONF_RPMMACROS, "_pkgverify_level signature");

	tx->percentage_from = tx->download_total > 0 ? 40 : 0;
	/* rpm's verify and prepare bars, then one per package; rpm --erase shows none */
	tx->steps = uninstall ? 1 : 2 + tx->install->len + tx->remove->len;
	pk_poldek_tx_status (tx, uninstall ? PK_STATUS_ENUM_REMOVE : PK_STATUS_ENUM_INSTALL);
	if (uninstall) {
		for (guint i = 0; i < tx->remove->len; i++)
			pk_poldek_tx_package_status (tx,
						     g_ptr_array_index (tx->remove, i),
						     PK_INFO_ENUM_REMOVING);
	}
	pk_poldek_rpm_parser_init (&tx->rpm,
				   pk_poldek_tx_rpm_phase_cb,
				   pk_poldek_tx_rpm_progress_cb,
				   tx);
	pk_poldek_tx_running = tx;
	pk_poldek_messages_set_sink (&sink);
	rc = poldek_ts_run (ts, POLDEK_TS_TRACK);
	pk_poldek_messages_set_sink (NULL);
	pk_poldek_tx_running = NULL;
	pk_poldek_rpm_parser_clear (&tx->rpm);

	if (rc && !tx->plan_changed)
		pk_poldek_ctx_transaction_done (tx->pc, ts);
	else
		pk_poldek_ctx_invalidate (tx->pc);
	poldek_ts_free (ts);
	if (tx->plan_changed) {
		pk_poldek_job_error (tx->job,
				     PK_ERROR_ENUM_TRANSACTION_ERROR,
				     "the packages changed while the transaction was prepared");
		return FALSE;
	}
	if (!rc) {
		pk_poldek_tx_fail (tx, PK_ERROR_ENUM_TRANSACTION_ERROR, "the transaction failed");
		return FALSE;
	}
	if (uninstall) {
		for (guint i = 0; i < tx->remove->len; i++)
			pk_poldek_tx_item_progress (tx,
						    g_ptr_array_index (tx->remove, i),
						    PK_STATUS_ENUM_REMOVE,
						    100);
	}
	pk_poldek_tx_percentage (tx, 100);
	return TRUE;
}

static void
pk_poldek_tx_run (PkPoldekTx *tx)
{
	pk_poldek_tx_allow_cancel (tx, TRUE);
	pk_poldek_messages_reset ();
	if (!pk_poldek_tx_plan (tx))
		return;
	if (pk_poldek_tx_has (tx, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		pk_poldek_tx_simulate (tx);
		return;
	}
	if (tx->type == POLDEK_TS_TYPE_INSTALL) {
		if (!pk_poldek_tx_download (tx, tx->cachedir, FALSE))
			return;
		if (pk_poldek_tx_has (tx, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED) &&
		    !pk_poldek_tx_verify (tx))
			return;
		if (pk_poldek_tx_has (tx, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD)) {
			pk_poldek_tx_percentage (tx, 100);
			return;
		}
	}
	pk_poldek_tx_commit (tx);
}

/*
 * Picks the poldek mode for what the packages to install change, or fails
 * the job: poldek's downgrade mode also changes how it resolves
 * dependencies, and one transaction cannot both upgrade and downgrade.
 */
static gboolean
pk_poldek_tx_choose_mode (PkPoldekTx *tx)
{
	tn_array *installed = pk_poldek_ctx_get_installed (tx->pc);
	guint counts[PK_POLDEK_CHANGE_DOWNGRADE + 1] = { 0 };
	gboolean reinstall = pk_poldek_tx_has (tx, PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL) ||
			     pk_poldek_tx_has (tx, PK_TRANSACTION_FLAG_ENUM_JUST_REINSTALL);
	guint others;
	gboolean ok = FALSE;

	for (guint i = 0; i < tx->requested->len; i++)
		counts[pk_poldek_change_of (installed, g_ptr_array_index (tx->requested, i))]++;
	n_array_free (installed);
	others = counts[PK_POLDEK_CHANGE_NEW] + counts[PK_POLDEK_CHANGE_UPGRADE];

	tx->tsflags = POLDEK_TS_UPGRADE;
	if (pk_poldek_tx_has (tx, PK_TRANSACTION_FLAG_ENUM_JUST_REINSTALL) &&
	    counts[PK_POLDEK_CHANGE_REINSTALL] != tx->requested->len)
		pk_poldek_job_error (tx->job,
				     PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED,
				     "only installed versions can be reinstalled");
	else if (counts[PK_POLDEK_CHANGE_REINSTALL] > 0 && !reinstall)
		pk_poldek_job_error (tx->job,
				     counts[PK_POLDEK_CHANGE_REINSTALL] == tx->requested->len
					 ? PK_ERROR_ENUM_ALL_PACKAGES_ALREADY_INSTALLED
					 : PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED,
				     "already installed in this version");
	else if (counts[PK_POLDEK_CHANGE_REINSTALL] > 0 &&
		 others + counts[PK_POLDEK_CHANGE_DOWNGRADE] > 0)
		pk_poldek_job_error (tx->job,
				     PK_ERROR_ENUM_NOT_SUPPORTED,
				     "reinstall these packages in a transaction of their own");
	else if (counts[PK_POLDEK_CHANGE_DOWNGRADE] > 0 && others > 0)
		pk_poldek_job_error (tx->job,
				     PK_ERROR_ENUM_NOT_SUPPORTED,
				     "downgrade these packages in a transaction of their own");
	else if (counts[PK_POLDEK_CHANGE_DOWNGRADE] > 0 &&
		 !pk_poldek_tx_has (tx, PK_TRANSACTION_FLAG_ENUM_ALLOW_DOWNGRADE))
		pk_poldek_job_error (tx->job,
				     PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED,
				     "a newer version is installed");
	else
		ok = TRUE;
	if (counts[PK_POLDEK_CHANGE_REINSTALL] > 0)
		tx->tsflags |= POLDEK_TS_REINSTALL;
	if (counts[PK_POLDEK_CHANGE_DOWNGRADE] > 0)
		tx->tsflags |= POLDEK_TS_DOWNGRADE;
	return ok;
}

/* The available package for @package_id, also for an installed one to reinstall. */
static struct pkg *
pk_poldek_tx_lookup_available (PkPoldekTx *tx, const gchar *package_id, GError **error)
{
	gboolean installed = FALSE;
	struct pkg *pkg = pk_poldek_lookup (tx->pc, package_id, &installed, error);
	tn_array *candidates;
	struct pkg *found = NULL;

	if (pkg == NULL || !installed)
		return pkg;
	candidates = poldek_search_avail_packages (tx->pc->ctx, POLDEK_ST_NAME, pkg->name);
	for (int i = 0; candidates != NULL && found == NULL && i < n_array_size (candidates); i++) {
		struct pkg *candidate = n_array_nth (candidates, i);

		if (pk_poldek_pkg_same_nevra (candidate, pkg))
			found = pkg_link (candidate);
	}
	if (candidates != NULL)
		n_array_free (candidates);
	if (found == NULL)
		g_set_error (error,
			     PK_POLDEK_ERROR,
			     PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
			     "%s is in no repository",
			     package_id);
	pkg_free (pkg);
	return found;
}

static void
pk_poldek_install_or_update (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_auto(GStrv) ids = pk_poldek_require_strv (job, req, "package_ids");
	PkPoldekTx tx;

	if (ids == NULL)
		return;
	pk_poldek_tx_init (&tx, pc, job, pk_poldek_request_get_flags (req));
	tx.type = POLDEK_TS_TYPE_INSTALL;
	for (guint i = 0; ids[i] != NULL; i++) {
		GError *error = NULL;
		struct pkg *pkg = pk_poldek_tx_lookup_available (&tx, ids[i], &error);

		if (pkg == NULL) {
			pk_poldek_job_take_error (job, error);
			pk_poldek_tx_clear (&tx);
			return;
		}
		g_ptr_array_add (tx.requested, pkg);
	}
	if (pk_poldek_tx_choose_mode (&tx))
		pk_poldek_tx_run (&tx);
	pk_poldek_tx_clear (&tx);
}

void
pk_poldek_install_packages (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	pk_poldek_install_or_update (pc, job, req);
}

void
pk_poldek_update_packages (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	pk_poldek_install_or_update (pc, job, req);
}

void
pk_poldek_remove_packages (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_auto(GStrv) ids = pk_poldek_require_strv (job, req, "package_ids");
	PkPoldekTx tx;

	if (ids == NULL)
		return;
	pk_poldek_tx_init (&tx, pc, job, pk_poldek_request_get_flags (req));
	tx.type = POLDEK_TS_TYPE_UNINSTALL;
	tx.allow_deps = pk_poldek_request_get_bool (req, "allow_deps");
	tx.autoremove = pk_poldek_request_get_bool (req, "autoremove");
	for (guint i = 0; ids[i] != NULL; i++) {
		GError *error = NULL;
		gboolean installed = FALSE;
		struct pkg *pkg = pk_poldek_lookup (pc, ids[i], &installed, &error);

		if (pkg == NULL || !installed) {
			if (pkg != NULL) {
				pkg_free (pkg);
				pk_poldek_job_error (job,
						     PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED,
						     "%s is not an installed package",
						     ids[i]);
			} else {
				pk_poldek_job_take_error (job, error);
			}
			pk_poldek_tx_clear (&tx);
			return;
		}
		g_ptr_array_add (tx.requested, pkg);
	}
	pk_poldek_tx_run (&tx);
	pk_poldek_tx_clear (&tx);
}

/* @files without repeats, also through links, which the directory source would read once */
static gchar **
pk_poldek_unique_files (gchar **files)
{
	g_autoptr(GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GStrvBuilder) unique = g_strv_builder_new ();

	for (guint i = 0; files[i] != NULL; i++) {
		struct stat st;
		gchar *key = stat (files[i], &st) == 0 ? g_strdup_printf ("%ju:%ju",
									  (uintmax_t) st.st_dev,
									  (uintmax_t) st.st_ino)
						       : g_strdup (files[i]);

		if (g_hash_table_add (seen, key))
			g_strv_builder_add (unique, files[i]);
	}
	return g_strv_builder_end (unique);
}

/*
 * Installs package files: a link to each in a private directory, which a
 * context of its own has as one more source. The job's context is let go
 * first, poldek's interrupt handling allows one set up at a time.
 */
void
pk_poldek_install_files (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_auto(GStrv) given = pk_poldek_require_strv (job, req, "files");
	g_auto(GStrv) files = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *packages = NULL;
	g_autofree gchar *config = NULL;
	PkPoldekCtx *local = NULL;
	tn_array *available = NULL;
	PkPoldekTx tx;

	if (given == NULL)
		return;
	files = pk_poldek_unique_files (given);
	dir = g_dir_make_tmp ("pk-poldek-files-XXXXXX", &error);
	if (dir == NULL) {
		pk_poldek_job_error (job, PK_ERROR_ENUM_INTERNAL_ERROR, "%s", error->message);
		return;
	}
	packages = g_build_filename (dir, "packages", NULL);
	config = g_build_filename (dir, "poldek.conf", NULL);
	g_mkdir (packages, 0700);
	for (guint i = 0; files[i] != NULL; i++) {
		g_autofree gchar *link = g_strdup_printf ("%s/%u.rpm", packages, i);

		if (symlink (files[i], link) < 0) {
			pk_poldek_job_error (job,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     "cannot link %s: %s",
					     files[i],
					     g_strerror (errno));
			goto out;
		}
	}

	pk_poldek_ctx_release (pc);
	local = pk_poldek_ctx_new_with_source (pc,
					       PK_POLDEK_LOCAL_SOURCE,
					       packages,
					       config,
					       &error);
	if (local == NULL) {
		pk_poldek_job_error (job, PK_ERROR_ENUM_INTERNAL_ERROR, "%s", error->message);
		goto out;
	}
	if (!pk_poldek_ctx_prepare (local, PK_POLDEK_NEED_PACKAGES, &error)) {
		pk_poldek_job_take_error (job, g_steal_pointer (&error));
		goto out;
	}
	pk_poldek_tx_init (&tx, local, job, pk_poldek_request_get_flags (req));
	tx.type = POLDEK_TS_TYPE_INSTALL;
	available = pk_poldek_ctx_get_available (local);
	for (int i = 0; i < n_array_size (available); i++) {
		struct pkg *pkg = n_array_nth (available, i);

		if (g_strcmp0 (pkg->pkgdir->name, PK_POLDEK_LOCAL_SOURCE) == 0)
			g_ptr_array_add (tx.requested, pkg_link (pkg));
	}
	/* a file the directory source did not take is no package it could read */
	if (tx.requested->len != g_strv_length (files))
		pk_poldek_job_error (job,
				     PK_ERROR_ENUM_INVALID_PACKAGE_FILE,
				     "%u of %u files are packages that can be installed",
				     tx.requested->len,
				     g_strv_length (files));
	else if (pk_poldek_tx_choose_mode (&tx))
		pk_poldek_tx_run (&tx);
	pk_poldek_tx_clear (&tx);
out:
	if (available != NULL)
		n_array_free (available);
	if (local != NULL)
		pk_poldek_ctx_free (local);
	for (guint i = 0; files[i] != NULL; i++) {
		g_autofree gchar *link = g_strdup_printf ("%s/%u.rpm", packages, i);

		g_unlink (link);
	}
	g_rmdir (packages);
	g_unlink (config);
	g_rmdir (dir);
}

/*
 * Upgrades everything that has an update, as poldek --upgrade-dist does;
 * held packages stay. PLD is rolling, so there is no distribution to name.
 */
void
pk_poldek_upgrade_system (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	PkPoldekTx tx;

	pk_poldek_tx_init (&tx, pc, job, pk_poldek_request_get_flags (req));
	tx.type = POLDEK_TS_TYPE_INSTALL;
	tx.tsflags = POLDEK_TS_UPGRADEDIST;
	pk_poldek_tx_run (&tx);
	pk_poldek_tx_clear (&tx);
}

/*
 * Fetches the packages themselves, not their dependencies, into "directory",
 * or into poldek's cache without one, as the daemon asks to store them there.
 */
void
pk_poldek_download_packages (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_auto(GStrv) ids = pk_poldek_require_strv (job, req, "package_ids");
	const gchar *directory = json_string_value (json_object_get (req, "directory"));
	PkPoldekTx tx;

	if (ids == NULL)
		return;
	pk_poldek_tx_init (&tx, pc, job, 0);
	if (directory == NULL) {
		struct poldek_ts *ts = poldek_ts_new (pc->ctx, 0);

		tx.cachedir = g_strdup (ts->cachedir);
		poldek_ts_free (ts);
	}
	pk_poldek_messages_reset ();
	pk_poldek_tx_allow_cancel (&tx, TRUE);
	for (guint i = 0; ids[i] != NULL; i++) {
		GError *error = NULL;
		struct pkg *pkg = pk_poldek_tx_lookup_available (&tx, ids[i], &error);

		if (pkg == NULL) {
			pk_poldek_job_take_error (job, error);
			pk_poldek_tx_clear (&tx);
			return;
		}
		g_ptr_array_add (tx.install, pkg);
	}
	if (pk_poldek_tx_download (&tx,
				   directory != NULL ? directory : tx.cachedir,
				   directory != NULL)) {
		for (guint i = 0; i < tx.install->len; i++) {
			const struct pkg *pkg = g_ptr_array_index (tx.install, i);
			g_autofree gchar *id = pk_poldek_pkgid_build (pkg, PK_POLDEK_PKG_AVAILABLE);
			g_autofree gchar *path = NULL;
			char name[PATH_MAX];

			if (directory != NULL) {
				pkg_filename (pkg, name, sizeof (name));
				path = g_build_filename (directory, name, NULL);
			} else if (pkg_localpath (pkg, name, sizeof (name), tx.cachedir) != NULL) {
				/* in the cache directory of its source, or read in place from a local one */
				path = g_strdup (name);
			} else {
				continue;
			}
			pk_poldek_job_emit (
			    job,
			    "files",
			    json_pack ("{s:s, s:{s:s}}",
				       "package_id",
				       id,
				       "files",
				       path,
				       pk_file_kind_enum_to_string (PK_FILE_KIND_ENUM_REGULAR)));
		}
		pk_poldek_tx_percentage (&tx, 100);
	}
	pk_poldek_tx_clear (&tx);
}

/* Updates the indexes of the enabled repositories; the next job loads them. */
void
pk_poldek_refresh_cache (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	tn_array *sources = poldek_get_sources (pc->ctx);
	gboolean force = pk_poldek_request_get_bool (req, "force");
	gboolean ok = TRUE;

	pk_poldek_messages_reset ();
	pk_poldek_job_emit (
	    job,
	    "status",
	    json_pack ("{s:s}", "status", pk_status_enum_to_string (PK_STATUS_ENUM_REFRESH_CACHE)));
	for (int i = 0; sources != NULL && i < n_array_size (sources); i++) {
		struct source *src = n_array_nth (sources, i);
		int caps;

		/* as poldek --up, without naming sources */
		if ((src->flags & (PKGSOURCE_NOAUTO | PKGSOURCE_NOAUTOUP)) ||
		    g_strcmp0 (src->type, source_TYPE_GROUP) == 0)
			continue;
		caps = pkgdir_type_info (src->type);
		/* a source without an index, such as a directory of packages, is read anew anyway */
		if ((caps & (PKGDIR_CAP_UPDATEABLE_INC | PKGDIR_CAP_UPDATEABLE)) == 0)
			continue;
		/* as poldek --up does; force, or an index that cannot be patched, downloads all of it */
		if (force || (caps & PKGDIR_CAP_UPDATEABLE_INC) == 0)
			ok = source_update (src, PKGSOURCE_UPA) && ok;
		else
			ok = source_update (src, PKGSOURCE_UP | PKGSOURCE_UPAUTOA) && ok;
	}
	if (sources != NULL)
		n_array_free (sources);
	pk_poldek_ctx_invalidate (pc);
	if (!ok) {
		g_autofree gchar *messages = pk_poldek_messages_take_errors ();
		PkErrorEnum code = pk_poldek_error_from_messages (messages,
								  PK_ERROR_ENUM_REPO_NOT_AVAILABLE);

		/* what failed to download is an index: the repository is not available */
		if (code == PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED)
			code = PK_ERROR_ENUM_REPO_NOT_AVAILABLE;
		pk_poldek_job_error (job,
				     code,
				     "cannot update the repositories%s%s",
				     messages != NULL ? ": " : "",
				     messages != NULL ? messages : "");
	}
}
