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
 * Updates: the newest available package that can replace an installed one,
 * the way poldek's ls -u and upgrade see it: same name and color or arch,
 * a higher version. PLD has no update advisories, so the details come from
 * the changelog entries newer than the installed package.
 */

#include <string.h>

#include <pk-enum.h>

#include <poldek/pkgcmp.h>
#include <poldek/pkgfl.h>
#include <poldek/pkgu.h>

#include "filters.h"
#include "pkgid.h"
#include "protocol.h"
#include "query.h"
#include "updates.h"

typedef struct
{
	struct pkg *update;
	struct pkg *installed;
} PkPoldekUpdate;

/* Whether @update can replace @installed. */
static gboolean
pk_poldek_is_update_of (const struct pkg *update, const struct pkg *installed)
{
	return pkg_is_kind_of (update, installed) && pkg_is_arch_compat (update, installed) &&
	       pkg_cmp_evr (update, installed) > 0;
}

/* The updates of @installed among @available, one per available package. */
static GArray *
pk_poldek_find_updates (tn_array *installed, tn_array *available)
{
	g_autoptr(GHashTable) by_name = g_hash_table_new_full (g_str_hash,
							       g_str_equal,
							       NULL,
							       (GDestroyNotify) g_ptr_array_unref);
	g_autoptr(GHashTable) installed_by_name = g_hash_table_new_full (
	    g_str_hash,
	    g_str_equal,
	    NULL,
	    (GDestroyNotify) g_ptr_array_unref);
	g_autoptr(GHashTable) seen = g_hash_table_new (NULL, NULL);
	GArray *updates = g_array_new (FALSE, FALSE, sizeof (PkPoldekUpdate));

	for (int i = 0; i < n_array_size (available); i++) {
		struct pkg *pkg = n_array_nth (available, i);
		GPtrArray *same = g_hash_table_lookup (by_name, pkg->name);

		if (same == NULL) {
			same = g_ptr_array_new ();
			g_hash_table_insert (by_name, pkg->name, same);
		}
		g_ptr_array_add (same, pkg);
	}
	for (int i = 0; i < n_array_size (installed); i++) {
		struct pkg *ipkg = n_array_nth (installed, i);
		GPtrArray *same = g_hash_table_lookup (installed_by_name, ipkg->name);

		if (same == NULL) {
			same = g_ptr_array_new ();
			g_hash_table_insert (installed_by_name, ipkg->name, same);
		}
		g_ptr_array_add (same, ipkg);
	}
	for (int i = 0; i < n_array_size (installed); i++) {
		struct pkg *ipkg = n_array_nth (installed, i);
		GPtrArray *same = g_hash_table_lookup (by_name, ipkg->name);
		GPtrArray *versions = g_hash_table_lookup (installed_by_name, ipkg->name);
		PkPoldekUpdate update = { NULL, ipkg };

		for (guint j = 0; same != NULL && j < same->len; j++) {
			struct pkg *pkg = g_ptr_array_index (same, j);

			if (pk_poldek_is_update_of (pkg, ipkg) &&
			    (update.update == NULL || pkg_cmp_evr (pkg, update.update) > 0))
				update.update = pkg;
		}
		/* as poldek ls -u: newer than the newest installed version, not just this one */
		for (guint j = 0; update.update != NULL && j < versions->len; j++) {
			const struct pkg *version = g_ptr_array_index (versions, j);

			if (pkg_is_kind_of (update.update, version) &&
			    pkg_is_arch_compat (update.update, version) &&
			    pkg_cmp_evr (version, update.update) >= 0)
				update.update = NULL;
		}
		if (update.update != NULL && g_hash_table_add (seen, update.update))
			g_array_append_val (updates, update);
	}
	return updates;
}

static gint
pk_poldek_update_cmp (gconstpointer a, gconstpointer b)
{
	const PkPoldekUpdate *u1 = a;
	const PkPoldekUpdate *u2 = b;

	return pk_poldek_pkg_cmp (&u1->update, &u2->update);
}

/*
 * The package whose changelog tells what changed in @pkg: poldek gives a
 * subpackage only "- see MAIN's log", so that is its main package, the one
 * in @available whose NAME-VERSION-RELEASE is the source rpm's name. poldek
 * keeps that name, without .src.rpm, for a subpackage only, and for any
 * package of a .nosrc.rpm, with .nosrc.rpm.
 */
