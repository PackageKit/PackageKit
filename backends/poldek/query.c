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

/*
 * The read-only roles. All matching runs in C over the package arrays: no
 * value from a client ever reaches a poclidek command line.
 */

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#include <pk-enum.h>
#include <pk-package-id.h>
#include <rpm/rpmds.h>

#include <poldek/capreq.h>
#include <poldek/pkgcmp.h>
#include <poldek/pkgdir/pkgdir.h>
#include <poldek/pkgfl.h>
#include <poldek/pkgu.h>
#include <poldek/pm/pm.h>
#include <poldek/poldek.h>

#include "filters.h"
#include "groups.h"
#include "pkgid.h"
#include "protocol.h"
#include "query.h"

typedef struct
{
	PkPoldekCtx *pc;
	tn_array *installed;
	tn_array *available;
	/* NEVRA key to installed package */
	GHashTable *installed_nevra;
	/* loaded when needed: see pk_poldek_installed_owners() and pk_poldek_rich_parse() */
	struct pkgdb *db;
	GHashTable *owners;
	GHashTable *rich;
} PkPoldekSets;

typedef gboolean (*PkPoldekMatchFunc) (const struct pkg *pkg, gboolean installed, gpointer data);

static gchar *
pk_poldek_nevra_key_of (const gchar *name,
			gint32 epoch,
			const gchar *version,
			const gchar *release,
			const gchar *arch)
{
	if (epoch > 0)
		return g_strdup_printf ("%s;%d:%s-%s;%s", name, epoch, version, release, arch);
	return g_strdup_printf ("%s;%s-%s;%s", name, version, release, arch);
}

static gchar *
pk_poldek_nevra_key (const struct pkg *pkg)
{
	return pk_poldek_nevra_key_of (pkg->name, pkg->epoch, pkg->ver, pkg->rel, pkg_arch (pkg));
}

static void
pk_poldek_sets_init (PkPoldekSets *sets, PkPoldekCtx *pc)
{
	memset (sets, 0, sizeof (*sets));
	sets->pc = pc;
	sets->installed = pk_poldek_ctx_get_installed (pc);
	sets->available = pk_poldek_ctx_get_available (pc);
	sets->installed_nevra = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (int i = 0; i < n_array_size (sets->installed); i++) {
		struct pkg *pkg = n_array_nth (sets->installed, i);

		g_hash_table_insert (sets->installed_nevra, pk_poldek_nevra_key (pkg), pkg);
	}
}

typedef struct PkPoldekRich PkPoldekRich;

static void pk_poldek_rich_free (PkPoldekRich *node);

static void
pk_poldek_sets_clear (PkPoldekSets *sets)
{
	n_array_free (sets->installed);
	n_array_free (sets->available);
	g_hash_table_unref (sets->installed_nevra);
	if (sets->db != NULL)
		pkgdb_free (sets->db);
	g_clear_pointer (&sets->owners, g_hash_table_unref);
	g_clear_pointer (&sets->rich, g_hash_table_unref);
}

/* whether @pkg is the installed package itself, not an available one of the same NEVRA */
static gboolean
pk_poldek_is_installed (PkPoldekSets *sets, const struct pkg *pkg)
{
	g_autofree gchar *key = pk_poldek_nevra_key (pkg);

	return g_hash_table_lookup (sets->installed_nevra, key) == pkg;
}

/*
 * The installed packages that own the file @path, as the rpm database says:
 * poldek loads installed packages from its cache without their file lists.
 * They are found by NEVRA, the record numbers of what a transaction of ours
 * installed are not known until the packages are loaded again.
 */
static GPtrArray *
pk_poldek_installed_owners (PkPoldekSets *sets, const gchar *path)
{
	GPtrArray *owners;
	struct pkgdb_it it;
	const struct pm_dbrec *rec;

	if (sets->owners == NULL) {
		sets->owners = g_hash_table_new_full (g_str_hash,
						      g_str_equal,
						      g_free,
						      (GDestroyNotify) g_ptr_array_unref);
		sets->db = pkgdb_open (poldek_get_pmctx (sets->pc->ctx),
				       sets->pc->root,
				       NULL,
				       O_RDONLY,
				       NULL);
	}
	if ((owners = g_hash_table_lookup (sets->owners, path)) != NULL)
		return owners;
	owners = g_ptr_array_new ();
	g_hash_table_insert (sets->owners, g_strdup (path), owners);
	if (sets->db == NULL)
		return owners;
	pkgdb_it_init (sets->db, &it, PMTAG_FILE, path);
	while ((rec = pkgdb_it_get (&it)) != NULL) {
		const char *name = NULL;
		const char *version = NULL;
		const char *release = NULL;
		const char *arch = NULL;
		int32_t epoch = 0;
		g_autofree gchar *key = NULL;
		struct pkg *pkg;

		if (!pm_dbrec_nevr (rec, &name, &epoch, &version, &release, &arch, NULL))
			continue;
		key = pk_poldek_nevra_key_of (name, epoch, version, release, arch);
		if ((pkg = g_hash_table_lookup (sets->installed_nevra, key)) != NULL)
			g_ptr_array_add (owners, pkg);
	}
	pkgdb_it_destroy (&it);
	return owners;
}

/*
 * Whether @pkg satisfies @req, a simple dependency. A file one it can also
 * satisfy with what it provides, as for rpm.
 */
static gboolean
pk_poldek_pkg_provides (PkPoldekSets *sets, const struct pkg *pkg, const struct capreq *req)
{
	if (!capreq_is_file (req))
		return pkg_satisfies_req (pkg, req, false);
	if (pkg_caps_match_req (pkg, req, 0))
		return TRUE;
	if (pk_poldek_is_installed (sets, pkg))
		return g_ptr_array_find (pk_poldek_installed_owners (sets, capreq_name (req)),
					 pkg,
					 NULL);
	return pkg_satisfies_req (pkg, req, false);
}

/*
 * A rich dependency as rpm parses it: a simple one (op RPMRICHOP_SINGLE) or
 * a group of operands, all joined by one op: and, or, with, without, or if
 * and unless with a condition second and an else branch third.
 */
struct PkPoldekRich
{
	rpmrichOp op;
	struct capreq *dep;
	GPtrArray *operands;
};

static void
pk_poldek_rich_free (PkPoldekRich *node)
{
	if (node == NULL)
		return;
	if (node->dep != NULL)
		capreq_free (node->dep);
	if (node->operands != NULL)
		g_ptr_array_unref (node->operands);
	g_free (node);
}

static PkPoldekRich *
pk_poldek_rich_group_new (void)
{
	PkPoldekRich *node = g_new0 (PkPoldekRich, 1);

	node->op = RPMRICHOP_SINGLE;
	node->operands = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_poldek_rich_free);
	return node;
}

static rpmRC
pk_poldek_rich_cb (void *cbdata,
		   rpmrichParseType type,
		   const char *n,
		   int nl,
		   const char *e,
		   int el,
		   rpmsenseFlags sense,
		   rpmrichOp op,
		   char **emsg)
{
	GPtrArray *stack = cbdata;
	PkPoldekRich *top = g_ptr_array_index (stack, stack->len - 1);

	switch (type) {
	case RPMRICH_PARSE_ENTER:
		g_ptr_array_add (stack, pk_poldek_rich_group_new ());
		break;
	case RPMRICH_PARSE_LEAVE: {
		PkPoldekRich *group = g_ptr_array_steal_index (stack, stack->len - 1);
		PkPoldekRich *parent = g_ptr_array_index (stack, stack->len - 1);

		/* (a) is a */
		if (group->op == RPMRICHOP_SINGLE && group->operands->len == 1) {
			PkPoldekRich *only = g_ptr_array_steal_index (group->operands, 0);

			pk_poldek_rich_free (group);
			group = only;
		}
		g_ptr_array_add (parent->operands, group);
		break;
	}
	case RPMRICH_PARSE_OP:
		/* the else of if or unless keeps the op */
		if (op != RPMRICHOP_ELSE)
			top->op = op;
		break;
	case RPMRICH_PARSE_SIMPLE: {
		g_autofree gchar *name = g_strndup (n, nl);
		g_autofree gchar *evr = el > 0 ? g_strndup (e, el) : NULL;
		int32_t rel = ((sense & RPMSENSE_LESS) ? REL_LT : 0) |
			      ((sense & RPMSENSE_GREATER) ? REL_GT : 0) |
			      ((sense & RPMSENSE_EQUAL) ? REL_EQ : 0);
		PkPoldekRich *leaf = g_new0 (PkPoldekRich, 1);

		leaf->op = RPMRICHOP_SINGLE;
		leaf->dep = evr != NULL && rel != 0 ? capreq_new_evr (NULL, name, evr, rel, 0)
						    : capreq_new (NULL, name, 0, NULL, NULL, 0, 0);
		if (leaf->dep == NULL) {
			g_free (leaf);
			return RPMRC_FAIL;
		}
		g_ptr_array_add (top->operands, leaf);
		break;
	}
	}
	return RPMRC_OK;
}

/* The parsed rich dependency @req, such as (a and b); NULL for a simple one or one that does not parse. */
static PkPoldekRich *
pk_poldek_rich_parse (PkPoldekSets *sets, const struct capreq *req)
{
	const gchar *expr = capreq_name (req);
	g_autoptr(GPtrArray) stack = NULL;
	PkPoldekRich *root = NULL;
	gpointer found;

	if (expr[0] != '(')
		return NULL;
	if (sets->rich == NULL)
		sets->rich = g_hash_table_new_full (g_str_hash,
						    g_str_equal,
						    g_free,
						    (GDestroyNotify) pk_poldek_rich_free);
	if (g_hash_table_lookup_extended (sets->rich, expr, NULL, &found))
		return found;
	stack = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_poldek_rich_free);
	g_ptr_array_add (stack, pk_poldek_rich_group_new ());
	if (rpmrichParse (&expr, NULL, pk_poldek_rich_cb, stack) == RPMRC_OK && stack->len == 1) {
		PkPoldekRich *top = g_ptr_array_index (stack, 0);

		if (top->operands->len == 1)
			root = g_ptr_array_steal_index (top->operands, 0);
	}
	g_hash_table_insert (sets->rich, g_strdup (capreq_name (req)), root);
	return root;
}

/* text from package headers is not always valid UTF-8, JSON strings must be */
json_t *
pk_poldek_json_text (const gchar *text)
{
	g_autofree gchar *valid = g_utf8_make_valid (text, -1);

	return json_string (valid);
}

void
pk_poldek_json_set_text (json_t *obj, const gchar *key, const gchar *text)
{
	if (text != NULL)
		json_object_set_new (obj, key, pk_poldek_json_text (text));
}

/* name, newest version first, arch, repository: the order results are sent in */
gint
pk_poldek_pkg_cmp (gconstpointer a, gconstpointer b)
{
	const struct pkg *p1 = *(const struct pkg *const *) a;
	const struct pkg *p2 = *(const struct pkg *const *) b;
	gint rc;

	rc = strcmp (p1->name, p2->name);
	if (rc == 0)
		rc = -pkg_cmp_evr (p1, p2);
	if (rc == 0)
		rc = g_strcmp0 (pkg_arch (p1), pkg_arch (p2));
	if (rc == 0 && p1->pkgdir != NULL && p2->pkgdir != NULL)
		rc = g_strcmp0 (p1->pkgdir->name, p2->pkgdir->name);
	return rc;
}

/* Queues @pkg for a packages event, as @info. */
void
pk_poldek_queue_package (PkPoldekJob *job,
			 const struct pkg *pkg,
			 gboolean installed,
			 PkInfoEnum info)
{
	pk_poldek_queue_update (job, pkg, installed, info, PK_SEVERITY_ENUM_NONE);
}

/* As pk_poldek_queue_package(), with the severity of an update unless none. */
void
pk_poldek_queue_update (PkPoldekJob *job,
			const struct pkg *pkg,
			gboolean installed,
			PkInfoEnum info,
			PkSeverityEnum severity)
{
	g_autofree gchar *id = pk_poldek_pkgid_build (pkg,
						      installed ? PK_POLDEK_PKG_INSTALLED
								: PK_POLDEK_PKG_AVAILABLE);
	struct pkguinf *inf = pkg_uinf (pkg);
	json_t *item = json_pack ("{s:s, s:s}",
				  "package_id",
				  id,
				  "info",
				  pk_info_enum_to_string (info));

	if (inf != NULL) {
		pk_poldek_json_set_text (item, "summary", pkguinf_get (inf, PKGUINF_SUMMARY));
		pkguinf_free (inf);
	}
	if (severity != PK_SEVERITY_ENUM_NONE)
		json_object_set_new (item,
				     "severity",
				     json_string (pk_severity_enum_to_string (severity)));
	pk_poldek_job_queue (job, "packages", item);
}

/* Drops what @filters reject, then duplicates, and sorts what is left. */
static void
pk_poldek_apply_filters (GPtrArray *pkgs, PkBitfield filters)
{
	g_autoptr(GHashTable) seen = g_hash_table_new (NULL, NULL);
	guint kept = 0;

	for (guint i = 0; i < pkgs->len; i++) {
		struct pkg *pkg = g_ptr_array_index (pkgs, i);

		if (pk_poldek_filters_accept (filters, pkg) && g_hash_table_add (seen, pkg))
			pkgs->pdata[kept++] = pkg;
	}
	g_ptr_array_set_size (pkgs, kept);
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NEWEST))
		pk_poldek_filter_newest (pkgs, TRUE);
	else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_NEWEST))
		pk_poldek_filter_newest (pkgs, FALSE);
	g_ptr_array_sort (pkgs, pk_poldek_pkg_cmp);
}

/*
 * Sends the installed and available packages found by a query, after the
 * filters. An available package that is installed is only sent as installed;
 * newest is applied before that, so an older available version does not
 * stand in for the installed newest one.
 */
static void
pk_poldek_emit_hits (PkPoldekJob *job,
		     PkPoldekSets *sets,
		     PkBitfield filters,
		     GPtrArray *installed,
		     GPtrArray *available)
{
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED))
		g_ptr_array_set_size (installed, 0);
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED))
		g_ptr_array_set_size (available, 0);
	pk_poldek_apply_filters (installed, filters);
	pk_poldek_apply_filters (available, filters);

	for (guint i = 0; i < installed->len; i++)
		pk_poldek_queue_package (job,
					 g_ptr_array_index (installed, i),
					 TRUE,
					 PK_INFO_ENUM_INSTALLED);
	for (guint i = 0; i < available->len; i++) {
		const struct pkg *pkg = g_ptr_array_index (available, i);
		g_autofree gchar *key = pk_poldek_nevra_key (pkg);

		if (!g_hash_table_contains (sets->installed_nevra, key))
			pk_poldek_queue_package (job, pkg, FALSE, PK_INFO_ENUM_AVAILABLE);
	}
}