static const struct pkg *
pk_poldek_changelog_pkg (const struct pkg *pkg, tn_array *available)
{
	if (pkg->srcfn == NULL)
		return pkg;
	for (int i = 0; i < n_array_size (available); i++) {
		const struct pkg *main_pkg = n_array_nth (available, i);
		g_autofree gchar *nvr = NULL;
		gsize len;

		if (main_pkg == pkg || !g_str_has_prefix (pkg->srcfn, main_pkg->name))
			continue;
		nvr = g_strdup_printf ("%s-%s-%s", main_pkg->name, main_pkg->ver, main_pkg->rel);
		len = strlen (nvr);
		if (strncmp (nvr, pkg->srcfn, len) == 0 &&
		    (pkg->srcfn[len] == '\0' || strcmp (pkg->srcfn + len, ".nosrc.rpm") == 0))
			return main_pkg;
	}
	return pkg;
}

static gboolean
pk_poldek_has_security_fixes (const struct pkg *update,
			      const struct pkg *installed,
			      tn_array *available)
{
	struct pkguinf *inf;
	gboolean security;

	/* poldek asserts a time to look from */
	if (installed->btime == 0 ||
	    (inf = pkg_uinf (pk_poldek_changelog_pkg (update, available))) == NULL)
		return FALSE;
	security = pkguinf_changelog_with_security_fixes (inf, installed->btime);
	pkguinf_free (inf);
	return security;
}

void
pk_poldek_get_updates (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	PkBitfield filters = pk_poldek_request_get_filters (req);
	tn_array *installed = pk_poldek_ctx_get_installed (pc);
	tn_array *available = pk_poldek_ctx_get_available (pc);
	g_autoptr(GArray) updates = pk_poldek_find_updates (installed, available);

	g_array_sort (updates, pk_poldek_update_cmp);
	for (guint i = 0; i < updates->len; i++) {
		const PkPoldekUpdate *update = &g_array_index (updates, PkPoldekUpdate, i);
		PkInfoEnum info = PK_INFO_ENUM_UPDATE;

		if (!pk_poldek_filters_accept (filters, update->update))
			continue;
		if (pk_poldek_has_security_fixes (update->update, update->installed, available))
			info = PK_INFO_ENUM_UPDATE_SECURITY;
		/* matched by a hold pattern of the configuration: blocked, keeping its severity */
		if (pkg_is_scored (update->update, PKG_HELD))
			pk_poldek_queue_update (job,
						update->update,
						FALSE,
						PK_INFO_ENUM_BLOCKED,
						info == PK_INFO_ENUM_UPDATE_SECURITY
						    ? PK_SEVERITY_ENUM_HIGH
						    : PK_SEVERITY_ENUM_MEDIUM);
		else
			pk_poldek_queue_package (job, update->update, FALSE, info);
	}
	n_array_free (installed);
	n_array_free (available);
}

/* Whether @pkg ships a kernel image, which only a reboot starts. */
static gboolean
pk_poldek_pkg_is_kernel (const struct pkg *pkg)
{
	struct pkgflist_it *it = pkg_get_flist_it (pkg);
	const gchar *path;
	gboolean found = FALSE;

	while (it != NULL && !found && (path = pkgflist_it_get (it, NULL)) != NULL)
		found = g_str_has_prefix (path, "/boot/vmlinuz");
	if (it != NULL)
		pkgflist_it_free (it);
	return found;
}

static PkRestartEnum
pk_poldek_restart_for (const struct pkg *pkg)
{
	if (strcmp (pkg->name, "glibc") == 0 || pk_poldek_pkg_is_kernel (pkg))
		return PK_RESTART_ENUM_SYSTEM;
	if (strcmp (pkg->name, "dbus") == 0)
		return PK_RESTART_ENUM_SESSION;
	return PK_RESTART_ENUM_NONE;
}