static void
pk_poldek_scan (PkPoldekSets *sets,
		PkBitfield filters,
		PkPoldekMatchFunc match,
		gpointer data,
		GPtrArray *installed,
		GPtrArray *available)
{
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		for (int i = 0; i < n_array_size (sets->installed); i++) {
			struct pkg *pkg = n_array_nth (sets->installed, i);

			if (match (pkg, TRUE, data))
				g_ptr_array_add (installed, pkg);
		}
	}
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		for (int i = 0; i < n_array_size (sets->available); i++) {
			struct pkg *pkg = n_array_nth (sets->available, i);

			if (match (pkg, FALSE, data))
				g_ptr_array_add (available, pkg);
		}
	}
}

/* Runs a query that matches one package at a time. */
static void
pk_poldek_query (PkPoldekCtx *pc,
		 PkPoldekJob *job,
		 json_t *req,
		 PkPoldekMatchFunc match,
		 gpointer data)
{
	PkBitfield filters = pk_poldek_request_get_filters (req);
	g_autoptr(GPtrArray) installed = g_ptr_array_new ();
	g_autoptr(GPtrArray) available = g_ptr_array_new ();
	PkPoldekSets sets;

	pk_poldek_sets_init (&sets, pc);
	pk_poldek_scan (&sets, filters, match, data, installed, available);
	pk_poldek_emit_hits (job, &sets, filters, installed, available);
	pk_poldek_sets_clear (&sets);
}

/* The array @key of @req; a missing one fails the job, the daemon always sends it. */
gchar **
pk_poldek_require_strv (PkPoldekJob *job, json_t *req, const gchar *key)
{
	gchar **strv = pk_poldek_request_get_strv (req, key);

	if (strv == NULL)
		pk_poldek_job_error (job,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "invalid request: no array of strings '%s'",
				     key);
	return strv;
}

/* The words of all search values, lower case: a package has to match every one of them. */
static gchar **
pk_poldek_search_words (gchar **values)
{
	g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();

	for (guint i = 0; values[i] != NULL; i++) {
		g_auto(GStrv) words = g_strsplit_set (values[i], " \t\n", -1);

		for (guint j = 0; words[j] != NULL; j++) {
			if (words[j][0] != '\0')
				g_strv_builder_take (builder, g_utf8_strdown (words[j], -1));
		}
	}
	return g_strv_builder_end (builder);
}

static gboolean
pk_poldek_contains_all (const gchar *text, gchar **words)
{
	g_autofree gchar *lower = g_utf8_strdown (text, -1);

	for (guint i = 0; words[i] != NULL; i++) {
		if (strstr (lower, words[i]) == NULL)
			return FALSE;
	}
	return TRUE;
}

static gboolean
pk_poldek_match_name (const struct pkg *pkg, gboolean installed, gpointer data)
{
	return pk_poldek_contains_all (pkg->name, data);
}

static gboolean
pk_poldek_match_details (const struct pkg *pkg, gboolean installed, gpointer data)
{
	g_autoptr(GString) text = g_string_new (pkg->name);
	struct pkguinf *inf = pkg_uinf (pkg);
	const gchar *group = pkg_group (pkg);

	if (group != NULL)
		g_string_append_printf (text, "\n%s", group);
	if (inf != NULL) {
		const int tags[] = { PKGUINF_SUMMARY,
				     PKGUINF_DESCRIPTION,
				     PKGUINF_LICENSE,
				     PKGUINF_URL };

		for (guint i = 0; i < G_N_ELEMENTS (tags); i++) {
			const gchar *value = pkguinf_get (inf, tags[i]);

			if (value != NULL)
				g_string_append_printf (text, "\n%s", value);
		}
		pkguinf_free (inf);
	}
	return pk_poldek_contains_all (text->str, data);
}

/* A search for words of @req's values; values of blanks only have none and match nothing. */
static void
pk_poldek_search_words_query (PkPoldekCtx *pc,
			      PkPoldekJob *job,
			      json_t *req,
			      PkPoldekMatchFunc match)
{
	g_auto(GStrv) values = pk_poldek_require_strv (job, req, "values");
	g_auto(GStrv) words = NULL;

	if (values == NULL)
		return;
	words = pk_poldek_search_words (values);
	if (words[0] != NULL)
		pk_poldek_query (pc, job, req, match, words);
}

void
pk_poldek_search_name (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	pk_poldek_search_words_query (pc, job, req, pk_poldek_match_name);
}

void
pk_poldek_search_details (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	pk_poldek_search_words_query (pc, job, req, pk_poldek_match_details);
}

typedef struct
{
	PkBitfield groups;
	gchar **repos;
} PkPoldekGroupSearch;

static gboolean
pk_poldek_match_group (const struct pkg *pkg, gboolean installed, gpointer data)
{
	PkPoldekGroupSearch *search = data;
	g_autofree gchar *origin = NULL;

	if (pk_bitfield_contain (search->groups, pk_poldek_group_from_rpm (pkg_group (pkg))))
		return TRUE;
	/* installed packages do not record the repository they came from */
	if (installed || pkg->pkgdir == NULL)
		return FALSE;
	/* the repository as get-repo-list and package ids name it */
	origin = pk_poldek_pkg_origin (pkg);
	return g_strv_contains ((const gchar *const *) search->repos, origin);
}

/* Values are group names, or repo:NAME for the packages of a repository. */
void
pk_poldek_search_group (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_auto(GStrv) values = pk_poldek_require_strv (job, req, "values");
	g_autoptr(GStrvBuilder) repos = g_strv_builder_new ();
	PkPoldekGroupSearch search = { 0 };

	if (values == NULL)
		return;
	for (guint i = 0; values[i] != NULL; i++) {
		if (g_str_has_prefix (values[i], "repo:"))
			g_strv_builder_add (repos, values[i] + strlen ("repo:"));
		else
			pk_bitfield_add (search.groups, pk_group_enum_from_string (values[i]));
	}
	/* never matches a package, its group is mapped to other */
	pk_bitfield_remove (search.groups, PK_GROUP_ENUM_UNKNOWN);
	search.repos = g_strv_builder_end (repos);
	pk_poldek_query (pc, job, req, pk_poldek_match_group, &search);
	g_strfreev (search.repos);
}

/* capreqs for @values, which name capabilities or, starting with '/', files */
static tn_array *
pk_poldek_capreqs_new (gchar **values)
{
	tn_array *reqs = capreq_arr_new (4);

	for (guint i = 0; values[i] != NULL; i++) {
		struct capreq *req = capreq_new (NULL, values[i], 0, NULL, NULL, 0, 0);

		if (req != NULL)
			n_array_push (reqs, req);
	}
	return reqs;
}

/*
 * Whether @pkg has the file @path. poldek keeps only the files of directories
 * something requires in a package's capabilities; the full list it loads when
 * asked, as its own search does.
 */
static gboolean
pk_poldek_pkg_has_file (const struct pkg *pkg, const gchar *path)
{
	struct pkgflist_it *it = pkg_get_flist_it (pkg);
	const gchar *file;
	gboolean found = FALSE;

	while (it != NULL && !found && (file = pkgflist_it_get (it, NULL)) != NULL)
		found = strcmp (file, path) == 0;
	if (it != NULL)
		pkgflist_it_free (it);
	return found;
}

static gboolean
pk_poldek_match_provides (const struct pkg *pkg, gboolean installed, gpointer data)
{
	tn_array *reqs = data;

	for (int i = 0; i < n_array_size (reqs); i++) {
		const struct capreq *req = n_array_nth (reqs, i);

		if (pkg_satisfies_req (pkg, req, false) ||
		    (capreq_is_file (req) && pk_poldek_pkg_has_file (pkg, capreq_name (req))))
			return TRUE;
	}
	return FALSE;
}

void
pk_poldek_what_provides (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_auto(GStrv) values = pk_poldek_require_strv (job, req, "values");
	tn_array *reqs;

	if (values == NULL)
		return;
	reqs = pk_poldek_capreqs_new (values);
	pk_poldek_query (pc, job, req, pk_poldek_match_provides, reqs);
	n_array_free (reqs);
}

typedef struct
{
	gchar **paths;
	gchar **basenames;
} PkPoldekFileSearch;

static gboolean
pk_poldek_match_file (const struct pkg *pkg, gboolean installed, gpointer data)
{
	PkPoldekFileSearch *search = data;
	struct pkgflist_it *it = pkg_get_flist_it (pkg);
	const gchar *path;
	gboolean found = FALSE;

	while (it != NULL && !found && (path = pkgflist_it_get (it, NULL)) != NULL) {
		const gchar *base = strrchr (path, '/');

		found = g_strv_contains ((const gchar *const *) search->paths, path) ||
			g_strv_contains ((const gchar *const *) search->basenames,
					 base != NULL ? base + 1 : path);
	}
	if (it != NULL)
		pkgflist_it_free (it);
	return found;
}

/* A value starting with '/' is a full path, anything else a file name in any directory. */
void
pk_poldek_search_file (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_auto(GStrv) values = pk_poldek_require_strv (job, req, "values");
	g_autoptr(GStrvBuilder) paths = g_strv_builder_new ();
	g_autoptr(GStrvBuilder) basenames = g_strv_builder_new ();
	PkPoldekFileSearch search;

	if (values == NULL)
		return;
	for (guint i = 0; values[i] != NULL; i++) {
		if (values[i][0] == '/')
			g_strv_builder_add (paths, values[i]);
		else if (values[i][0] != '\0')
			g_strv_builder_add (basenames, values[i]);
	}
	search.paths = g_strv_builder_end (paths);
	search.basenames = g_strv_builder_end (basenames);
	/* nothing to look for: no file list needs loading */
	if (search.paths[0] != NULL || search.basenames[0] != NULL)
		pk_poldek_query (pc, job, req, pk_poldek_match_file, &search);
	g_strfreev (search.paths);
	g_strfreev (search.basenames);
}

static gboolean
pk_poldek_match_any (const struct pkg *pkg, gboolean installed, gpointer data)
{
	return TRUE;
}

void
pk_poldek_get_packages (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	pk_poldek_query (pc, job, req, pk_poldek_match_any, NULL);
}

static gboolean
pk_poldek_match_names (const struct pkg *pkg, gboolean installed, gpointer data)
{
	return g_hash_table_contains (data, pkg->name);
}

/*
 * Package ids are looked up exactly, anything else is an exact, case
 * sensitive package name.
 */
void
pk_poldek_resolve (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_auto(GStrv) values = pk_poldek_require_strv (job, req, "values");
	PkBitfield filters = pk_poldek_request_get_filters (req);
	g_autoptr(GHashTable) names = g_hash_table_new (g_str_hash, g_str_equal);
	g_autoptr(GPtrArray) installed = g_ptr_array_new ();
	g_autoptr(GPtrArray) available = g_ptr_array_new ();
	g_autoptr(GPtrArray) found = g_ptr_array_new_with_free_func ((GDestroyNotify) pkg_free);
	PkPoldekSets sets;

	if (values == NULL)
		return;
	for (guint i = 0; values[i] != NULL; i++) {
		g_autoptr(GError) error = NULL;
		struct pkg *pkg;
		gboolean is_installed;

		if (!pk_package_id_check (values[i])) {
			g_hash_table_add (names, values[i]);
			continue;
		}
		pkg = pk_poldek_lookup (pc, values[i], &is_installed, &error);
		if (pkg == NULL)
			continue;
		g_ptr_array_add (found, pkg);
		g_ptr_array_add (is_installed ? installed : available, pkg);
	}

	pk_poldek_sets_init (&sets, pc);
	pk_poldek_scan (&sets, filters, pk_poldek_match_names, names, installed, available);
	pk_poldek_emit_hits (job, &sets, filters, installed, available);
	pk_poldek_sets_clear (&sets);
}

/*
 * Finds the package @package_id names. Returns a new reference, free it with
 * pkg_free().
 */
struct pkg *
pk_poldek_lookup (PkPoldekCtx *pc, const gchar *package_id, gboolean *installed, GError **error)
{
	PkPoldekPkgId id = { 0 };
	struct pkg *found = NULL;
	tn_array *pkgs = NULL;

	if (!pk_poldek_pkgid_parse (package_id, &id, error))
		return NULL;
	switch (id.kind) {
	case PK_POLDEK_PKG_INSTALLED:
		pkgs = pk_poldek_ctx_get_installed (pc);
		break;
	case PK_POLDEK_PKG_AVAILABLE:
		pkgs = poldek_search_avail_packages (pc->ctx,
						     POLDEK_ST_NAME,
						     id.split[PK_PACKAGE_ID_NAME]);
		break;
	}
	for (int i = 0; pkgs != NULL && found == NULL && i < n_array_size (pkgs); i++) {
		struct pkg *pkg = n_array_nth (pkgs, i);

		if (!pk_poldek_pkgid_matches (&id, pkg))
			continue;
		/* only the copy poldek kept of a NEVRA in several repositories can be installed */
		if (id.kind == PK_POLDEK_PKG_AVAILABLE) {
			g_autofree gchar *origin = pk_poldek_pkg_origin (pkg);

			if (strcmp (origin, id.split[PK_PACKAGE_ID_ORIGIN]) != 0)
				continue;
		}
		found = pkg_link (pkg);
	}
	if (pkgs != NULL)
		n_array_free (pkgs);
	if (found == NULL)
		g_set_error (error,
			     PK_POLDEK_ERROR,
			     PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
			     "package %s not found",
			     package_id);
	else
		*installed = id.kind == PK_POLDEK_PKG_INSTALLED;
	pk_poldek_pkgid_clear (&id);
	return found;
}

/* Looks up all "package_ids" of @req, or fails the job. */
static GPtrArray *
pk_poldek_lookup_request (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req, GArray **installed)
{
	g_auto(GStrv) ids = pk_poldek_require_strv (job, req, "package_ids");
	g_autoptr(GPtrArray) pkgs = g_ptr_array_new_with_free_func ((GDestroyNotify) pkg_free);
	g_autoptr(GArray) kinds = g_array_new (FALSE, FALSE, sizeof (gboolean));

	if (ids == NULL)
		return NULL;
	for (guint i = 0; ids[i] != NULL; i++) {
		GError *error = NULL;
		gboolean is_installed = FALSE;
		struct pkg *pkg = pk_poldek_lookup (pc, ids[i], &is_installed, &error);

		if (pkg == NULL) {
			pk_poldek_job_take_error (job, error);
			return NULL;
		}
		g_ptr_array_add (pkgs, pkg);
		g_array_append_val (kinds, is_installed);
	}
	*installed = g_steal_pointer (&kinds);
	return g_steal_pointer (&pkgs);
}