/* CVE ids mentioned in @text, each once, in the order they appear */
static json_t *
pk_poldek_cve_issues (const gchar *text)
{
	static GRegex *regex = NULL;
	g_autoptr(GMatchInfo) match = NULL;
	g_autoptr(GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	json_t *issues = json_array ();

	if (regex == NULL)
		regex = g_regex_new ("CVE-[0-9]{4}-[0-9]{4,}", G_REGEX_OPTIMIZE, 0, NULL);
	g_regex_match (regex, text != NULL ? text : "", 0, &match);
	while (g_match_info_matches (match)) {
		gchar *id = g_match_info_fetch (match, 0);

		if (g_hash_table_add (seen, id)) {
			g_autofree gchar *url = g_strdup_printf (
			    "https://www.cve.org/CVERecord?id=%s",
			    id);

			json_array_append_new (
			    issues,
			    json_pack ("{s:s, s:s, s:s}",
				       "kind",
				       pk_issue_kind_enum_to_string (PK_ISSUE_KIND_ENUM_CVE),
				       "url",
				       url,
				       "id",
				       id));
		}
		g_match_info_next (match, NULL);
	}
	return issues;
}

static json_t *
pk_poldek_update_detail (const struct pkg *pkg,
			 gboolean is_installed,
			 tn_array *installed,
			 tn_array *available)
{
	g_autofree gchar *id = pk_poldek_pkgid_build (pkg,
						      is_installed ? PK_POLDEK_PKG_INSTALLED
								   : PK_POLDEK_PKG_AVAILABLE);
	g_autoptr(GDateTime) issued = g_date_time_new_from_unix_utc (pkg->btime);
	g_autofree gchar *time_issued = g_date_time_format (issued, "%Y-%m-%dT%H:%M:%SZ");
	g_autofree gchar *changelog = NULL;
	json_t *updates = json_array ();
	json_t *obsoletes = json_array ();
	struct pkguinf *inf = pkg_uinf (pk_poldek_changelog_pkg (pkg, available));
	uint32_t since = 0;
	json_t *item;

	for (int i = 0; i < n_array_size (installed); i++) {
		const struct pkg *ipkg = n_array_nth (installed, i);
		g_autofree gchar *iid = pk_poldek_pkgid_build (ipkg, PK_POLDEK_PKG_INSTALLED);

		if (pk_poldek_is_update_of (pkg, ipkg)) {
			json_array_append_new (updates, json_string (iid));
			since = MAX (since, ipkg->btime);
		} else if (strcmp (pkg->name, ipkg->name) != 0 && pkg_obsoletes_pkg (pkg, ipkg)) {
			json_array_append_new (obsoletes, json_string (iid));
		}
	}
	/* what changed since the installed version was built */
	if (inf != NULL) {
		changelog = g_strdup (pkguinf_get_changelog (inf, since));
		pkguinf_free (inf);
	}
	item = json_pack ("{s:s, s:o, s:o, s:o, s:s, s:s, s:s}",
			  "package_id",
			  id,
			  "updates",
			  updates,
			  "obsoletes",
			  obsoletes,
			  "issues",
			  pk_poldek_cve_issues (changelog),
			  "restart",
			  pk_restart_enum_to_string (pk_poldek_restart_for (pkg)),
			  "state",
			  pk_update_state_enum_to_string (PK_UPDATE_STATE_ENUM_STABLE),
			  "time_issued",
			  time_issued);
	pk_poldek_json_set_text (item, "update_text", changelog);
	pk_poldek_json_set_text (item, "changelog", changelog);
	return item;
}

void
pk_poldek_get_update_detail (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_auto(GStrv) ids = pk_poldek_require_strv (job, req, "package_ids");
	tn_array *installed;
	tn_array *available;
	GArray *pkgs;

	if (ids == NULL)
		return;
	pkgs = g_array_new (FALSE, FALSE, sizeof (PkPoldekUpdate));
	for (guint i = 0; ids[i] != NULL; i++) {
		GError *error = NULL;
		PkPoldekUpdate found = { NULL, NULL };
		gboolean is_installed = FALSE;

		found.update = pk_poldek_lookup (pc, ids[i], &is_installed, &error);
		if (found.update == NULL) {
			if (error->code == PK_ERROR_ENUM_PACKAGE_NOT_FOUND)
				error->code = PK_ERROR_ENUM_UPDATE_NOT_FOUND;
			pk_poldek_job_take_error (job, error);
			goto out;
		}
		/* marks the ones that are installed themselves */
		found.installed = is_installed ? found.update : NULL;
		g_array_append_val (pkgs, found);
	}
	/* in the order of the indexes: poldek loads file lists out of order slowly */
	g_array_sort (pkgs, pk_poldek_update_cmp);
	installed = pk_poldek_ctx_get_installed (pc);
	available = pk_poldek_ctx_get_available (pc);
	for (guint i = 0; i < pkgs->len; i++) {
		const PkPoldekUpdate *found = &g_array_index (pkgs, PkPoldekUpdate, i);

		pk_poldek_job_queue (job,
				     "update-details",
				     pk_poldek_update_detail (found->update,
							      found->installed != NULL,
							      installed,
							      available));
	}
	n_array_free (installed);
	n_array_free (available);
out:
	for (guint i = 0; i < pkgs->len; i++)
		pkg_free (g_array_index (pkgs, PkPoldekUpdate, i).update);
	g_array_unref (pkgs);
}