void
pk_poldek_get_details (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_autoptr(GArray) installed = NULL;
	g_autoptr(GPtrArray) pkgs = pk_poldek_lookup_request (pc, job, req, &installed);

	for (guint i = 0; pkgs != NULL && i < pkgs->len; i++) {
		const struct pkg *pkg = g_ptr_array_index (pkgs, i);
		gboolean is_installed = g_array_index (installed, gboolean, i);
		g_autofree gchar *id = pk_poldek_pkgid_build (
		    pkg,
		    is_installed ? PK_POLDEK_PKG_INSTALLED : PK_POLDEK_PKG_AVAILABLE);
		PkGroupEnum group = pk_poldek_group_from_rpm (pkg_group (pkg));
		struct pkguinf *inf = pkg_uinf (pkg);
		json_t *item = json_pack ("{s:s, s:s, s:I}",
					  "package_id",
					  id,
					  "group",
					  pk_group_enum_to_string (group),
					  "size",
					  (json_int_t) pkg->size);

		/* the download size only means something for a package that is not installed */
		if (!is_installed)
			json_object_set_new (item, "download_size", json_integer (pkg->fsize));
		if (inf != NULL) {
			pk_poldek_json_set_text (item,
						 "summary",
						 pkguinf_get (inf, PKGUINF_SUMMARY));
			pk_poldek_json_set_text (item,
						 "description",
						 pkguinf_get (inf, PKGUINF_DESCRIPTION));
			pk_poldek_json_set_text (item,
						 "license",
						 pkguinf_get (inf, PKGUINF_LICENSE));
			pk_poldek_json_set_text (item, "url", pkguinf_get (inf, PKGUINF_URL));
			pkguinf_free (inf);
		}
		pk_poldek_job_queue (job, "details", item);
	}
}

/* Adds @path of a package, with what kind of file @mode says, to a files event's @files. */
void
pk_poldek_files_add (json_t *files, const gchar *path, mode_t mode)
{
	PkFileKindEnum kind = PK_FILE_KIND_ENUM_UNKNOWN;
	/* package headers name files in whatever encoding they have */
	g_autofree gchar *valid = g_utf8_make_valid (path, -1);

	if (S_ISDIR (mode))
		kind = PK_FILE_KIND_ENUM_DIRECTORY;
	else if (S_ISLNK (mode))
		kind = PK_FILE_KIND_ENUM_SYMLINK;
	else if (S_ISREG (mode))
		kind = PK_FILE_KIND_ENUM_REGULAR;
	json_object_set_new (files, valid, json_string (pk_file_kind_enum_to_string (kind)));
}

void
pk_poldek_get_files (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_autoptr(GArray) installed = NULL;
	g_autoptr(GPtrArray) pkgs = pk_poldek_lookup_request (pc, job, req, &installed);

	for (guint i = 0; pkgs != NULL && i < pkgs->len; i++) {
		const struct pkg *pkg = g_ptr_array_index (pkgs, i);
		g_autofree gchar *id = pk_poldek_pkgid_build (pkg,
							      g_array_index (installed, gboolean, i)
								  ? PK_POLDEK_PKG_INSTALLED
								  : PK_POLDEK_PKG_AVAILABLE);
		struct pkgflist_it *it = pkg_get_flist_it (pkg);
		json_t *files = json_object ();
		struct flfile *flfile = NULL;
		const gchar *path;

		while (it != NULL && (path = pkgflist_it_get (it, &flfile)) != NULL)
			pk_poldek_files_add (files, path, flfile != NULL ? flfile->mode : 0);
		if (it != NULL)
			pkgflist_it_free (it);
		pk_poldek_job_emit (job,
				    "files",
				    json_pack ("{s:s, s:o}", "package_id", id, "files", files));
	}
}

/* what rpm itself provides, not a package */
static gboolean
pk_poldek_req_is_rpmlib (const struct capreq *req)
{
	return capreq_is_rpmlib (req) || g_str_has_prefix (capreq_name (req), "rpmlib(");
}

static gboolean
pk_poldek_cancelled (PkPoldekJob *job)
{
	if (!pk_poldek_job_is_cancelled (job))
		return FALSE;
	pk_poldek_job_error (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Cancelled");
	return TRUE;
}

/* The installed and the available packages a dependency is met by. */
typedef struct
{
	GPtrArray *installed;
	GPtrArray *available;
} PkPoldekProviders;

static void
pk_poldek_providers_init (PkPoldekProviders *found)
{
	found->installed = g_ptr_array_new ();
	found->available = g_ptr_array_new ();
}

static void
pk_poldek_providers_clear (PkPoldekProviders *found)
{
	g_ptr_array_unref (found->installed);
	g_ptr_array_unref (found->available);
}

static void
pk_poldek_add_unique (GPtrArray *to, gpointer pkg)
{
	if (!g_ptr_array_find (to, pkg, NULL))
		g_ptr_array_add (to, pkg);
}

/*
 * Adds the providers of a simple dependency @dep of @requirer to @found:
 * the installed ones, and the available ones if no installed one meets it
 * or with @all. Whether @requirer meets it itself is for the caller to
 * judge: a part of with it meets still needs a package that meets all.
 */
static void
pk_poldek_dep_providers (PkPoldekSets *sets,
			 const struct pkg *requirer,
			 const struct capreq *dep,
			 gboolean all,
			 PkPoldekProviders *found)
{
	tn_array *candidates;
	gboolean installed = FALSE;

	if (pk_poldek_req_is_rpmlib (dep))
		return;
	if (capreq_is_file (dep)) {
		GPtrArray *owners = pk_poldek_installed_owners (sets, capreq_name (dep));

		for (guint i = 0; i < owners->len; i++) {
			pk_poldek_add_unique (found->installed, g_ptr_array_index (owners, i));
			installed = TRUE;
		}
	}
	for (int i = 0; i < n_array_size (sets->installed); i++) {
		struct pkg *provider = n_array_nth (sets->installed, i);

		/* a file one by what the package provides, the owners are there already */
		if (provider != requirer &&
		    (capreq_is_file (dep) ? pkg_caps_match_req (provider, dep, 0)
					  : pkg_satisfies_req (provider, dep, false))) {
			pk_poldek_add_unique (found->installed, provider);
			installed = TRUE;
		}
	}
	/* a dependency an installed package meets does not need an available one */
	if (installed && !all)
		return;
	candidates = poldek_search_avail_packages (sets->pc->ctx,
						   POLDEK_ST_PROVIDES,
						   capreq_name (dep));
	for (int i = 0; candidates != NULL && i < n_array_size (candidates); i++) {
		struct pkg *provider = n_array_nth (candidates, i);

		/* the pkgset holds a reference for as long as sets->available does */
		if (provider != requirer && pk_poldek_pkg_provides (sets, provider, dep))
			pk_poldek_add_unique (found->available, provider);
	}
	if (candidates != NULL)
		n_array_free (candidates);
}

/*
 * A rich dependency of @requirer is judged against what is installed and
 * what @requirer provides itself, as rpm does: they decide the branch of if
 * and unless, which alternative of or is met, and whether anything is
 * needed at all.
 */
static gboolean
pk_poldek_rich_installed (PkPoldekSets *sets, const struct pkg *requirer, const PkPoldekRich *node);

/* The branch of if or unless that applies; NULL for none. */
static const PkPoldekRich *
pk_poldek_rich_branch (PkPoldekSets *sets, const struct pkg *requirer, const PkPoldekRich *node)
{
	gboolean met = pk_poldek_rich_installed (sets,
						 requirer,
						 g_ptr_array_index (node->operands, 1));

	if (met == (node->op == RPMRICHOP_IF))
		return g_ptr_array_index (node->operands, 0);
	return node->operands->len > 2 ? g_ptr_array_index (node->operands, 2) : NULL;
}

/* Whether the one package @pkg meets @node, a rich dependency of @requirer. */
static gboolean
pk_poldek_rich_met_by (PkPoldekSets *sets,
		       const struct pkg *requirer,
		       const struct pkg *pkg,
		       const PkPoldekRich *node)
{
	const PkPoldekRich *branch;

	switch (node->op) {
	case RPMRICHOP_SINGLE:
		return pk_poldek_req_is_rpmlib (node->dep) ||
		       pk_poldek_pkg_provides (sets, pkg, node->dep);
	case RPMRICHOP_OR:
		for (guint i = 0; i < node->operands->len; i++) {
			if (pk_poldek_rich_met_by (sets,
						   requirer,
						   pkg,
						   g_ptr_array_index (node->operands, i)))
				return TRUE;
		}
		return FALSE;
	case RPMRICHOP_WITHOUT:
		return pk_poldek_rich_met_by (sets,
					      requirer,
					      pkg,
					      g_ptr_array_index (node->operands, 0)) &&
		       !pk_poldek_rich_met_by (sets,
					       requirer,
					       pkg,
					       g_ptr_array_index (node->operands, 1));
	case RPMRICHOP_IF:
	case RPMRICHOP_UNLESS:
		branch = pk_poldek_rich_branch (sets, requirer, node);
		return branch == NULL || pk_poldek_rich_met_by (sets, requirer, pkg, branch);
	default:
		/* and, with */
		for (guint i = 0; i < node->operands->len; i++) {
			if (!pk_poldek_rich_met_by (sets,
						    requirer,
						    pkg,
						    g_ptr_array_index (node->operands, i)))
				return FALSE;
		}
		return TRUE;
	}
}

/* Whether the installed packages or @requirer meet @node. */
static gboolean
pk_poldek_rich_installed (PkPoldekSets *sets, const struct pkg *requirer, const PkPoldekRich *node)
{
	const PkPoldekRich *branch;

	switch (node->op) {
	case RPMRICHOP_AND:
	case RPMRICHOP_OR:
		for (guint i = 0; i < node->operands->len; i++) {
			gboolean met = pk_poldek_rich_installed (
			    sets,
			    requirer,
			    g_ptr_array_index (node->operands, i));

			if (met == (node->op == RPMRICHOP_OR))
				return met;
		}
		return node->op == RPMRICHOP_AND;
	case RPMRICHOP_IF:
	case RPMRICHOP_UNLESS:
		branch = pk_poldek_rich_branch (sets, requirer, node);
		return branch == NULL || pk_poldek_rich_installed (sets, requirer, branch);
	default:
		/* simple, with, without: one package meets it */
		if (pk_poldek_rich_met_by (sets, requirer, requirer, node))
			return TRUE;
		for (int i = 0; i < n_array_size (sets->installed); i++) {
			if (pk_poldek_rich_met_by (sets,
						   requirer,
						   n_array_nth (sets->installed, i),
						   node))
				return TRUE;
		}
		return FALSE;
	}
}

/* Calls @func for each simple dependency @node can need, its conditions and exclusions left out. */
static void
pk_poldek_rich_deps (const PkPoldekRich *node,
		     void (*func) (const struct capreq *dep, gpointer data),
		     gpointer data)
{
	if (node->op == RPMRICHOP_SINGLE) {
		func (node->dep, data);
		return;
	}
	for (guint i = 0; i < node->operands->len; i++) {
		gboolean condition = i == 1 &&
				     (node->op == RPMRICHOP_IF || node->op == RPMRICHOP_UNLESS ||
				      node->op == RPMRICHOP_WITHOUT);

		if (!condition)
			pk_poldek_rich_deps (g_ptr_array_index (node->operands, i), func, data);
	}
}

typedef struct
{
	PkPoldekSets *sets;
	const struct pkg *requirer;
	PkPoldekProviders *found;
} PkPoldekCandidates;

static void
pk_poldek_add_candidates (const struct capreq *dep, gpointer data)
{
	PkPoldekCandidates *add = data;

	pk_poldek_dep_providers (add->sets, add->requirer, dep, TRUE, add->found);
}

/*
 * Adds the providers of @node, a rich dependency of @requirer, with rpm's
 * meaning: all of and, the alternatives of or met by what is installed if
 * there are any, packages that meet all of with or the first but not the
 * second of without, the branch of if or unless that applies. Installed ones
 * again make available ones needless.
 */
static void
pk_poldek_rich_providers (PkPoldekSets *sets,
			  const struct pkg *requirer,
			  const PkPoldekRich *node,
			  PkPoldekProviders *found)
{
	const PkPoldekRich *branch;
	PkPoldekProviders part;
	gboolean installed = FALSE;

	if (pk_poldek_rich_met_by (sets, requirer, requirer, node))
		return;
	switch (node->op) {
	case RPMRICHOP_SINGLE:
		pk_poldek_dep_providers (sets, requirer, node->dep, FALSE, found);
		break;
	case RPMRICHOP_AND:
		for (guint i = 0; i < node->operands->len; i++)
			pk_poldek_rich_providers (sets,
						  requirer,
						  g_ptr_array_index (node->operands, i),
						  found);
		break;
	case RPMRICHOP_OR:
		installed = pk_poldek_rich_installed (sets, requirer, node);
		for (guint i = 0; i < node->operands->len; i++) {
			const PkPoldekRich *operand = g_ptr_array_index (node->operands, i);

			if (!installed || pk_poldek_rich_installed (sets, requirer, operand))
				pk_poldek_rich_providers (sets, requirer, operand, found);
		}
		break;
	case RPMRICHOP_WITH:
	case RPMRICHOP_WITHOUT: {
		PkPoldekCandidates add = { sets, requirer, &part };

		pk_poldek_providers_init (&part);
		pk_poldek_rich_deps (g_ptr_array_index (node->operands, 0),
				     pk_poldek_add_candidates,
				     &add);
		for (guint i = 0; i < part.installed->len; i++) {
			gpointer pkg = g_ptr_array_index (part.installed, i);

			if (pk_poldek_rich_met_by (sets, requirer, pkg, node)) {
				pk_poldek_add_unique (found->installed, pkg);
				installed = TRUE;
			}
		}
		for (guint i = 0; !installed && i < part.available->len; i++) {
			gpointer pkg = g_ptr_array_index (part.available, i);

			if (pk_poldek_rich_met_by (sets, requirer, pkg, node))
				pk_poldek_add_unique (found->available, pkg);
		}
		pk_poldek_providers_clear (&part);
		break;
	}
	case RPMRICHOP_IF:
	case RPMRICHOP_UNLESS:
		if ((branch = pk_poldek_rich_branch (sets, requirer, node)) != NULL)
			pk_poldek_rich_providers (sets, requirer, branch, found);
		break;
	default:
		break;
	}
}

/*
 * Whether @target is one of what @node, a rich dependency of @requirer,
 * needs: it meets a part of and, an alternative of or (one met by what is
 * installed if there is one), all of with, the first but not the second of
 * without, or the branch of if or unless that applies, where @requirer does
 * not meet that part itself.
 */
static gboolean
pk_poldek_rich_needs (PkPoldekSets *sets,
		      const struct pkg *requirer,
		      const struct pkg *target,
		      const PkPoldekRich *node)
{
	const PkPoldekRich *branch;
	gboolean installed;

	if (pk_poldek_rich_met_by (sets, requirer, requirer, node))
		return FALSE;
	switch (node->op) {
	case RPMRICHOP_SINGLE:
	case RPMRICHOP_WITH:
	case RPMRICHOP_WITHOUT:
		return pk_poldek_rich_met_by (sets, requirer, target, node);
	case RPMRICHOP_IF:
	case RPMRICHOP_UNLESS:
		branch = pk_poldek_rich_branch (sets, requirer, node);
		return branch != NULL && pk_poldek_rich_needs (sets, requirer, target, branch);
	case RPMRICHOP_OR:
		installed = pk_poldek_rich_installed (sets, requirer, node);
		for (guint i = 0; i < node->operands->len; i++) {
			const PkPoldekRich *operand = g_ptr_array_index (node->operands, i);

			if ((!installed || pk_poldek_rich_installed (sets, requirer, operand)) &&
			    pk_poldek_rich_needs (sets, requirer, target, operand))
				return TRUE;
		}
		return FALSE;
	default:
		for (guint i = 0; i < node->operands->len; i++) {
			if (pk_poldek_rich_needs (sets,
						  requirer,
						  target,
						  g_ptr_array_index (node->operands, i)))
				return TRUE;
		}
		return FALSE;
	}
}

/* The providers of what @pkg requires, rich dependencies included. */
static void
pk_poldek_pkg_providers (PkPoldekSets *sets, const struct pkg *pkg, PkPoldekProviders *found)
{
	for (int i = 0; pkg->reqs != NULL && i < n_array_size (pkg->reqs); i++) {
		const struct capreq *req = n_array_nth (pkg->reqs, i);
		PkPoldekRich *rich = pk_poldek_rich_parse (sets, req);

		if (rich != NULL)
			pk_poldek_rich_providers (sets, pkg, rich, found);
		else if (capreq_name (req)[0] != '(' && !pk_poldek_pkg_provides (sets, pkg, req))
			pk_poldek_dep_providers (sets, pkg, req, FALSE, found);
	}
}

void
pk_poldek_get_dependencies (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	PkBitfield filters = pk_poldek_request_get_filters (req);
	gboolean recursive = pk_poldek_request_get_bool (req, "recursive");
	g_autoptr(GArray) kinds = NULL;
	g_autoptr(GPtrArray) pkgs = pk_poldek_lookup_request (pc, job, req, &kinds);
	g_autoptr(GPtrArray) installed = g_ptr_array_new ();
	g_autoptr(GPtrArray) available = g_ptr_array_new ();
	g_autoptr(GHashTable) done = g_hash_table_new (NULL, NULL);
	GQueue todo = G_QUEUE_INIT;
	PkPoldekSets sets;

	if (pkgs == NULL)
		return;
	pk_poldek_sets_init (&sets, pc);
	for (guint i = 0; i < pkgs->len; i++)
		g_queue_push_tail (&todo, g_ptr_array_index (pkgs, i));
	while (!g_queue_is_empty (&todo)) {
		struct pkg *pkg = g_queue_pop_head (&todo);
		PkPoldekProviders found;

		if (!g_hash_table_add (done, pkg))
			continue;
		if (pk_poldek_cancelled (job))
			break;
		pk_poldek_providers_init (&found);
		pk_poldek_pkg_providers (&sets, pkg, &found);
		g_ptr_array_extend (installed, found.installed, NULL, NULL);
		g_ptr_array_extend (available, found.available, NULL, NULL);
		for (guint i = 0; recursive && i < found.installed->len; i++)
			g_queue_push_tail (&todo, g_ptr_array_index (found.installed, i));
		for (guint i = 0; recursive && i < found.available->len; i++)
			g_queue_push_tail (&todo, g_ptr_array_index (found.available, i));
		pk_poldek_providers_clear (&found);
	}
	g_queue_clear (&todo);
	if (!pk_poldek_job_is_cancelled (job)) {
		/* the packages asked about are not their own dependencies */
		for (guint i = 0; i < pkgs->len; i++) {
			while (g_ptr_array_remove (installed, g_ptr_array_index (pkgs, i)))
				;
			while (g_ptr_array_remove (available, g_ptr_array_index (pkgs, i)))
				;
		}
		pk_poldek_emit_hits (job, &sets, filters, installed, available);
	}
	pk_poldek_sets_clear (&sets);
}

/* Whether @pkg depends on @target. */
static gboolean
pk_poldek_requires_pkg (PkPoldekSets *sets, const struct pkg *pkg, const struct pkg *target)
{
	for (int i = 0; pkg->reqs != NULL && i < n_array_size (pkg->reqs); i++) {
		const struct capreq *req = n_array_nth (pkg->reqs, i);
		PkPoldekRich *rich = pk_poldek_rich_parse (sets, req);

		if (rich != NULL) {
			if (pk_poldek_rich_needs (sets, pkg, target, rich))
				return TRUE;
		} else if (capreq_name (req)[0] != '(' && !pk_poldek_req_is_rpmlib (req) &&
			   !pk_poldek_pkg_provides (sets, pkg, req) &&
			   pk_poldek_pkg_provides (sets, target, req)) {
			return TRUE;
		}
	}
	return FALSE;
}

typedef struct
{
	GHashTable *index;
	struct pkg *pkg;
} PkPoldekIndexing;

static void
pk_poldek_index_name (const gchar *name, gpointer data)
{
	PkPoldekIndexing *indexing = data;
	GPtrArray *requirers = g_hash_table_lookup (indexing->index, name);

	if (requirers == NULL) {
		requirers = g_ptr_array_new ();
		g_hash_table_insert (indexing->index, g_strdup (name), requirers);
	}
	if (requirers->len == 0 ||
	    g_ptr_array_index (requirers, requirers->len - 1) != indexing->pkg)
		g_ptr_array_add (requirers, indexing->pkg);
}

static void
pk_poldek_index_dep (const struct capreq *dep, gpointer data)
{
	pk_poldek_index_name (capreq_name (dep), data);
}

/* The packages of @pkgs by the names of what they may depend on, capabilities or files. */
static GHashTable *
pk_poldek_requirers_new (PkPoldekSets *sets, tn_array *pkgs)
{
	PkPoldekIndexing indexing = { g_hash_table_new_full (g_str_hash,
							     g_str_equal,
							     g_free,
							     (GDestroyNotify) g_ptr_array_unref),
				      NULL };

	for (int i = 0; i < n_array_size (pkgs); i++) {
		indexing.pkg = n_array_nth (pkgs, i);
		for (int j = 0; indexing.pkg->reqs != NULL && j < n_array_size (indexing.pkg->reqs);
		     j++) {
			const struct capreq *req = n_array_nth (indexing.pkg->reqs, j);
			PkPoldekRich *rich = pk_poldek_rich_parse (sets, req);

			if (rich != NULL)
				pk_poldek_rich_deps (rich, pk_poldek_index_dep, &indexing);
			else if (capreq_name (req)[0] != '(' && !pk_poldek_req_is_rpmlib (req))
				pk_poldek_index_name (capreq_name (req), &indexing);
		}
	}
	return indexing.index;
}

typedef struct
{
	PkPoldekSets *sets;
	GHashTable *installed_index;
	GHashTable *available_index;
	/* installed package to the files it owns that something requires */
	GHashTable *installed_required_files;
	GPtrArray *installed;
	GPtrArray *available;
	GQueue *next;
} PkPoldekRequiredBy;

static void
pk_poldek_add_requirers_of (PkPoldekRequiredBy *by,
			    const struct pkg *target,
			    const gchar *name,
			    GHashTable *seen)
{
	GHashTable *indexes[] = { by->installed_index, by->available_index };
	GPtrArray *hits[] = { by->installed, by->available };

	for (guint k = 0; k < G_N_ELEMENTS (indexes); k++) {
		GPtrArray *requirers = indexes[k] != NULL ? g_hash_table_lookup (indexes[k], name)
							  : NULL;

		for (guint i = 0; requirers != NULL && i < requirers->len; i++) {
			struct pkg *pkg = g_ptr_array_index (requirers, i);

			if (pkg != target && !g_hash_table_contains (seen, pkg) &&
			    pk_poldek_requires_pkg (by->sets, pkg, target)) {
				g_hash_table_add (seen, pkg);
				g_ptr_array_add (hits[k], pkg);
				g_queue_push_tail (by->next, pkg);
			}
		}
	}
}

/* For installed packages: which of the files something requires each owns, from the rpm database. */
static GHashTable *
pk_poldek_installed_required_files (PkPoldekRequiredBy *by)
{
	GHashTable *indexes[] = { by->installed_index, by->available_index };
	GHashTable *files = g_hash_table_new_full (NULL,
						   NULL,
						   NULL,
						   (GDestroyNotify) g_ptr_array_unref);

	for (guint k = 0; k < G_N_ELEMENTS (indexes); k++) {
		GHashTableIter iter;
		gpointer name;

		if (indexes[k] == NULL)
			continue;
		g_hash_table_iter_init (&iter, indexes[k]);
		while (g_hash_table_iter_next (&iter, &name, NULL)) {
			GPtrArray *owners;

			if (((const gchar *) name)[0] != '/')
				continue;
			owners = pk_poldek_installed_owners (by->sets, name);
			for (guint i = 0; i < owners->len; i++) {
				GPtrArray *owned = g_hash_table_lookup (
				    files,
				    g_ptr_array_index (owners, i));

				if (owned == NULL) {
					owned = g_ptr_array_new ();
					g_hash_table_insert (files,
							     g_ptr_array_index (owners, i),
							     owned);
				}
				g_ptr_array_add (owned, name);
			}
		}
	}
	return files;
}

/* What depends on @target: by its name, what it provides and its files. */
static void
pk_poldek_add_requirers (PkPoldekRequiredBy *by, const struct pkg *target)
{
	g_autoptr(GHashTable) seen = g_hash_table_new (NULL, NULL);

	pk_poldek_add_requirers_of (by, target, target->name, seen);
	for (int i = 0; target->caps != NULL && i < n_array_size (target->caps); i++) {
		const struct capreq *cap = n_array_nth (target->caps, i);

		pk_poldek_add_requirers_of (by, target, capreq_name (cap), seen);
	}
	if (pk_poldek_is_installed (by->sets, target)) {
		GPtrArray *owned;

		if (by->installed_required_files == NULL)
			by->installed_required_files = pk_poldek_installed_required_files (by);
		owned = g_hash_table_lookup (by->installed_required_files, target);
		for (guint i = 0; owned != NULL && i < owned->len; i++)
			pk_poldek_add_requirers_of (by, target, g_ptr_array_index (owned, i), seen);
	} else if (target->fl != NULL && n_tuple_size (target->fl) > 0) {
		struct pkgfl_it it;
		const gchar *path;

		/* a file something requires is in the directories poldek keeps the files
		 * of in fl; the full list would be loaded, slowly out of order */
		pkgfl_it_init (&it, target->fl);
		while ((path = pkgfl_it_get (&it, NULL)) != NULL)
			pk_poldek_add_requirers_of (by, target, path, seen);
	}
}

void
pk_poldek_get_dependents (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	PkBitfield filters = pk_poldek_request_get_filters (req);
	gboolean recursive = pk_poldek_request_get_bool (req, "recursive");
	g_autoptr(GArray) kinds = NULL;
	g_autoptr(GPtrArray) pkgs = pk_poldek_lookup_request (pc, job, req, &kinds);
	g_autoptr(GPtrArray) installed = g_ptr_array_new ();
	g_autoptr(GPtrArray) available = g_ptr_array_new ();
	g_autoptr(GHashTable) done = g_hash_table_new (NULL, NULL);
	GQueue todo = G_QUEUE_INIT;
	GQueue next = G_QUEUE_INIT;
	PkPoldekSets sets;
	PkPoldekRequiredBy by = { &sets, NULL, NULL, NULL, installed, available, &next };

	if (pkgs == NULL)
		return;
	pk_poldek_sets_init (&sets, pc);
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED))
		by.installed_index = pk_poldek_requirers_new (&sets, sets.installed);
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED))
		by.available_index = pk_poldek_requirers_new (&sets, sets.available);
	for (guint i = 0; i < pkgs->len; i++)
		g_queue_push_tail (&todo, g_ptr_array_index (pkgs, i));
	while (!g_queue_is_empty (&todo)) {
		struct pkg *target = g_queue_pop_head (&todo);

		if (!g_hash_table_add (done, target))
			continue;
		if (pk_poldek_cancelled (job))
			break;
		pk_poldek_add_requirers (&by, target);
		if (recursive) {
			while (!g_queue_is_empty (&next))
				g_queue_push_tail (&todo, g_queue_pop_head (&next));
		} else {
			g_queue_clear (&next);
		}
	}
	g_queue_clear (&todo);
	g_queue_clear (&next);
	if (!pk_poldek_job_is_cancelled (job)) {
		for (guint i = 0; i < pkgs->len; i++) {
			while (g_ptr_array_remove (installed, g_ptr_array_index (pkgs, i)))
				;
			while (g_ptr_array_remove (available, g_ptr_array_index (pkgs, i)))
				;
		}
		pk_poldek_emit_hits (job, &sets, filters, installed, available);
	}
	/* before the indexes, it points to their keys */
	g_clear_pointer (&by.installed_required_files, g_hash_table_unref);
	g_clear_pointer (&by.installed_index, g_hash_table_unref);
	g_clear_pointer (&by.available_index, g_hash_table_unref);
	pk_poldek_sets_clear (&sets);
}
