/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Marcin Banasiak <megabajt@pld-linux.org>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <pk-backend.h>

#include <log.h>
#include <capreq.h>
#include <poldek.h>
#include <poclidek/dent.h>
#include <poclidek/poclidek.h>
#include <pkgdir/pkgdir.h>
#include <pkgdir/source.h>
#include <pkgu.h>
#include <pkgfl.h>
#include <pkgmisc.h>
#include <pm/pm.h>
#include <vfile/vfile.h>
#include <sigint/sigint.h>

static gchar* poldek_pkg_evr (const struct pkg *pkg);
static void poldek_backend_package (PkBackend *backend, struct pkg *pkg, PkInfoEnum infoenum, PkBitfield filters);
static long do_get_bytes_to_download (struct poldek_ts *ts, tn_array *pkgs);
static gint do_get_files_to_download (const struct poldek_ts *ts, const gchar *mark);
static void pb_load_packages (PkBackend *backend);
static void poldek_backend_set_allow_cancel (PkBackend *backend, gboolean allow_cancel, gboolean reset);

static void pb_error_show (PkBackend *backend, PkErrorEnum errorcode);
static void pb_error_clean (void);
static void poldek_backend_percentage_data_destroy (PkBackend *backend);

typedef enum {
	TS_TYPE_ENUM_INSTALL,
	TS_TYPE_ENUM_UPDATE,
	TS_TYPE_ENUM_REMOVE,
	TS_TYPE_ENUM_REFRESH_CACHE
} TsType;

enum {
	SEARCH_ENUM_NONE,
	SEARCH_ENUM_NAME,
	SEARCH_ENUM_GROUP,
	SEARCH_ENUM_DETAILS,
	SEARCH_ENUM_FILE,
	SEARCH_ENUM_PROVIDES,
	SEARCH_ENUM_RESOLVE
};

typedef struct {
	PkGroupEnum	group;
	const gchar	*regex;
} PLDGroupRegex;

static PLDGroupRegex group_perlre[] = {
	{ PK_GROUP_ENUM_ACCESSORIES, ".*Archiving\\|.*Dictionaries" },
	{ PK_GROUP_ENUM_ADMIN_TOOLS, ".*Databases.*\\|.*Admin" },
	{ PK_GROUP_ENUM_COMMUNICATION, ".*Communications" },
	{ PK_GROUP_ENUM_DOCUMENTATION, "Documentation" },
	{ PK_GROUP_ENUM_EDUCATION, ".*Engineering\\|.*Math\\|.*Science" },
	{ PK_GROUP_ENUM_FONTS, "Fonts" },
	{ PK_GROUP_ENUM_GAMES, ".*Games.*" },
	{ PK_GROUP_ENUM_GRAPHICS, ".*Graphics" },
	{ PK_GROUP_ENUM_LOCALIZATION, "I18n" },
	{ PK_GROUP_ENUM_MULTIMEDIA, ".*Multimedia\\|.*Sound" },
	{ PK_GROUP_ENUM_NETWORK, ".*Networking.*\\|/.*Mail\\|.*News\\|.*WWW" },
	{ PK_GROUP_ENUM_OFFICE, ".*Editors.*\\|.*Spreadsheets" },
	{ PK_GROUP_ENUM_OTHER, "^Applications$\\|.*Console\\|.*Emulators\\|.*File\\|.*Printing\\|.*Terminal\\|.*Text\\|^Libraries.*\\|^Themes.*\\|^X11$\\|.*Amusements\\|^X11\\/Applications$\\|^X11\\/Libraries$\\|.*Window\\ Managers.*" },
	{ PK_GROUP_ENUM_PROGRAMMING, ".*Development.*" },
	{ PK_GROUP_ENUM_PUBLISHING, ".*Publishing.*" },
	{ PK_GROUP_ENUM_SERVERS, "Daemons\\|.*Servers" },
	{ PK_GROUP_ENUM_SYSTEM, ".*Shells\\|.*System\\|Base.*" },
	{ 0, NULL }
};

typedef struct {
	gint		step; // current step

	/* Numer of sources to update. It's used only by refresh cache,
	 * as each source can have multiple files to download. I don't
	 * know how to get numer of files which will be downloaded. */
	guint		nsources;

	long		bytesget;
	long		bytesdownload;

	/* how many files I have already downloaded or which I'm currently
	 * downloading */
	guint		filesget;
	/* how many files I have to download */
	guint		filesdownload;

	gint		percentage;
	gint		subpercentage;
} PercentageData;

typedef enum {
	PB_RPM_STATE_ENUM_NONE = 0,
	PB_RPM_STATE_ENUM_INSTALLING = (1 << 1),
	PB_RPM_STATE_ENUM_REPACKAGING = (1 << 2)
} PbRpmState;

/* I need this to avoid showing error messages more than once.
 * It's initalized by backend_initalize() and destroyed by
 * backend_destroy(), but every method should clean it at the
 * end. */
typedef struct {
	PbRpmState	rpmstate;

	/* last 'vfff: foo' message */
	gchar		*vfffmsg;

	/* all messages merged into one string which can
	 * be displayed at the end of transaction. */
	GString		*tslog;
} PbError;

/* global variables */
static gint verbose = 1;
static PbError *pberror;
/* cached locale variants */
static GHashTable *clv;

static struct poldek_ctx	*ctx = NULL;
static struct poclidek_ctx	*cctx = NULL;

/**
 * execute_command:
 *
 * Execute specified command.
 *
 * Returns TRUE on success, FALSE when some error occurs.
 **/
static gboolean
execute_command (const gchar *format, ...)
{
	struct poclidek_rcmd *rcmd;
	struct poldek_ts *ts;
	gchar *command;
	va_list args;
	gboolean result = FALSE;

	va_start (args, format);
	command = g_strdup_vprintf (format, args);
	va_end (args);

	ts = poldek_ts_new (ctx, 0);
	rcmd = poclidek_rcmd_new (cctx, ts);

	ts->setop(ts, POLDEK_OP_PARTICLE, 0);

	if (poclidek_rcmd_execline (rcmd, command)) {
		result = TRUE;
	}

	poclidek_rcmd_free (rcmd);
	poldek_ts_free (ts);

	g_free (command);

	return result;
}

/**
 * execute_packages_command:
 *
 * Execute specified command.
 *
 * Returns on success pointer to the tn_array containing packages which are the
 * result of specified command. On failure returns NULL.
 **/
static tn_array*
execute_packages_command (const gchar *format, ...)
{
	struct poclidek_rcmd *rcmd;
	tn_array *packages = NULL;
	va_list args;
	gchar *command;

	va_start (args, format);
	command = g_strdup_vprintf (format, args);
	va_end (args);

	rcmd = poclidek_rcmd_new (cctx, NULL);

	if (poclidek_rcmd_execline (rcmd, command)) {
		packages = poclidek_rcmd_get_packages (rcmd);
	}

	poclidek_rcmd_free (rcmd);

	g_free (command);

	return packages;
}

/**
 * cut_country_code: (copied from poldek::misc.c)
 *
 * Usually lang looks like:
 *   ll[_CC][.EEEE][@dddd]
 * where:
 *   ll      ISO language code
 *   CC      (optional) ISO country code
 *   EE      (optional) encoding
 *   dd      (optional) dialect
 *
 * Returns: lang without country code (ll[.EEEE][@dddd]) or NULL when it's not
 *          present in lang string. Returned value must be released.
 **/
static gchar*
cut_country_code (const gchar *lang)
{
	gchar *p;
	gchar *q;
	gchar *newlang;

	if ((q = strchr (lang, '_')) == NULL)
		return NULL;

	/* newlang is always shorter than lang */
	newlang = malloc (strlen (lang));

	p = n_strncpy (newlang, lang, q - lang + 1);

	if ((q = strchr (lang, '.')))
		n_strncpy (p, q, strlen (q) + 1);
	else if ((q = strchr (lang, '@')))
		n_strncpy(p, q, strlen(q) + 1);

	return newlang;
}

/**
 * get_locale_variants:
 *
 * Returns pointer to tn_array in which are locales which suit to lang. For
 * example: for lang "pl_PL.UTF-8", returned array will contain "pl_PL.UTF-8",
 * "pl.UTF-8", "pl_PL" and "pl". This array is needed by pkg_xuinf().
 **/
static tn_array*
get_locale_variants (PkBackend *backend, const gchar *lang)
{
	tn_array *langs;
	gchar *copy;
	gchar *wocc = NULL;
	const gchar *sep = "@.";
	gint len;

	/* first check cached_locale_variants */
	if ((langs = g_hash_table_lookup (clv, lang)) != NULL)
		return langs;

	langs = n_array_new (2, (tn_fn_free)free, NULL);

	n_array_push (langs, g_strdup (lang));

	/* try without country code */
	if ((wocc = cut_country_code (lang)) != NULL)
		n_array_push (langs, wocc);

	len = strlen (lang) + 1;
	copy = g_alloca (len);
	memcpy (copy, lang, len);

	while (*sep) {
		gchar *p;

		if ((p = strchr (copy, *sep)) != NULL) {
			*p = '\0';

			n_array_push (langs, g_strdup (copy));

			/* try without country code */
			if ((wocc = cut_country_code (copy)) != NULL)
				n_array_push (langs, wocc);
		}

		sep++;
	}

	g_hash_table_insert (clv, g_strdup (lang), langs);

	return langs;
}

/**
 * pkg_uinf_i18n:
 *
 * Returns pointer to struct pkguinf with localized summary and description.
 **/
static struct pkguinf*
pkg_uinf_i18n (PkBackend *backend, struct pkg *pkg)
{
	struct pkguinf *pkgu = NULL;
	gchar *lang = NULL;

	lang = pk_backend_get_locale (backend);

	if (lang) {
		tn_array *langs;

		langs = get_locale_variants (backend, lang);
		pkgu = pkg_xuinf (pkg, langs);
	} else {
		pkgu = pkg_uinf (pkg);
	}

	g_free (lang);

	return pkgu;
}

static gint
poldek_get_files_to_download (const struct poldek_ts *ts)
{
	gint	files = 0;

	files += do_get_files_to_download (ts, "I");
	files += do_get_files_to_download (ts, "D");

	return files;
}

static gint
do_get_files_to_download (const struct poldek_ts *ts, const gchar *mark)
{
	tn_array	*pkgs = NULL;
	gint		files = 0;

	pkgs = poldek_ts_get_summary (ts, mark);

	if (pkgs) {
		files = n_array_size (pkgs);
		n_array_free (pkgs);
	}

	return files;
}

/**
 * poldek_get_bytes_to_download:
 *
 * Returns: bytes to download
 */
static long
poldek_get_bytes_to_download (struct poldek_ts *ts, tn_array *pkgs)
{
	return do_get_bytes_to_download (ts, pkgs);
}

static long
poldek_get_bytes_to_download_from_ts (struct poldek_ts *ts)
{
	tn_array *pkgs = NULL;
	long bytes = 0;

	pkgs = poldek_ts_get_summary (ts, "I");

	if (pkgs) {
		bytes += do_get_bytes_to_download (ts, pkgs);

		n_array_free (pkgs);
	}

	pkgs = poldek_ts_get_summary (ts, "D");

	if (pkgs) {
		bytes += do_get_bytes_to_download (ts, pkgs);

		n_array_free (pkgs);
	}

	return bytes;
}

static long
do_get_bytes_to_download (struct poldek_ts *ts, tn_array *pkgs)
{
	size_t i;
	long bytes = 0;

	for (i = 0; i < n_array_size (pkgs); i++) {
		struct pkg	*pkg = n_array_nth (pkgs, i);
		gchar		path[1024];

		if (pkg->pkgdir && (vf_url_type (pkg->pkgdir->path) & VFURL_REMOTE)) {
			if (pkg_localpath (pkg, path, sizeof(path), ts->cachedir)) {
				if (access(path, R_OK) != 0) {
					bytes += pkg->fsize;
				} else {
					if (!pm_verify_signature(ts->pmctx, path, PKGVERIFY_MD)) {
						bytes += pkg->fsize;
					}
				}
			}
		}
	}

	return bytes;
}

/**
 * VF_PROGRESS
 */
static void*
poldek_vf_progress_new (void *data, const gchar *label)
{
	PkBackend *backend = (PkBackend*) data;
	guint ts_type = pk_backend_get_uint (backend, "ts_type");

	if (ts_type == TS_TYPE_ENUM_INSTALL || ts_type == TS_TYPE_ENUM_UPDATE) {
		gchar *filename = g_path_get_basename (label), *pkgname;
		tn_array *packages = NULL;

		pkgname = g_strndup (filename, (sizeof(gchar)*strlen(filename)-4));

		pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);

		if ((packages = execute_packages_command ("cd /all-avail; ls -q %s", pkgname)) != NULL) {
			struct pkg *pkg = n_array_nth (packages, 0);

			poldek_backend_package (backend, pkg, PK_INFO_ENUM_DOWNLOADING, PK_FILTER_ENUM_NONE);

			n_array_free (packages);
		}

		g_free (pkgname);
		g_free (filename);
	}

	return data;
}

static void
poldek_vf_progress (void *bar, long total, long amount)
{
	PkBackend	*backend = (PkBackend*) bar;
	PercentageData	*pd = pk_backend_get_pointer (backend, "percentage_ptr");
	guint ts_type = pk_backend_get_uint (backend, "ts_type");

	if (ts_type == TS_TYPE_ENUM_INSTALL || ts_type == TS_TYPE_ENUM_UPDATE) {
		float	frac = (float)amount / (float)total;

		/* file already downloaded */
		if (frac < 0) {
			pd->bytesget += total;
			pd->filesget++;

			pd->percentage = (gint)((float)(pd->bytesget) / (float)pd->bytesdownload * 100);
			pd->subpercentage = 100;
		} else {
			pd->percentage = (gint)(((float)(pd->bytesget + amount) / (float)pd->bytesdownload) * 100);
			pd->subpercentage = (gint)(frac * 100);
		}

		pk_backend_set_sub_percentage (backend, pd->subpercentage);

	} else if (ts_type == TS_TYPE_ENUM_REFRESH_CACHE) {
		if (pd->step == 0)
			pd->percentage = 1;
		else
			pd->percentage = (gint)(((float)pd->step / (float)pd->nsources) * 100);
	}

	pk_backend_set_percentage (backend, pd->percentage);
}

static void
poldek_vf_progress_reset (void *bar)
{
	PkBackend *backend = (PkBackend *) bar;
	PercentageData *pd = pk_backend_get_pointer (backend, "percentage_ptr");
	pd->subpercentage = 0;
}

/**
 * poldek_pkg_in_array_idx:
 *
 * Returns index of the first matching package. If not found, -1 will be returned.
 **/
static gint
poldek_pkg_in_array_idx (const struct pkg *pkg, const tn_array *array, tn_fn_cmp cmp_fn)
{
	size_t	i;

	if (array) {
		for (i = 0; i < n_array_size (array); i++) {
			struct pkg	*p = n_array_nth (array, i);

			if (cmp_fn (pkg, p) == 0)
				return i;
		}
	}

	return -1;
}

static gboolean
poldek_pkg_in_array (const struct pkg *pkg, const tn_array *array, tn_fn_cmp cmp_fn)
{
	if (array == NULL)
		return FALSE;

	if (poldek_pkg_in_array_idx (pkg, array, cmp_fn) == -1)
		return FALSE;
	else
		return TRUE;
}

static void
get_ts_summary (TsType type, tn_array *ipkgs, tn_array *dpkgs, tn_array *rpkgs,
		tn_array **install_pkgs, tn_array **update_pkgs, tn_array **remove_pkgs)
{
	guint  i;

	if (type == TS_TYPE_ENUM_INSTALL || type == TS_TYPE_ENUM_UPDATE) {
		*install_pkgs = n_array_new (2, (tn_fn_free)pkg_free, (tn_fn_cmp)pkg_cmp_name_evr);
		*update_pkgs = n_array_new (2, (tn_fn_free)pkg_free, (tn_fn_cmp)pkg_cmp_name_evr);
	}

	*remove_pkgs = n_array_new (2, (tn_fn_free)pkg_free, (tn_fn_cmp)pkg_cmp_name_evr);

	switch (type) {
		case TS_TYPE_ENUM_INSTALL:
		case TS_TYPE_ENUM_UPDATE:
			if (rpkgs) {
				for (i = 0; i < n_array_size (rpkgs); i++) {
					struct pkg *rpkg = n_array_nth (rpkgs, i);

					if (poldek_pkg_in_array (rpkg, ipkgs, (tn_fn_cmp)pkg_cmp_name) ||
					    poldek_pkg_in_array (rpkg, dpkgs, (tn_fn_cmp)pkg_cmp_name)) {
						n_array_push (*update_pkgs, pkg_link (rpkg));

					} else {
						n_array_push (*remove_pkgs, pkg_link (rpkg));
					}
				}
			}

			if (ipkgs) {
				for (i = 0; i < n_array_size (ipkgs); i++) {
					struct pkg *ipkg = n_array_nth (ipkgs, i);

					if (poldek_pkg_in_array (ipkg, *update_pkgs, (tn_fn_cmp)pkg_cmp_name) == FALSE)
						n_array_push (*install_pkgs, pkg_link (ipkg));
				}
			}

			if (dpkgs) {
				for (i = 0; i < n_array_size (dpkgs); i++) {
					struct pkg *dpkg = n_array_nth (dpkgs, i);

					if (poldek_pkg_in_array (dpkg, *update_pkgs, (tn_fn_cmp)pkg_cmp_name) == FALSE)
						n_array_push (*install_pkgs, pkg_link (dpkg));
				}
			}
			break;
		case TS_TYPE_ENUM_REMOVE:
			/* copy packages from rpkgs and dpkgs to remove_pkgs */
			if (rpkgs)
				n_array_concat_ex (*remove_pkgs, rpkgs, (tn_fn_dup)pkg_link);

			if (dpkgs)
				n_array_concat_ex (*remove_pkgs, dpkgs, (tn_fn_dup)pkg_link);

			break;
		default:
			g_error ("Unknown ts_type value: %d", type);
	}

	/* return sorted arrays */
	if (*install_pkgs)
		n_array_sort (*install_pkgs);

	if (*update_pkgs)
		n_array_sort (*update_pkgs);

	if (*remove_pkgs)
		n_array_sort (*remove_pkgs);
}

/**
 * ts_confirm:
 * Returns Yes - 1
 *	    No - 0
 */
static int
ts_confirm (void *data, struct poldek_ts *ts)
{
	tn_array	*ipkgs, *dpkgs, *rpkgs;
	PkBackend	*backend = (PkBackend *)data;
	size_t		i = 0;
	gint		result = 1;

	ipkgs = poldek_ts_get_summary (ts, "I");
	dpkgs = poldek_ts_get_summary (ts, "D");
	rpkgs = poldek_ts_get_summary (ts, "R");

	if (poldek_ts_get_type (ts) == POLDEK_TS_TYPE_INSTALL) {
		tn_array *update_pkgs, *remove_pkgs, *install_pkgs;
		PercentageData *pd = pk_backend_get_pointer (backend, "percentage_ptr");
		guint to_install = 0;

		update_pkgs = n_array_new (4, (tn_fn_free)pkg_free, NULL);
		remove_pkgs = n_array_new (4, (tn_fn_free)pkg_free, NULL);
		install_pkgs = n_array_new (4, (tn_fn_free)pkg_free, NULL);

		pd->step = 0;

		pd->bytesget = 0;
		pd->bytesdownload = poldek_get_bytes_to_download_from_ts (ts);

		pd->filesget = 0;
		pd->filesdownload = poldek_get_files_to_download (ts);

		pberror->rpmstate = PB_RPM_STATE_ENUM_NONE;

		/* create an array with pkgs which will be updated */
		if (rpkgs) {
			for (i = 0; i < n_array_size (rpkgs); i++) {
				struct pkg	*rpkg = n_array_nth (rpkgs, i);

				if (poldek_pkg_in_array (rpkg, ipkgs, (tn_fn_cmp)pkg_cmp_name))
					n_array_push (update_pkgs, pkg_link (rpkg));
				else if (poldek_pkg_in_array (rpkg, dpkgs, (tn_fn_cmp)pkg_cmp_name))
					n_array_push (update_pkgs, pkg_link (rpkg));
				else
					n_array_push (remove_pkgs, pkg_link (rpkg));
			}
		}

		/* create an array with pkgs which will be installed */
		if (ipkgs) {
			for (i = 0; i < n_array_size (ipkgs); i++) {
				struct pkg *ipkg = n_array_nth (ipkgs, i);

				if (poldek_pkg_in_array (ipkg, update_pkgs, (tn_fn_cmp)pkg_cmp_name) == FALSE)
					n_array_push (install_pkgs, pkg_link (ipkg));
			}
		}
		if (dpkgs) {
			for (i = 0; i < n_array_size (dpkgs); i++) {
				struct pkg *dpkg = n_array_nth (dpkgs, i);

				if (poldek_pkg_in_array (dpkg, update_pkgs, (tn_fn_cmp)pkg_cmp_name) == FALSE)
					n_array_push (install_pkgs, pkg_link (dpkg));
			}
		}

		/* packages to install & update */
		to_install = n_array_size (install_pkgs);
		to_install += n_array_size (update_pkgs);

		pk_backend_set_uint (backend, "to_install", to_install);

		pk_backend_set_pointer (backend, "to_update_pkgs", update_pkgs);
		pk_backend_set_pointer (backend, "to_remove_pkgs", remove_pkgs);
		pk_backend_set_pointer (backend, "to_install_pkgs", install_pkgs);
	} else if (poldek_ts_get_type (ts) == POLDEK_TS_TYPE_UNINSTALL) {
		gboolean allow_deps = pk_backend_get_bool (backend, "allow_deps");

		/* check if transaction can be performed */
		if (allow_deps == FALSE) {
			if (dpkgs && n_array_size (dpkgs) > 0) {
				result = 0;
			}
		}

		if (result == 1) { /* remove is allowed */
			pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);

			/* we shouldn't cancel remove proccess */
			poldek_backend_set_allow_cancel (backend, FALSE, FALSE);

			if (dpkgs) {
				for (i = 0; i < n_array_size (dpkgs); i++) {
					struct pkg *pkg = n_array_nth (dpkgs, i);

					poldek_backend_package (backend, pkg, PK_INFO_ENUM_REMOVING, PK_FILTER_ENUM_NONE);
				}
			}

			if (rpkgs) {
				for (i = 0; i < n_array_size (rpkgs); i++) {
					struct pkg *pkg = n_array_nth (rpkgs, i);

					poldek_backend_package (backend, pkg, PK_INFO_ENUM_REMOVING, PK_FILTER_ENUM_NONE);
				}
			}
		}
	}

	n_array_cfree (&ipkgs);
	n_array_cfree (&dpkgs);
	n_array_cfree (&rpkgs);

	return result;
}

/**
 * suggests_callback:
 **/
static gint
suggests_callback (void *data, const struct poldek_ts *ts, const struct pkg *pkg,
		   tn_array *caps, tn_array *choices, int hint)
{
	/* install all suggested packages */
	return 1;
}
/**
 * setup_vf_progress:
 */
static void
setup_vf_progress (struct vf_progress *vf_progress, PkBackend *backend)
{
	vf_progress->data = backend;
	vf_progress->new = poldek_vf_progress_new;
	vf_progress->progress = poldek_vf_progress;
	vf_progress->reset = poldek_vf_progress_reset;
	vf_progress->free = NULL;

	vfile_configure (VFILE_CONF_VERBOSE, &verbose);
	vfile_configure (VFILE_CONF_STUBBORN_NRETRIES, 5);

	poldek_configure (ctx, POLDEK_CONF_VFILEPROGRESS, vf_progress);
}

static gint
pkg_cmp_name_evr_rev_recno (const struct pkg *p1, const struct pkg *p2) {
	register gint rc;

	if ((rc = pkg_cmp_name_evr_rev (p1, p2)) == 0)
		rc = -(p1->recno - p2->recno);

	return rc;
}

/**
 * do_post_search_process:
 *
 * Merges installed, available and removes duplicates.
 *
 **/
static tn_array *
do_post_search_process (tn_array *installed, tn_array *available)
{
	tn_array       *packages = NULL;
	guint		i;

	if (available != NULL) {
		packages = n_ref (available);

		if (installed != NULL) {
			for (i = 0; i < n_array_size (installed); i++) {
				struct pkg *pkg = n_array_nth (installed, i);

				/* check for duplicates */
				if (poldek_pkg_in_array (pkg, packages, (tn_fn_cmp)pkg_cmp_name_evr) == FALSE)
					n_array_push (packages, pkg_link (pkg));

			}

			n_array_sort_ex (packages, (tn_fn_cmp)pkg_cmp_name_evr_rev_recno);
		}

	} else if (installed != NULL) {
		packages = n_ref (installed);
	}

	return packages;
}

static gboolean
pkg_is_installed (struct pkg *pkg)
{
	struct pkgdb *db;
	gint cmprc, is_installed = 0;
	struct poldek_ts *ts;

	g_return_val_if_fail (pkg != NULL, FALSE);

	/* XXX: I don't know how to get ctx->rootdir */
	ts = poldek_ts_new (ctx, 0);

	db = pkgdb_open (ts->pmctx, ts->rootdir, NULL, O_RDONLY, NULL);

	if (db) {
		is_installed = pkgdb_is_pkg_installed (db, pkg, &cmprc);

		pkgdb_free (db);
	}

	poldek_ts_free (ts);

	return is_installed ? TRUE : FALSE;
}

/**
 * get_pkgid_from_localpath:
 *
 * Query rpmdb by localpath.
 *
 * localpath: full path to the file on local filesystem
 *
 * Returns: pkgid (foo-bar-0.1.2-3.i686) that owns file at localpath or NULL.
 **/
static gchar *
get_pkgid_from_localpath (const gchar *localpath)
{
	struct pkgdb *db = NULL;
	struct poldek_ts *ts = NULL;
	gchar *pkgid = NULL;

	g_return_val_if_fail (localpath != NULL, NULL);

	ts = poldek_ts_new (ctx, 0);
	db = pkgdb_open (ts->pmctx, ts->rootdir, NULL, O_RDONLY, NULL);

	if (db) {
		const struct pm_dbrec *ldbrec;
		struct pkgdb_it it;

		pkgdb_it_init (db, &it, PMTAG_FILE, localpath);

		/* get only one package */
		if ((ldbrec = pkgdb_it_get (&it)) != NULL) {
			gchar *name = NULL, *version = NULL, *release = NULL, *arch = NULL;
			gint epoch;

			pm_dbrec_nevr (ldbrec, &name, &epoch, &version, &release, &arch, NULL);

			pkgid = g_strdup_printf ("%s-%s-%s.%s", name, version, release, arch);
		}

		pkgdb_it_destroy (&it);
		/* it calls pkgdb_close (db) */
		pkgdb_free (db);
	}

	poldek_ts_free (ts);

	return pkgid;
}

/**
 * poldek_get_security_updates:
 **/
static tn_array*
poldek_get_security_updates (void)
{
	return execute_packages_command ("cd /all-avail; ls -S");
}

/**
 * pld_group_to_enum:
 *
 * Converts PLD RPM group to PkGroupEnum.
 **/
static PkBitfield
pld_group_to_enum (const gchar *group)
{
	g_return_val_if_fail (group != NULL, PK_GROUP_ENUM_OTHER);

	if (strstr (group, "Archiving") != NULL ||
	    strstr (group, "Dictionaries") != NULL)
		return PK_GROUP_ENUM_ACCESSORIES;
	else if (strstr (group, "Databases") != NULL ||
		 strstr (group, "Admin") != NULL)
		return PK_GROUP_ENUM_ADMIN_TOOLS;
	else if (strstr (group, "Communications") != NULL)
		return PK_GROUP_ENUM_COMMUNICATION;
	else if (strstr (group, "Engineering") != NULL ||
		 strstr (group, "Math") != NULL	||
		 strstr (group, "Science") != NULL)
		return PK_GROUP_ENUM_EDUCATION;
	else if (strcmp (group, "Documentation") == 0)
		return PK_GROUP_ENUM_DOCUMENTATION;
	else if (strcmp (group, "Fonts") == 0)
		return PK_GROUP_ENUM_FONTS;
	else if (strstr (group, "Games") != NULL)
		return PK_GROUP_ENUM_GAMES;
	else if (strstr (group, "Graphics") != NULL)
		return PK_GROUP_ENUM_GRAPHICS;
	else if (strcmp (group, "I18n") == 0)
		return PK_GROUP_ENUM_LOCALIZATION;
	else if (strstr (group, "Multimedia") != NULL ||
		 strstr (group, "Sound") != NULL)
		return PK_GROUP_ENUM_MULTIMEDIA;
	else if (strstr (group, "Networking") != NULL ||
		 strstr (group, "Mail") != NULL ||
		 strstr (group, "News") != NULL ||
		 strstr (group, "WWW") != NULL)
		return PK_GROUP_ENUM_NETWORK;
	else if (strstr (group, "Editors") != NULL ||
		 strstr (group, "Spreadsheets") != NULL)
		return PK_GROUP_ENUM_OFFICE;
	else if (strstr (group, "Development") != NULL)
		return PK_GROUP_ENUM_PROGRAMMING;
	else if (strstr (group, "Publishing") != NULL)
		return PK_GROUP_ENUM_PUBLISHING;
	else if (strstr (group, "Daemons") != NULL ||
		 strstr (group, "Servers") != NULL)
		return PK_GROUP_ENUM_SERVERS;
	else if (strstr (group, "Shells") != NULL ||
		 strstr (group, "System") != NULL ||
		 strstr (group, "Base") != NULL)
		return PK_GROUP_ENUM_SYSTEM;
	else
		return PK_GROUP_ENUM_OTHER;
}

/**
 * pld_group_get_regex_from_text:
 **/
static const gchar*
pld_group_get_regex_from_text (const gchar *str)
{
	guint		i = 0;

	while (group_perlre[i].regex) {
		if (pk_group_enum_from_string (str) == group_perlre[i].group)
			return group_perlre[i].regex;

		i++;
	}

	return NULL;
}

/**
 * poldek_pkg_evr:
 */
static gchar*
poldek_pkg_evr (const struct pkg *pkg)
{
	if (pkg->epoch == 0)
		return g_strdup_printf ("%s-%s", pkg->ver, pkg->rel);
	else
		return g_strdup_printf ("%d:%s-%s", pkg->epoch, pkg->ver, pkg->rel);
}

static gchar*
poldek_get_vr_from_package_id_evr (const gchar *evr)
{
	gchar		**sections, *result;

	sections = g_strsplit (evr, ":", 2);

	if (sections[1])
		result = g_strdup (sections[1]);
	else
		result = g_strdup (evr);

	g_strfreev (sections);

	return result;
}

/**
 * poldek_get_nvra_from_package_id:
 */
static gchar*
poldek_get_nvra_from_package_id (const gchar* package_id)
{
	gchar **parts = NULL;
	gchar  *nvra = NULL;

	g_return_val_if_fail (package_id != NULL, NULL);

	if ((parts = pk_package_id_split (package_id))) {
		gchar *vr = NULL;

		vr = poldek_get_vr_from_package_id_evr (parts[PK_PACKAGE_ID_VERSION]);

		nvra = g_strdup_printf ("%s-%s.%s", parts[PK_PACKAGE_ID_NAME],
						    vr,
						    parts[PK_PACKAGE_ID_ARCH]);

		g_free (vr);
		g_strfreev (parts);
	}

	return nvra;
}

/**
 * poldek_get_installed_packages:
 */
static tn_array*
poldek_get_installed_packages (void)
{
	return poclidek_get_dent_packages (cctx, POCLIDEK_INSTALLEDDIR);
}

static tn_array*
poldek_pkg_get_cves_from_pld_changelog (struct pkg *pkg, time_t since)
{
	struct pkguinf	*inf = NULL;
	const gchar	*ch;
	gchar *chlog = NULL;
	tn_array		*cves = NULL;

	if ((inf = pkg_uinf (pkg)) == NULL)
		return NULL;

	if ((ch = pkguinf_get_changelog (inf, since))) {
		chlog = g_strdup(ch);
		if (g_strstr_len (chlog, 55 * sizeof (gchar), " poldek@pld-linux.org\n- see ")) { /* pkg is subpackage */
			gchar *s, *e;

			s = strchr (chlog, '\n');

			s += 7; /* cut "\n- see " */

			if ((e = strchr (s, '\''))) {
				tn_array *packages = NULL;

				*e = '\0'; /* now s is the name of package with changelog */

				/* release it */
				g_free (chlog);
				chlog = NULL;

				if ((packages = execute_packages_command ("cd /all-avail; ls -q %s*", s)) != NULL) {
					struct pkg *p = n_array_nth (packages, 0);
					struct pkguinf *inf_parent = NULL;

					if ((inf_parent = pkg_uinf (p))) {
						if ((ch = pkguinf_get_changelog (inf_parent, since)))
							chlog = g_strdup(ch);

						pkguinf_free (inf_parent);
					}

					n_array_free (packages);
				}
			}
		}
	}

	if (chlog && strlen (chlog) > 0) {
		gchar *s=chlog;

		cves = n_array_new (2, free, (tn_fn_cmp)strcmp);
		while (1) {
			gchar cve[14];
			gboolean valid = TRUE;
			gint i;

			if ((s = strstr (s, "CVE-")) == NULL)
				break;

			if (strlen (s) < 13) /* CVE-XXXX-YYYY has 13 chars */
				break;

			for (i = 0; i < 14; i++) {
				if (i == 13)
					cve[i] = '\0';
				else
					cve[i] = *(s + i);
			}

			for (i = 4; i < 13; i++) {
				if (i == 8) {
					if (cve[i] != '-') {
						valid = FALSE;
						break;
					}
				} else if (g_ascii_isdigit (cve[i]) == FALSE) {
					valid = FALSE;
					break;
				}
			}

			if (valid)
				n_array_push (cves, g_strdup(cve));

			s += 13;
		}
	}

	pkguinf_free (inf);

	g_free (chlog);

	return cves;
}

/**
 * poldek_pkg_is_devel:
 */
static gboolean
poldek_pkg_is_devel (struct pkg *pkg)
{
	if (g_str_has_suffix (pkg->name, "-devel"))
		return TRUE;
	if (g_str_has_suffix (pkg->name, "-debuginfo"))
		return TRUE;
	if (g_str_has_suffix (pkg->name, "-static"))
		return TRUE;

	return FALSE;
}

/**
 * poldek_pkg_is_gui:
 */
static gboolean
poldek_pkg_is_gui (struct pkg *pkg)
{
	if (g_str_has_prefix (pkg_group (pkg), "X11"))
		return TRUE;

	return FALSE;
}

static void
do_newest (tn_array *pkgs)
{
	guint i = 1;

	if (!n_array_is_sorted (pkgs))
		n_array_sort_ex (pkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev_recno);

	while (i < pkgs->items) {
		if (pkg_cmp_name (pkgs->data[i - 1], pkgs->data[i]) == 0) {
			struct pkg *pkg = n_array_nth (pkgs, i);

			if (!pkg_is_installed (pkg)) {
				n_array_remove_nth (pkgs, i);
				continue;
			}
		}

		i++;
	}
}

/**
 * do_filtering:
 *
 * Apply newest, devel and gui filters (if requested).
 *
 **/
static void
do_filtering (tn_array *packages, PkBitfield filters)
{
	guint	i = 0;

	g_return_if_fail (packages != NULL);

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NEWEST))
		do_newest (packages);

	while (i < n_array_size (packages)) {
		struct pkg     *pkg = n_array_nth (packages, i);

		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT))
			if (!poldek_pkg_is_devel (pkg)) {
				n_array_remove_nth (packages, i);
				continue;
			}

		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT))
			if (poldek_pkg_is_devel (pkg)) {
				n_array_remove_nth (packages, i);
				continue;
			}

		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI))
			if (!poldek_pkg_is_gui (pkg)) {
				n_array_remove_nth (packages, i);
				continue;
			}

		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_GUI))
			if (poldek_pkg_is_gui (pkg)) {
				n_array_remove_nth (packages, i);
				continue;
			}

		i++;
	}
}

/**
 * do_requires:
 */
static void
do_requires (tn_array *installed, tn_array *available, tn_array *requires,
	     struct pkg *pkg, PkBackend *backend)
{
	tn_array	*tmp = NULL;
	size_t		i;
	PkBitfield filters;
	gboolean recursive;

	tmp = n_array_new (2, NULL, NULL);
	filters = pk_backend_get_uint (backend, "filters");

	/* if ~installed doesn't exists in filters, we can query installed */
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		for (i = 0; i < n_array_size (installed); i++) {
			struct pkg      *ipkg = n_array_nth (installed, i);
			size_t j;

			/* self match */
			if (pkg_cmp_name_evr (pkg, ipkg) == 0)
				continue;

			/* skip when there is no reqs */
			if (!ipkg->reqs)
				continue;

			/* package already added to the array */
			if (poldek_pkg_in_array (ipkg, requires, (tn_fn_cmp)pkg_cmp_name_evr_rev))
				continue;

			for (j = 0; j < n_array_size (ipkg->reqs); j++) {
				struct capreq   *req = n_array_nth (ipkg->reqs, j);

				if (capreq_is_rpmlib (req))
					continue;
				else if (capreq_is_file (req))
					continue;

				if (pkg_satisfies_req (pkg, req, 1)) {
					n_array_push (requires, pkg_link (ipkg));
					n_array_push (tmp, pkg_link (ipkg));
					break;
				}
			}
		}
	}
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		for (i = 0; i < n_array_size (available); i++) {
			struct pkg      *apkg = n_array_nth (available, i);
			size_t j;

			/* self match */
			if (pkg_cmp_name_evr (pkg, apkg) == 0)
				continue;

			if (!apkg->reqs)
				continue;

			/* package already added to the array */
			if (poldek_pkg_in_array (apkg, requires, (tn_fn_cmp)pkg_cmp_name_evr_rev))
				continue;

			for (j = 0; j < n_array_size (apkg->reqs); j++) {
				struct capreq   *req = n_array_nth (apkg->reqs, j);

				if (capreq_is_rpmlib (req))
					continue;
				else if (capreq_is_file (req))
					continue;

				if (pkg_satisfies_req (pkg, req, 1)) {
					n_array_push (requires, pkg_link (apkg));
					n_array_push (tmp, pkg_link (apkg));
					break;
				}
			}
		}
	}

	/* FIXME: recursive takes too much time for available packages, so don't use it */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		recursive = pk_backend_get_bool (backend, "recursive");
		if (recursive && tmp && n_array_size (tmp) > 0) {
			for (i = 0; i < n_array_size (tmp); i++) {
				struct pkg	*p = n_array_nth (tmp, i);
				do_requires (installed, available, requires, p, backend);
			}
		}
	}

	n_array_free (tmp);
}

/**
 * do_depends:
 */
static void
do_depends (tn_array *installed, tn_array *available, tn_array *depends, struct pkg *pkg, PkBackend *backend)
{
	tn_array	*reqs = pkg->reqs;
	tn_array	*tmp = NULL;
	size_t		i;
	PkBitfield filters;
	gboolean recursive;

	tmp = n_array_new (2, NULL, NULL);
	filters = pk_backend_get_uint (backend, "filters");
	recursive = pk_backend_get_bool (backend, "recursive");

	/* nothing to do */
	if (!reqs || (reqs && n_array_size (reqs) < 1))
		return;

	for (i = 0; i < n_array_size (reqs); i++) {
		struct capreq	*req = n_array_nth (reqs, i);
		gboolean	found = FALSE;
		size_t		j;

		/* skip it */
		if (capreq_is_rpmlib (req))
			continue;

		/* FIXME: pkg_satisfies_req() doesn't find file capreq's
		 * in installed packages, so skip them */
		if (capreq_is_file (req))
			continue;

		/* self match */
		if (pkg_satisfies_req (pkg, req, 1))
			continue;

		/* Maybe this capreq is satisfied by package already added to
		 * depends array. */
		for (j = 0; j < n_array_size (depends); j++) {
			struct pkg	*p = n_array_nth (depends, j);

			if (pkg_satisfies_req (p, req, 1)) {
				/* Satisfied! */
				found = TRUE;
				break;
			}
		}

		if (found)
			continue;

		/* first check in installed packages */
		if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
			for (j = 0; j < n_array_size (installed); j++) {
				struct pkg	*p = n_array_nth (installed, j);

				if (pkg_satisfies_req (p, req, 1)) {
					found = TRUE;
					n_array_push (depends, pkg_link (p));
					n_array_push (tmp, pkg_link (p));
					break;
				}
			}
		}

		if (found)
			continue;

		/* ... now available */
		if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
			for (j = 0; j < n_array_size (available); j++) {
				struct pkg	*p = n_array_nth (available, j);

				if (pkg_satisfies_req (p, req, 1)) {
					/* If only available packages are queried,
					 * don't return these, which are installed.
					 * Can be used to tell the user which packages
					 * will be additionaly installed. */
					if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
						gint	ret;

						ret = poldek_pkg_in_array_idx (p, installed, (tn_fn_cmp)pkg_cmp_name);

						if (ret >= 0) {
							struct pkg	*ipkg = NULL;

							ipkg = n_array_nth (installed, ret);

							if (pkg_satisfies_req (ipkg, req, 1))
								break;
						}
					}

					n_array_push (depends, pkg_link (p));
					n_array_push (tmp, pkg_link (p));
					break;
				}
			}
		}
	}

	if (recursive && tmp && n_array_size (tmp) > 0) {
		for (i = 0; i < n_array_size (tmp); i++) {
			struct pkg	*p = n_array_nth (tmp, i);

			do_depends (installed, available, depends, p, backend);
		}
	}

	n_array_free (tmp);
}

static gchar*
package_id_from_pkg (struct pkg *pkg, const gchar *repo, PkBitfield filters)
{
	gchar *evr, *package_id, *poldek_dir;

	g_return_val_if_fail (pkg != NULL, NULL);

	evr = poldek_pkg_evr (pkg);

	if (repo) {
		poldek_dir = g_strdup (repo);
	} else {
		/* when filters contain PK_FILTER_ENUM_NOT_INSTALLED package
		 * can't be marked as installed */
		if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) &&
		    pkg_is_installed (pkg)) {
			poldek_dir = g_strdup ("installed");
		} else {
			if (pkg->pkgdir && pkg->pkgdir->name) {
				poldek_dir = g_strdup (pkg->pkgdir->name);
			} else {
				poldek_dir = g_strdup ("all-avail");
			}
		}
	}

	package_id = pk_package_id_build (pkg->name,
					  evr,
					  pkg_arch (pkg),
					  poldek_dir);

	g_free (evr);
	g_free (poldek_dir);

	return package_id;
}

/**
 * poldek_backend_package:
 */
static void
poldek_backend_package (PkBackend *backend, struct pkg *pkg, PkInfoEnum infoenum, PkBitfield filters)
{
	struct pkguinf *pkgu = NULL;
	gchar *package_id;

	if (infoenum == PK_INFO_ENUM_UNKNOWN) {
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
			infoenum = PK_INFO_ENUM_INSTALLED;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
			infoenum = PK_INFO_ENUM_AVAILABLE;
		} else {
			if (pkg_is_installed (pkg)) {
				infoenum = PK_INFO_ENUM_INSTALLED;
			} else {
				infoenum = PK_INFO_ENUM_AVAILABLE;
			}
		}
	}

	package_id = package_id_from_pkg (pkg, NULL, filters);

	if ((pkgu = pkg_uinf_i18n (backend, pkg))) {
		pk_backend_package (backend, infoenum, package_id, pkguinf_get (pkgu, PKGUINF_SUMMARY));

		pkguinf_free (pkgu);
	} else {
		pk_backend_package (backend, infoenum, package_id, "");
	}

	g_free (package_id);
}

/**
 * poldek_get_pkg_from_package_id:
 */
static struct pkg*
poldek_get_pkg_from_package_id (const gchar *package_id)
{
	struct pkg  *pkg = NULL;
	gchar      **parts = NULL;

	g_return_val_if_fail (package_id != NULL, NULL);

	if ((parts = pk_package_id_split (package_id))) {
		tn_array *packages = NULL;
		gchar    *vr = NULL;

		vr = poldek_get_vr_from_package_id_evr (parts[PK_PACKAGE_ID_VERSION]);

		if ((packages = execute_packages_command ("cd /%s; ls -q %s-%s.%s", parts[PK_PACKAGE_ID_DATA],
										    parts[PK_PACKAGE_ID_NAME],
										    vr,
										    parts[PK_PACKAGE_ID_ARCH]))) {
			if (n_array_size (packages) > 0) {
				/* only one package is needed */
				pkg = pkg_link (n_array_nth (packages, 0));
			}
		}

		g_free (vr);
		g_strfreev (parts);
	}

	return pkg;
}

static tn_array*
do_search_details (const gchar *tree, gchar **values)
{
	guint i;
	tn_array *pkgs = NULL;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (values != NULL, NULL);

	pkgs = execute_packages_command ("%s; search -qsd *%s*", tree, values[0]);

	if (g_strv_length (values) > 1) {
		GString *pkgnames = NULL;

		pkgnames = g_string_new ("");

		for (i = 1; pkgs && i < g_strv_length (values); i++) {
			guint j;

			g_string_truncate (pkgnames, 0);

			/* create string from pkgs names */
			for (j = 0; j < n_array_size (pkgs); j++) {
				struct pkg *pkg = n_array_nth (pkgs, j);

				g_string_append (pkgnames, pkg_id (pkg));
				g_string_append_c (pkgnames, ' ');
			}

			n_array_free (pkgs);

			pkgs = execute_packages_command ("%s; search -qsd \"*%s*\" %s",
							 tree, values[i], pkgnames->str);
		}

		g_string_free (pkgnames, TRUE);
	}

	return pkgs;
}

/**
 * search_package_thread:
 */
static gboolean
search_package_thread (PkBackend *backend)
{
	PkBitfield		filters;
	PkProvidesEnum		provides;
	gchar		       *search_cmd_available = NULL;
	gchar		       *search_cmd_installed = NULL;
	tn_array	       *pkgs = NULL;
	gchar		      **values = NULL;
	gchar		       *search;
	guint mode;

	pb_load_packages (backend);

	mode = pk_backend_get_uint (backend, "mode");
	filters = pk_backend_get_uint (backend, "filters");

	values = pk_backend_get_strv (backend, "search");

	if (values == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
				       "failed to get 'search'");
		goto out;
	}

	/* SearchName */
	if (mode == SEARCH_ENUM_NAME) {
		search = g_strjoinv ("*", values);

		search_cmd_installed = g_strdup_printf ("ls -q *%s*", search);
		search_cmd_available = g_strdup (search_cmd_installed);

		g_free (search);
	/* SearchGroup */
	} else if (mode == SEARCH_ENUM_GROUP) {
		GString        *command;
		guint		i;

		command = g_string_new ("search -qg --perlre /");

		for (i = 0; i < g_strv_length (values); i++) {
			const gchar *regex = NULL;

			regex = pld_group_get_regex_from_text (values[i]);

			if (regex == NULL) {
				pk_backend_error_code (backend, PK_ERROR_ENUM_GROUP_NOT_FOUND,
						       "The group '%s' does not exist.", values[i]);
				g_string_free (command, TRUE);
				goto out;
			}

			if (i > 0)
				g_string_append (command, "\\|");

			g_string_append (command, regex);
		}

		g_string_append_c (command, '/');

		search_cmd_installed = g_string_free (command, FALSE);
		search_cmd_available = g_strdup (search_cmd_installed);
	/* SearchFile */
	} else if (mode == SEARCH_ENUM_FILE) {
		GString *local_pkgs = NULL;
		GString *installed_pkgs = NULL;
		GString *available_pkgs = NULL;
		guint i;

		local_pkgs = g_string_new ("");
		installed_pkgs = g_string_new ("");
		available_pkgs = g_string_new ("");

		for (i = 0; i < g_strv_length (values); i++) {
			if (available_pkgs->len > 0)
				g_string_append (available_pkgs, "\\|");

			if (*values[i] == '/') {
				gchar *pkgid = NULL;

				/* use rpmdb to get local packages (equivalent to: rpm -qf /foo/bar) */
				if ((pkgid = get_pkgid_from_localpath (values[i]))) {
					g_string_append_printf (local_pkgs, " %s", pkgid);
					g_free (pkgid);
				}

				g_string_append_printf (available_pkgs, "^%s$", values[i]);
			} else {

				g_string_append_printf (available_pkgs, ".*%s.*", values[i]);

				if (installed_pkgs->len > 0)
					g_string_append (installed_pkgs, "\\|");

				g_string_append_printf (installed_pkgs, ".*%s.*", values[i]);
			}
		}

		if (installed_pkgs->len > 0) {
			g_string_prepend (installed_pkgs, "search -ql --perlre /");
			g_string_append (installed_pkgs, "/;");
		}

		if (local_pkgs->len > 0)
			g_string_append_printf (installed_pkgs, "ls -q %s", local_pkgs->str);

		g_string_prepend (available_pkgs, "search -ql --perlre /");
		g_string_append_c (available_pkgs, '/');

		g_string_free (local_pkgs, TRUE);

		search_cmd_installed = g_string_free (installed_pkgs, FALSE);
		search_cmd_available = g_string_free (available_pkgs, FALSE);

	/* WhatProvides */
	} else if (mode == SEARCH_ENUM_PROVIDES) {
		search = g_strjoinv ("\\|", values);

		search_cmd_installed = g_strdup_printf ("search -qp --perlre /%s/", search);
		search_cmd_available = g_strdup_printf ("search -qp --perlre /%s/", search);

		g_free (search);
	/* Resolve */
	} else if (mode == SEARCH_ENUM_RESOLVE) {
		gchar **package_ids;
		gchar *packages_str;

		package_ids = pk_backend_get_strv (backend, "package_ids");
		packages_str = g_strjoinv(" ", package_ids);

		search_cmd_installed = g_strdup_printf ("ls -q %s", packages_str);
		search_cmd_available = g_strdup (search_cmd_installed);

		g_free (packages_str);
	}

	if ((search_cmd_installed != NULL && search_cmd_available != NULL) || mode == SEARCH_ENUM_DETAILS) {
		tn_array *installed = NULL;
		tn_array *available = NULL;

		if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
			if (mode == SEARCH_ENUM_DETAILS)
				installed = do_search_details ("cd /installed", values);
			else
				installed = execute_packages_command ("cd /installed; %s", search_cmd_installed);
		}

		if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
			if (mode == SEARCH_ENUM_DETAILS)
				available = do_search_details ("cd /all-avail", values);
			else
				available = execute_packages_command ("cd /all-avail; %s", search_cmd_available);
		}

		/* merge installed and available */
		if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) &&
		    !pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
			pkgs = do_post_search_process (installed, available);

		/* filter out installed packages from available */
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) && available) {
			tn_array *dbpkgs = NULL;
			guint i;

			dbpkgs = poldek_get_installed_packages ();
			pkgs = n_array_new (4, (tn_fn_free)pkg_free, NULL);

			for (i = 0; i < n_array_size (available); i++) {
				struct pkg *pkg = n_array_nth (available, i);

				/* drop installed packages */
				if (!poldek_pkg_in_array (pkg, dbpkgs, (tn_fn_cmp)pkg_cmp_name_evr)) {
					n_array_push (pkgs, pkg_link (pkg));
				}
			}

			n_array_free (dbpkgs);

		} else if (available) {
			pkgs = n_ref (available);

		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) || installed)
			pkgs = n_ref (installed);

		if (installed)
			n_array_free (installed);

		if (available)
			n_array_free (available);
	}

	do_filtering (pkgs, filters);

	if (pkgs && n_array_size (pkgs) > 0) {
		guint	i;

		for (i = 0; i < n_array_size (pkgs); i++) {
			struct pkg *pkg = n_array_nth (pkgs, i);

			if (sigint_reached ())
				break;

			poldek_backend_package (backend, pkg, PK_INFO_ENUM_UNKNOWN, filters);
		}
		n_array_free (pkgs);
	} else {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "Package not found");
	}

	if (sigint_reached ()) {
		switch (mode) {
			case SEARCH_ENUM_NAME:
			case SEARCH_ENUM_GROUP:
			case SEARCH_ENUM_DETAILS:
			case SEARCH_ENUM_FILE:
			case SEARCH_ENUM_RESOLVE:
				pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Search cancelled.");
				break;
			default:
				pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Transaction cancelled.");
		}
	}

	g_free (search_cmd_installed);
	g_free (search_cmd_available);

out:
	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
update_packages_thread (PkBackend *backend)
{
	struct vf_progress	vf_progress;
	gboolean		update_system;
	guint			i, toupdate = 0;
	gchar **package_ids, *command;
	GString *cmd;

	update_system = pk_backend_get_bool (backend, "update_system");
	/* FIXME: support only_trusted */
	package_ids = pk_backend_get_strv (backend, "package_ids");

	/* sth goes wrong. package_ids has to be set in UpdatePackages */
	if (update_system == FALSE && package_ids == NULL) {
		g_warning ("package_ids cannot be NULL in UpdatePackages method.");
		pk_backend_finished (backend);
		return TRUE;
	}

	setup_vf_progress (&vf_progress, backend);

	pb_load_packages (backend);

	pk_backend_set_status (backend, PK_STATUS_ENUM_DEP_RESOLVE);
	pb_error_clean ();

	cmd = g_string_new ("upgrade ");

	if (update_system) {
		tn_array *packages = NULL;

		if ((packages = execute_packages_command ("cd /all-avail; ls -q -u")) != NULL) {
			/* UpdateSystem updates to the newest available packages */
			do_newest (packages);

			for (i = 0; i < n_array_size (packages); i++) {
				struct pkg *pkg = n_array_nth (packages, i);

				/* don't try to update blocked packages */
				if (!(pkg->flags & PKG_HELD)) {
					g_string_append_printf (cmd, "%s-%s-%s.%s ", pkg->name,
								pkg->ver, pkg->rel, pkg_arch (pkg));

					toupdate++;
				}
			}

			n_array_free (packages);
		}
	} else {
		for (i = 0; i < g_strv_length (package_ids); i++) {
			struct pkg *pkg;

			pkg = poldek_get_pkg_from_package_id (package_ids[i]);

			g_string_append_printf (cmd, "%s-%s-%s.%s ", pkg->name,
						pkg->ver, pkg->rel, pkg_arch (pkg));

			toupdate++;

			pkg_free (pkg);
		}
	}

	command = g_string_free (cmd, FALSE);

	if (toupdate > 0) {
		if (execute_command (command) == FALSE) {
			pb_error_show (backend, PK_ERROR_ENUM_TRANSACTION_ERROR);
		}
	}

	poldek_backend_percentage_data_destroy (backend);

	g_free (command);

	pk_backend_finished (backend);
	return TRUE;
}

static void
pb_load_packages (PkBackend *backend)
{
	gboolean	allow_cancel = pk_backend_get_allow_cancel (backend);

	/* this operation can't be cancelled, so if enabled, set allow_cancel to FALSE */
	if (allow_cancel)
		poldek_backend_set_allow_cancel (backend, FALSE, FALSE);

	/* load information about installed and available packages */
	poclidek_load_packages (cctx, POCLIDEK_LOAD_ALL);

	if (allow_cancel)
		poldek_backend_set_allow_cancel (backend, TRUE, FALSE);
}

static void
pb_error_show (PkBackend *backend, PkErrorEnum errorcode)
{
	if (sigint_reached()) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Action cancelled.");
		return;
	}

	/* Before emiting error_code try to find the most suitable PkErrorEnum */
	if (g_strrstr (pberror->tslog->str, " unresolved depend") != NULL)
		errorcode = PK_ERROR_ENUM_DEP_RESOLUTION_FAILED;
	else if (g_strrstr (pberror->tslog->str, " conflicts") != NULL)
		errorcode = PK_ERROR_ENUM_FILE_CONFLICTS;

	pk_backend_error_code (backend, errorcode, "%s", pberror->tslog->str);
}

/**
 * pb_error_check:
 *
 * When we try to install already installed package, poldek won't report any error
 * just show message like 'liferea-1.4.11-2.i686: equal version installed, skipped'.
 * This function checks if it happens and if yes, emits error_code and returns TRUE.
 **/
static gboolean
pb_error_check (PkBackend *backend)
{
	PkErrorEnum	errorcode = PK_ERROR_ENUM_UNKNOWN;

	if (g_strrstr (pberror->tslog->str, " version installed, skipped") != NULL)
		errorcode = PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED;

	if (errorcode != PK_ERROR_ENUM_UNKNOWN) {
		pk_backend_error_code (backend, errorcode, pberror->tslog->str);
		return TRUE;
	}

	return FALSE;
}

static void
pb_error_clean (void)
{
	g_free (pberror->vfffmsg);
	pberror->vfffmsg = NULL;

	pberror->tslog = g_string_erase (pberror->tslog, 0, -1);
	pberror->rpmstate = PB_RPM_STATE_ENUM_NONE;
}

static gint
pkg_n_strncmp (struct pkg *p, gchar *name)
{
	g_return_val_if_fail (p != NULL, -1);
	g_return_val_if_fail (p->name != NULL, -1);
	g_return_val_if_fail (name != NULL, 1);

	return strncmp (p->name, name, strlen (name));
}

static void
show_rpm_progress (PkBackend *backend, gchar *message)
{
	g_return_if_fail (message != NULL);

	if (pberror->rpmstate & PB_RPM_STATE_ENUM_REPACKAGING) {
		g_debug ("repackaging '%s'", message);
	} else if (pberror->rpmstate & PB_RPM_STATE_ENUM_INSTALLING) {
		tn_array *upkgs, *ipkgs, *rpkgs, *arr = NULL;
		guint to_install;
		PkInfoEnum pkinfo;
		gint n = -2;

		g_debug ("installing or updating '%s'", message);

		to_install = pk_backend_get_uint (backend, "to_install");

		ipkgs = pk_backend_get_pointer (backend, "to_install_pkgs");
		upkgs = pk_backend_get_pointer (backend, "to_update_pkgs");
		rpkgs = pk_backend_get_pointer (backend, "to_remove_pkgs");

		/* emit remove for packages marked for removal */
		if (rpkgs) {
			size_t i;

			/* XXX: don't release rpkgs array here! */
			for (i = 0; i < n_array_size (rpkgs); i++) {
				struct pkg *pkg = n_array_nth (rpkgs, i);

				poldek_backend_package (backend, pkg, PK_INFO_ENUM_REMOVING, PK_FILTER_ENUM_NONE);

				n_array_remove_nth (rpkgs, i);
			}
		}

		if (upkgs) {
			n = n_array_bsearch_idx_ex (upkgs, message, (tn_fn_cmp)pkg_n_strncmp);
		}

		if (n >= 0) {
			pkinfo = PK_INFO_ENUM_UPDATING;
			arr = upkgs;
		} else if (ipkgs) {
			n = n_array_bsearch_idx_ex (ipkgs, message, (tn_fn_cmp)pkg_n_strncmp);

			if (n >= 0) {
				pkinfo = PK_INFO_ENUM_INSTALLING;
				arr = ipkgs;
			}
		}

		if (arr) {
			struct pkg *pkg = n_array_nth (arr, n);
			guint in_arrays = 0;

			poldek_backend_package (backend, pkg, pkinfo, PK_FILTER_ENUM_NONE);

			n_array_remove_nth (arr, n);

			if (upkgs) {
				in_arrays += n_array_size (upkgs);
			}
			if (ipkgs) {
				in_arrays += n_array_size (ipkgs);
			}

			pk_backend_set_percentage (backend, (gint)(((float)(to_install - in_arrays) / (float)to_install) * 100));
		}
	}
}

/* Returns NULL if not found */
static gchar*
get_filename_from_message (char *message)
{
	gchar *msg = NULL, *p;

	if ((p = strchr (message, ':')) == NULL)
		return NULL;

	/* check if it's really rpm progress
	 * example: ' 4:foo    ###'
	 */
	if (g_ascii_isdigit (*(p - 1))) {
		p++;

		msg = p;

		while (p) {
			if (*p == '#' || g_ascii_isspace (*p)) {
				*p = '\0';
				break;
			}

			p++;
		}
	}

	return msg;
}

static void
poldek_backend_log (void *data, int pri, char *message)
{
	PkBackend *backend = (PkBackend*)data;

	/* skip messages that we don't want to show */
	if (g_str_has_prefix (message, "Nothing")) // 'Nothing to do'
		return;
	if (g_str_has_prefix (message, "There we")) // 'There were errors'
		return;

	/* catch vfff messages */
	if (g_str_has_prefix (message, "vfff: ")) {
		if (g_str_has_prefix (message + 6, "Inter")) // 'Interrupted system call'
			return;
		else if (g_str_has_prefix (message + 6, "connection cancell")) // 'connection cancelled'
			return;

		/* check if this message was already showed */
		if (pberror->vfffmsg) {
			if (strcmp (pberror->vfffmsg, message) == 0)
				return;
			else {
				g_free (pberror->vfffmsg);
				pberror->vfffmsg = NULL;
			}
		}

		pberror->vfffmsg = g_strdup (message);

		// 'vfff: unable to connect to ftp.pld-linux.org:21: Connection refused'
		pk_backend_message (backend, PK_MESSAGE_ENUM_CONNECTION_REFUSED, "%s", message);
	} else {
		if (pri & LOGERR) {
			g_string_append_printf (pberror->tslog, "error: %s", message);
		} else {
			g_string_append_printf (pberror->tslog, "%s", message);
		}
	}

	if (strstr (message, "Preparing...")) {
		pberror->rpmstate |= PB_RPM_STATE_ENUM_INSTALLING;

		/* we shouldn't cancel install / update proccess */
		poldek_backend_set_allow_cancel (backend, FALSE, FALSE);
	} else if (strstr (message, "Repackaging...")) {
		pberror->rpmstate |= PB_RPM_STATE_ENUM_REPACKAGING;

		pk_backend_set_status (backend, PK_STATUS_ENUM_REPACKAGING);
	} else if (strstr (message, "Upgrading...")) {
		pberror->rpmstate &= (~PB_RPM_STATE_ENUM_REPACKAGING);
	}
	if (pberror->rpmstate != PB_RPM_STATE_ENUM_NONE) {
		gchar *fn;

		if ((fn = get_filename_from_message (message)) == NULL)
			return;

		if ((pberror->rpmstate & PB_RPM_STATE_ENUM_REPACKAGING) == FALSE) {
			guint ts_type = pk_backend_get_uint (backend, "ts_type");

			/* set proper status */
			if (ts_type == TS_TYPE_ENUM_INSTALL) {
				pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
			} else if (ts_type == TS_TYPE_ENUM_UPDATE) {
				pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
			}
		}

		show_rpm_progress (backend, fn);
	}
}

static void
poldek_backend_set_allow_cancel (PkBackend *backend, gboolean allow_cancel, gboolean reset)
{
	if (reset)
		sigint_reset ();

	pk_backend_set_allow_cancel (backend, allow_cancel);
}

static void
poldek_backend_percentage_data_create (PkBackend *backend)
{
	PercentageData *data;

	data = g_new0 (PercentageData, 1);
	pk_backend_set_pointer (backend, "percentage_ptr", data);
}

static void
poldek_backend_percentage_data_destroy (PkBackend *backend)
{
	PercentageData *data;
	tn_array *upkgs, *ipkgs, *rpkgs;

	data = (gpointer) pk_backend_get_pointer (backend, "percentage_ptr");

	upkgs = (gpointer) pk_backend_get_pointer (backend, "to_update_pkgs");
	ipkgs = (gpointer) pk_backend_get_pointer (backend, "to_install_pkgs");
	rpkgs = (gpointer) pk_backend_get_pointer (backend, "to_remove_pkgs");

	n_array_cfree (&upkgs);
	n_array_cfree (&ipkgs);
	n_array_cfree (&rpkgs);

	g_free (data);
}

static void
do_poldek_init (PkBackend *backend)
{
	poldeklib_init ();

	ctx = poldek_new (0);

	poldek_load_config (ctx, "/etc/poldek/poldek.conf", NULL, 0);

	poldek_setup (ctx);

	cctx = poclidek_new (ctx);

	poldek_set_verbose (1);
	/* disable LOGFILE and LOGTTY logging */
	poldek_configure (ctx, POLDEK_CONF_LOGFILE, NULL);
	poldek_configure (ctx, POLDEK_CONF_LOGTTY, NULL);

	poldek_log_set_appender ("PackageKit", (void *)backend, NULL, 0, (poldek_vlog_fn)poldek_backend_log);

	/* disable unique package names */
	poldek_configure (ctx, POLDEK_CONF_OPT, POLDEK_OP_UNIQN, 0);

	poldek_configure (ctx, POLDEK_CONF_OPT, POLDEK_OP_LDALLDESC, 1);

	/* poldek has to ask. Otherwise callbacks won't be used */
	poldek_configure (ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_INST, 1);
	poldek_configure (ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_UNINST, 1);
	/* (...), but we don't need choose_equiv callback */
	poldek_configure (ctx, POLDEK_CONF_OPT, POLDEK_OP_EQPKG_ASKUSER, 0);

	poldek_configure (ctx, POLDEK_CONF_TSCONFIRM_CB, ts_confirm, backend);
	/* Install all suggested packages by default */
	poldek_configure (ctx, POLDEK_CONF_CHOOSESUGGESTS_CB, suggests_callback, NULL);

	sigint_init ();
}

static void
do_poldek_destroy (PkBackend *backend)
{
	sigint_destroy ();

	poclidek_free (cctx);
	poldek_free (ctx);

	poldeklib_destroy ();
}

static void
poldek_reload (PkBackend *backend, gboolean load_packages) {
	do_poldek_destroy (backend);
	do_poldek_init (backend);

	if (load_packages)
		pb_load_packages (backend);
}

/**
 * backend_initalize:
 */
static void
backend_initalize (PkBackend *backend)
{
	g_debug ("backend initalize start");

	clv = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)n_array_free);

	pberror = g_new0 (PbError, 1);
	pberror->tslog = g_string_new ("");

	do_poldek_init (backend);

	g_debug ("backend initalize end");
}
/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	do_poldek_destroy (backend);

	/* release PbError struct */
	g_free (pberror->vfffmsg);
	g_string_free (pberror->tslog, TRUE);

	g_hash_table_destroy (clv);

	g_free (pberror);
}

/**
 * backend_download_packages:
 */
static gboolean
backend_download_packages_thread (PkBackend *backend)
{
	PercentageData *pd = pk_backend_get_pointer (backend, "percentage_ptr");
	struct poldek_ts *ts;
	struct vf_progress vf_progress;
	tn_array *pkgs;
	gchar **package_ids;
	const gchar *destdir;
	size_t i;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	destdir = pk_backend_get_string (backend, "directory");

	pkgs = n_array_new (10, (tn_fn_free)pkg_free, NULL);

	ts = poldek_ts_new (ctx, 0);

	setup_vf_progress (&vf_progress, backend);

	pb_load_packages (backend);

	for (i = 0; i < g_strv_length (package_ids); i++) {
		struct pkg *pkg = poldek_get_pkg_from_package_id (package_ids[i]);

		n_array_push (pkgs, pkg_link (pkg));

		pkg_free (pkg);
	}

	pd->bytesget = 0;
	pd->bytesdownload = poldek_get_bytes_to_download (ts, pkgs);

	if (packages_fetch (poldek_get_pmctx (ts->ctx), pkgs, destdir, 1)) {
		GString *filelist = NULL;
		gchar *result = NULL;

		filelist = g_string_new ("");

		/* emit the file list we downloaded */
		for (i = 0; i < n_array_size (pkgs); i++) {
			struct pkg *pkg = n_array_nth (pkgs, i);
			gchar buf[256];

			if (i > 0)
			    g_string_append_c (filelist, ';');

			g_string_append_printf (filelist, "%s/%s", destdir,
						pkg_filename (pkg, buf, sizeof(buf)));
		}

		result = g_string_free (filelist, FALSE);

		pk_backend_files (backend, NULL, result);

		g_free (result);
	}

	poldek_ts_free (ts);

	poldek_backend_percentage_data_destroy (backend);

	pk_backend_finished (backend);

	return TRUE;
}

static void
backend_download_packages (PkBackend *backend, gchar **package_ids,
			   const gchar *directory)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	poldek_backend_set_allow_cancel (backend, FALSE, TRUE);
	pb_error_clean ();

	poldek_backend_percentage_data_create (backend);
	pk_backend_thread_create (backend, backend_download_packages_thread);
}

/**
 * backend_get_groups:
 **/
static PkBitfield
backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_GROUP_ENUM_ACCESSORIES,
		PK_GROUP_ENUM_ADMIN_TOOLS,
		PK_GROUP_ENUM_COMMUNICATION,
		PK_GROUP_ENUM_DOCUMENTATION,
		PK_GROUP_ENUM_EDUCATION,
		PK_GROUP_ENUM_FONTS,
		PK_GROUP_ENUM_GAMES,
		PK_GROUP_ENUM_GRAPHICS,
		PK_GROUP_ENUM_LOCALIZATION,
		PK_GROUP_ENUM_MULTIMEDIA,
		PK_GROUP_ENUM_NETWORK,
		PK_GROUP_ENUM_OFFICE,
		PK_GROUP_ENUM_OTHER,
		PK_GROUP_ENUM_PROGRAMMING,
		PK_GROUP_ENUM_PUBLISHING,
		PK_GROUP_ENUM_SERVERS,
		PK_GROUP_ENUM_SYSTEM,
		-1);
}

/**
 * backend_get_filters:
 */
static PkBitfield
backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_NEWEST,
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		-1);
}

/**
 * backend_get_mime_types:
 */
static gchar *
backend_get_mime_types (PkBackend *backend)
{
	return g_strdup ("application/x-rpm");
}

/**
 * backend_get_cancel:
 **/
static void
backend_get_cancel (PkBackend *backend)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_CANCEL);

	sigint_emit ();
}

/**
 * backend_get_depends:
 */
static gboolean
backend_get_depends_thread (PkBackend *backend)
{
	struct pkg	*pkg;
	tn_array	*deppkgs, *available, *installed;
	size_t		i;
	gchar **package_ids;

	pb_load_packages (backend);

	deppkgs = n_array_new (2, NULL, NULL);

	installed = poldek_get_installed_packages ();
	available = poldek_get_avail_packages (ctx);
	package_ids = pk_backend_get_strv (backend, "package_ids");

	pkg = poldek_get_pkg_from_package_id (package_ids[0]);

	do_depends (installed, available, deppkgs, pkg, backend);

	n_array_sort_ex(deppkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);

	for (i = 0; i < n_array_size (deppkgs); i++) {
		struct pkg	*p = n_array_nth (deppkgs, i);

		poldek_backend_package (backend, p, PK_INFO_ENUM_UNKNOWN, pk_backend_get_uint (backend, "filters"));
	}

	pkg_free (pkg);

	n_array_free (deppkgs);
	n_array_free (available);
	n_array_free (installed);

	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, FALSE, TRUE);
	pb_error_clean ();

	pk_backend_thread_create (backend, backend_get_depends_thread);
}

/**
 * backend_get_details:
 */
static gboolean
backend_get_details_thread (PkBackend *backend)
{
	gchar **package_ids;
	guint n;

	package_ids = pk_backend_get_strv (backend, "package_ids");

	pb_load_packages (backend);

	for (n = 0; n < g_strv_length (package_ids); n++) {
		struct pkg *pkg = NULL;

		if ((pkg = poldek_get_pkg_from_package_id (package_ids[n])) != NULL) {
			struct pkguinf *pkgu = NULL;
			PkGroupEnum group;

			group = pld_group_to_enum (pkg_group (pkg));

			if ((pkgu = pkg_uinf_i18n (backend, pkg)) != NULL) {
				pk_backend_details (backend,
							package_ids[n],
							pkguinf_get (pkgu, PKGUINF_LICENSE),
							group,
							pkguinf_get (pkgu, PKGUINF_DESCRIPTION),
							pkguinf_get (pkgu, PKGUINF_URL),
							pkg->fsize);
				pkguinf_free (pkgu);
			} else {
				pk_backend_details (backend,
							package_ids[n],
							"",
							group,
							"",
							"",
							pkg->fsize);
			}

			pkg_free (pkg);
		}
	}

	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_get_details (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, FALSE, TRUE);
	pb_error_clean ();

	pk_backend_thread_create (backend, backend_get_details_thread);
}

/**
 * backend_get_files:
 */
static gboolean
backend_get_files_thread (PkBackend *backend)
{
	gchar **package_ids;
	size_t n;

	package_ids = pk_backend_get_strv (backend, "package_ids");

	pb_load_packages (backend);

	for (n = 0; n < g_strv_length (package_ids); n++) {
		struct pkg *pkg;

		pkg = poldek_get_pkg_from_package_id (package_ids[n]);

		if (pkg != NULL) {
			struct pkgflist *flist = pkg_get_flist (pkg);
			GString *filelist;
			gchar *result;
			const gchar *sep;
			gint i, j;

			sep = "";

			if (flist == NULL) {
				pkg_free (pkg);
				continue;
			}

			filelist = g_string_new ("");

			for (i = 0; i < n_tuple_size (flist->fl); i++) {
				struct pkgfl_ent *flent = n_tuple_nth (flist->fl, i);
				gchar *dirname;

				dirname = g_strdup_printf ("%s%s", *flent->dirname == '/' ? "" : "/", flent->dirname);

				for (j = 0; j < flent->items; j++) {
					struct flfile *f = flent->files[j];

					if (strcmp (dirname, "/") == 0)
						g_string_append_printf (filelist, "%s/%s", sep, f->basename);
					else
						g_string_append_printf (filelist, "%s%s/%s", sep, dirname, f->basename);

					sep = ";";
				}
				g_free (dirname);
			}

			result = g_string_free (filelist, FALSE);

			pk_backend_files (backend, package_ids[n], result);

			g_free (result);

			pkg_free (pkg);
		}
	}

	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_get_files (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, FALSE, TRUE);
	pb_error_clean ();

	pk_backend_thread_create (backend, backend_get_files_thread);
}

/**
 * backend_get_packages:
 **/
static gboolean
backend_get_packages_thread (PkBackend *backend)
{
	PkBitfield	filters;
	tn_array       *installed = NULL;
	tn_array       *available = NULL;
	tn_array       *packages = NULL;
	guint		i;

	filters = pk_backend_get_uint (backend, "filters");

	pk_backend_set_percentage (backend, 0);

	pb_load_packages (backend);

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) == FALSE)
		installed = poldek_get_installed_packages ();

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) == FALSE)
		available = poldek_get_avail_packages (ctx);

	pk_backend_set_percentage (backend, 4);

	packages = do_post_search_process (installed, available);

	do_filtering (packages, filters);

	pk_backend_set_percentage (backend, 10);

	if (packages != NULL) {
		for (i = 0; i < n_array_size (packages); i++) {
			struct pkg     *pkg = n_array_nth (packages, i);

			if (sigint_reached ())
				break;

			pk_backend_set_percentage (backend, (guint)(10 + (90 * (float)(i + 1) / n_array_size (packages))));

			poldek_backend_package (backend, pkg, PK_INFO_ENUM_UNKNOWN, filters);
		}
	}

	if (sigint_reached ())
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "");
	else
		pk_backend_set_percentage (backend, 100);

	n_array_cfree (&installed);
	n_array_cfree (&available);
	n_array_cfree (&packages);

	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();

	pk_backend_thread_create (backend, backend_get_packages_thread);
}

/**
 * backend_get_requires:
 */
static gboolean
backend_get_requires_thread (PkBackend *backend)
{
	struct pkg	*pkg;
	tn_array	*reqpkgs, *available, *installed;
	size_t		i;
	gchar **package_ids;

	pb_load_packages (backend);

	reqpkgs = n_array_new (2, NULL, NULL);

	package_ids = pk_backend_get_strv (backend, "package_ids");
	pkg = poldek_get_pkg_from_package_id (package_ids[0]);
	installed = poldek_get_installed_packages ();
	available = poldek_get_avail_packages (ctx);

	do_requires (installed, available, reqpkgs, pkg, backend);

	/* sort output */
	n_array_sort_ex(reqpkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);

	for (i = 0; i < n_array_size (reqpkgs); i++) {
		struct pkg	*p = n_array_nth (reqpkgs, i);

		poldek_backend_package (backend, p, PK_INFO_ENUM_UNKNOWN, pk_backend_get_uint (backend, "filters"));
	}

	n_array_free (reqpkgs);
	n_array_free (installed);
	n_array_free (available);

	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_get_requires (PkBackend	*backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, FALSE, TRUE);
	pb_error_clean ();

	pk_backend_thread_create (backend, backend_get_requires_thread);
}

/**
 * backend_get_update_detail:
 */
static gchar*
get_obsoletedby_pkg (struct pkg *pkg)
{
	tn_array *dbpkgs;
	GString *obsoletes = NULL;
	size_t i;

	g_return_val_if_fail (pkg != NULL, NULL);

	/* get installed packages */
	dbpkgs = poclidek_get_dent_packages (cctx, POCLIDEK_INSTALLEDDIR);

	if (dbpkgs == NULL)
		return NULL;

	for (i = 0; i < n_array_size (dbpkgs); i++) {
		struct pkg *dbpkg = n_array_nth (dbpkgs, i);

		if (pkg_caps_obsoletes_pkg_caps (pkg, dbpkg)) {
			gchar *package_id = package_id_from_pkg (dbpkg, "installed", 0);

			if (obsoletes) {
				obsoletes = g_string_append_c (obsoletes, '^');
				obsoletes = g_string_append (obsoletes, package_id);
			} else {
				obsoletes = g_string_new (package_id);
			}

			g_free (package_id);
		}
	}

	n_array_free (dbpkgs);

	return obsoletes ? g_string_free (obsoletes, FALSE) : NULL;
}

static gboolean
backend_get_update_detail_thread (PkBackend *backend)
{
	gchar **package_ids = NULL;
	guint n;

	package_ids = pk_backend_get_strv (backend, "package_ids");

	pb_load_packages (backend);

	for (n = 0; n < g_strv_length (package_ids); n++) {
		tn_array  *packages = NULL;
		gchar    **parts = NULL;

		if ((parts = pk_package_id_split (package_ids[n])) == NULL)
			continue;

		if ((packages = execute_packages_command ("cd /installed; ls -q %s", parts[PK_PACKAGE_ID_NAME])) != NULL) {
			struct pkg *pkg = NULL;
			struct pkg *upkg = NULL;

			/* get one package */
			pkg = n_array_nth (packages, 0);

			if (strcmp (pkg->name, parts[PK_PACKAGE_ID_NAME]) == 0) {
				gchar *updates = NULL;
				gchar *obsoletes = NULL;
				gchar *cve_url = NULL;
				const gchar *changes = NULL;
				tn_array *cves = NULL;
				struct pkguinf *upkg_uinf = NULL;

				updates = package_id_from_pkg (pkg, "installed", 0);

				upkg = poldek_get_pkg_from_package_id (package_ids[n]);

				obsoletes = get_obsoletedby_pkg (upkg);

				if ((upkg_uinf = pkg_uinf (upkg)) != NULL) {
					changes = pkguinf_get_changelog (upkg_uinf, pkg->btime);
				}

				if ((cves = poldek_pkg_get_cves_from_pld_changelog (upkg, pkg->btime))) {
					GString *string;
					guint i;

					string = g_string_new ("");

					for (i = 0; i < n_array_size (cves); i++) {
						gchar *cve = n_array_nth (cves, i);

						g_string_append_printf (string,
									"http://nvd.nist.gov/nvd.cfm?cvename=%s;%s",
									cve, cve);

						if ((i + 1) < n_array_size (cves))
							g_string_append_printf (string, ";");
					}

					cve_url = g_string_free (string, FALSE);
				}

				pk_backend_update_detail (backend,
							  package_ids[n],
							  updates,
							  obsoletes ? obsoletes : "",
							  "",
							  "",
							  cve_url ? cve_url : "",
							  PK_RESTART_ENUM_NONE,
							  "", changes, PK_UPDATE_STATE_ENUM_UNKNOWN, NULL, NULL);

				g_free (updates);
				g_free (obsoletes);
				g_free (cve_url);

				n_array_cfree (&cves);
			}

			n_array_free (packages);
		} else {
			pk_backend_update_detail (backend,
						  package_ids[n],
						  "",
						  "",
						  "",
						  "",
						  "",
						  PK_RESTART_ENUM_NONE,
						  "", NULL, PK_UPDATE_STATE_ENUM_UNKNOWN, NULL, NULL);
		}

		g_strfreev (parts);
	}

	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, FALSE, TRUE);
	pb_error_clean ();

	pk_backend_thread_create (backend, backend_get_update_detail_thread);
}

/**
 * backend_get_updates:
 */
static gboolean
backend_get_updates_thread (PkBackend *backend)
{
	tn_array *packages = NULL;

	pb_load_packages (backend);

	if ((packages = execute_packages_command ("cd /all-avail; ls -q -u")) != NULL) {
		tn_array *secupgrades = NULL;
		guint i;

		/* GetUpdates returns only the newest packages */
		do_newest (packages);

		secupgrades = poldek_get_security_updates ();

		for (i = 0; i < n_array_size (packages); i++) {
			struct pkg *pkg = n_array_nth (packages, i);

			if (sigint_reached ())
				break;

			/* mark held packages as blocked */
			if (pkg->flags & PKG_HELD)
				poldek_backend_package (backend, pkg, PK_INFO_ENUM_BLOCKED, PK_FILTER_ENUM_NONE);
			else if (poldek_pkg_in_array (pkg, secupgrades, (tn_fn_cmp)pkg_cmp_name_evr))
				poldek_backend_package (backend, pkg, PK_INFO_ENUM_SECURITY, PK_FILTER_ENUM_NONE);
			else
				poldek_backend_package (backend, pkg, PK_INFO_ENUM_NORMAL, PK_FILTER_ENUM_NONE);
		}

		n_array_cfree (&secupgrades);
		n_array_free (packages);
	}

	if (sigint_reached ())
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Action cancelled.");

	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();

	pk_backend_thread_create (backend, backend_get_updates_thread);
}

/**
 * backend_install_packages:
 */
static gboolean
backend_install_packages_thread (PkBackend *backend)
{
	gchar			*command;
	struct vf_progress	vf_progress;
	gchar **package_ids;
	GString *cmd;
	size_t i;

	pk_backend_set_uint (backend, "ts_type", TS_TYPE_ENUM_INSTALL);

	/* FIXME: support only_trusted */
	package_ids = pk_backend_get_strv (backend, "package_ids");

	setup_vf_progress (&vf_progress, backend);

	pb_load_packages (backend);

	cmd = g_string_new ("install ");

	/* prepare command */
	for (i = 0; i < g_strv_length (package_ids); i++) {
		gchar	*nvra = poldek_get_nvra_from_package_id (package_ids[i]);

		g_string_append_printf (cmd, "%s ", nvra);

		g_free (nvra);
	}

	command = g_string_free (cmd, FALSE);

	pk_backend_set_status (backend, PK_STATUS_ENUM_DEP_RESOLVE);

	if (execute_command (command)) {
		pb_error_check (backend);
	} else {
		pb_error_show (backend, PK_ERROR_ENUM_TRANSACTION_ERROR);
	}

	g_free (command);

	poldek_backend_percentage_data_destroy (backend);

	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install package when offline!");
		pk_backend_finished (backend);
		return;
	}

	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();

	poldek_backend_percentage_data_create (backend);
	pk_backend_thread_create (backend, backend_install_packages_thread);
}

/**
 * FIXME: force currently omited
 * backend_refresh_cache:
 */
static gboolean
backend_refresh_cache_thread (PkBackend *backend)
{
	tn_array		*sources = NULL;
	struct vf_progress	vfpro;
	PercentageData *pd = pk_backend_get_pointer (backend, "percentage_ptr");

	setup_vf_progress (&vfpro, backend);

	pk_backend_set_percentage (backend, 1);

	sources = poldek_get_sources (ctx);

	if (sources) {
		size_t	i;

		pk_backend_set_uint (backend, "ts_type", TS_TYPE_ENUM_REFRESH_CACHE);
		pd->step = 0;
		pd->nsources = 0;

		for (i = 0; i < n_array_size (sources); i++) {
			struct source	*src = n_array_nth (sources, i);

			if (src->flags & PKGSOURCE_NOAUTOUP)
				continue;
			else
				pd->nsources++;
		}

		for (i = 0; i < n_array_size (sources); i++) {
			struct source	*src = n_array_nth (sources, i);

			if (src->flags & PKGSOURCE_NOAUTOUP)
				continue;

			if (sigint_reached ())
				break;

			source_update (src, 0);
			pd->step++;
		}
		n_array_free (sources);
	}

	poldek_reload (backend, TRUE);

	pk_backend_set_percentage (backend, 100);

	poldek_backend_percentage_data_destroy (backend);

	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache when offline!");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();

	poldek_backend_percentage_data_create (backend);
	pk_backend_thread_create (backend, backend_refresh_cache_thread);
}

/**
 * backend_remove_packages:
 */
static gboolean
backend_remove_packages_thread (PkBackend *backend)
{
	GString *cmd;
	gchar *command;
	gchar **package_ids;
	size_t i;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	pb_load_packages (backend);

	cmd = g_string_new ("uninstall ");

	/* prepare command */
	for (i = 0; i < g_strv_length (package_ids); i++) {
		gchar	*nvra = poldek_get_nvra_from_package_id (package_ids[i]);

		g_string_append_printf (cmd, "%s ", nvra);

		g_free (nvra);
	}

	command = g_string_free (cmd, FALSE);

	pk_backend_set_status (backend, PK_STATUS_ENUM_DEP_RESOLVE);

	if (execute_command (command) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE, pberror->tslog->str);
	}

	g_free (command);

	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	poldek_backend_set_allow_cancel (backend, FALSE, TRUE);
	pb_error_clean ();
	pk_backend_thread_create (backend, backend_remove_packages_thread);
}

/**
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, PkBitfield filters, gchar **packages)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);

	pk_backend_set_uint (backend, "mode", SEARCH_ENUM_RESOLVE);
	pk_backend_thread_create (backend, search_package_thread);
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();
	pk_backend_set_uint (backend, "mode", SEARCH_ENUM_DETAILS);
	pk_backend_thread_create (backend, search_package_thread);
}

/**
 * backend_search_files:
 */
static void
backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();
	pk_backend_set_uint (backend, "mode", SEARCH_ENUM_FILE);
	pk_backend_thread_create (backend, search_package_thread);
}

/**
 * backend_search_groups:
 */
static void
backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();
	pk_backend_set_uint (backend, "mode", SEARCH_ENUM_GROUP);
	pk_backend_thread_create (backend, search_package_thread);
}

/**
 * backend_search_names:
 */
static void
backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();
	pk_backend_set_uint (backend, "mode", SEARCH_ENUM_NAME);
	pk_backend_thread_create (backend, search_package_thread);
}

/**
 * backend_update_packages:
 */
static void
backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot update packages when offline!");
		pk_backend_finished (backend);
		return;
	}

	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();

	poldek_backend_percentage_data_create (backend);
	pk_backend_set_uint (backend, "ts_type", TS_TYPE_ENUM_UPDATE);
	pk_backend_thread_create (backend, update_packages_thread);
}

/**
 * backend_update_system:
 **/
static void
backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot update system when offline!");
		pk_backend_finished (backend);
		return;
	}

	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();

	poldek_backend_percentage_data_create (backend);
	pk_backend_set_uint (backend, "ts_type", TS_TYPE_ENUM_UPDATE);
	pk_backend_set_bool (backend, "update_system", TRUE);
	pk_backend_thread_create (backend, update_packages_thread);
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	tn_array *sources = NULL;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, FALSE, TRUE);
	pb_error_clean ();

	sources = poldek_get_sources (ctx);

	if (sources) {
		size_t i;

		for (i = 0; i < n_array_size (sources); i++) {
			struct source *src = n_array_nth (sources, i);
			gboolean enabled = TRUE;

			if (src->flags & PKGSOURCE_NOAUTO)
				enabled = FALSE;

			pk_backend_repo_detail (backend, src->path, src->name, enabled);
		}

		n_array_free (sources);
	}

	pk_backend_finished (backend);
}

/**
 * backend_what_provides:
 **/
static void
backend_what_provides (PkBackend *backend, PkBitfield filters, PkProvidesEnum provides, gchar **values)
{
	GPtrArray *array = NULL;
	gchar **search = NULL;
	guint i;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();

	pk_backend_set_uint (backend, "mode", SEARCH_ENUM_PROVIDES);

	/* prepare array of commands */
	array = g_ptr_array_new_with_free_func (g_free);

	for (i = 0; i < g_strv_length (values); i++) {
		if (provides == PK_PROVIDES_ENUM_ANY) {
			g_ptr_array_add (array, g_strdup_printf ("%s", values[i]));
			g_ptr_array_add (array, g_strdup_printf ("gstreamer0.10\\(%s\\)", values[i]));
			g_ptr_array_add (array, g_strdup_printf ("mimetype\\(%s\\)", values[i]));
		} else if (provides == PK_PROVIDES_ENUM_CODEC) {
			g_ptr_array_add (array, g_strdup_printf ("gstreamer0.10\\(%s\\)", values[i]));
		} else if (provides == PK_PROVIDES_ENUM_MIMETYPE) {
			g_ptr_array_add (array, g_strdup_printf ("mimetype\\(%s\\)", values[i]));
		} else {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PROVIDE_TYPE_NOT_SUPPORTED,
					       "provide type '%s' not supported",
					       pk_provides_enum_to_text (provides));
		}
	}

	search = pk_ptr_array_to_strv (array);
	pk_backend_set_strv (backend, "search", search);
	pk_backend_thread_create (backend, search_package_thread);
	g_strfreev (search);
	g_ptr_array_unref (array);
}

static gboolean do_simulate_packages (PkBackend *backend)
{
	struct poclidek_rcmd *rcmd = NULL;
	struct poldek_ts     *ts = NULL;
	GString      *buf = NULL;
	gchar        *cmd = NULL;
	gchar       **package_ids = NULL;
	const gchar  *command = NULL;
	guint         i;
	guint         ts_type;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	command = pk_backend_get_string (backend, "command");
	ts_type = pk_backend_get_uint (backend, "ts_type");

	pk_backend_set_status (backend, PK_STATUS_ENUM_DEP_RESOLVE);

	buf = g_string_new (command);

	for (i = 0; i < g_strv_length (package_ids); i++) {
		gchar *nvra = poldek_get_nvra_from_package_id (package_ids[i]);

		g_string_append_c (buf, ' ');
		g_string_append (buf, nvra);

		g_free (nvra);
	}

	cmd = g_string_free (buf, FALSE);

	ts = poldek_ts_new (ctx, 0);
	rcmd = poclidek_rcmd_new (cctx, ts);

	ts->setop(ts, POLDEK_OP_PARTICLE, 0);

	if (poclidek_rcmd_execline (rcmd, cmd)) {
		tn_array *ipkgs = NULL, *dpkgs = NULL, *rpkgs = NULL;
		tn_array *install_pkgs = NULL, *update_pkgs = NULL, *remove_pkgs = NULL;

		ipkgs = poldek_ts_get_summary (ts, "I");
		dpkgs = poldek_ts_get_summary (ts, "D");
		rpkgs = poldek_ts_get_summary (ts, "R");

		get_ts_summary (ts_type, ipkgs, dpkgs, rpkgs, &install_pkgs, &update_pkgs, &remove_pkgs);

		if (install_pkgs) {
			for (i = 0; i < n_array_size (install_pkgs); i++) {
				struct pkg *pkg = n_array_nth (install_pkgs, i);

				poldek_backend_package (backend, pkg, PK_INFO_ENUM_INSTALLING, PK_FILTER_ENUM_NONE);
			}

			n_array_free (install_pkgs);
		}

		if (update_pkgs) {
			for (i = 0; i < n_array_size (update_pkgs); i++) {
				struct pkg *pkg = n_array_nth (update_pkgs, i);

				poldek_backend_package (backend, pkg, PK_INFO_ENUM_UPDATING, PK_FILTER_ENUM_NONE);
			}

			n_array_free (update_pkgs);
		}

		if (remove_pkgs) {
			for (i = 0; i < n_array_size (remove_pkgs); i++) {
				struct pkg *pkg = n_array_nth (remove_pkgs, i);

				poldek_backend_package (backend, pkg, PK_INFO_ENUM_REMOVING, PK_FILTER_ENUM_NONE);
			}

			n_array_free (remove_pkgs);
		}
	}

	g_free (cmd);

	poclidek_rcmd_free (rcmd);
	poldek_ts_free (ts);

	pk_backend_finished (backend);

	return TRUE;
}

/**
 * backend_simulate_install_packages:
 **/
static void
backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();
	pk_backend_set_uint (backend, "ts_type", TS_TYPE_ENUM_INSTALL);
	pk_backend_set_string (backend, "command", "cd /all-avail; install --test");
	pk_backend_thread_create (backend, do_simulate_packages);
}

/**
 * backend_simulate_remove_packages:
 **/
static void
backend_simulate_remove_packages (PkBackend *backend, gchar **package_ids, gboolean autoremove)
{
	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();
	pk_backend_set_uint (backend, "ts_type", TS_TYPE_ENUM_REMOVE);
	pk_backend_set_string (backend, "command", "cd /all-avail; uninstall --test");
	pk_backend_thread_create (backend, do_simulate_packages);
}

/**
 * backend_simulate_update_packages:
 **/
static void
backend_simulate_update_packages (PkBackend *backend, gchar **package_ids)
{
	poldek_backend_set_allow_cancel (backend, TRUE, TRUE);
	pb_error_clean ();
	pk_backend_set_uint (backend, "ts_type", TS_TYPE_ENUM_UPDATE);
	pk_backend_set_string (backend, "command", "cd /all-avail; upgrade --test");
	pk_backend_thread_create (backend, do_simulate_packages);
}

/* FIXME: port this away from PK_BACKEND_OPTIONS */
PK_BACKEND_OPTIONS (
	"poldek",					/* description */
	"Marcin Banasiak <megabajt@pld-linux.org>",	/* author */
	backend_initalize,				/* initalize */
	backend_destroy,				/* destroy */
	backend_get_groups,				/* get_groups */
	backend_get_filters,				/* get_filters */
	NULL,						/* get_roles */
	backend_get_mime_types,				/* get_mime_types */
	backend_get_cancel,				/* cancel */
	backend_download_packages,			/* download_packages */
	NULL,						/* get_categories */
	backend_get_depends,				/* get_depends */
	backend_get_details,				/* get_details */
	NULL,						/* get_distro_upgrades */
	backend_get_files,				/* get_files */
	backend_get_packages,				/* get_packages */
	backend_get_repo_list,				/* get_repo_list */
	backend_get_requires,				/* get_requires */
	backend_get_update_detail,			/* get_update_detail */
	backend_get_updates,				/* get_updates */
	NULL,						/* install_files */
	backend_install_packages,			/* install_packages */
	NULL,						/* install_signature */
	backend_refresh_cache,				/* refresh_cache */
	backend_remove_packages,			/* remove_packages */
	NULL,						/* repo_enable */
	NULL,						/* repo_set_data */
	backend_resolve,				/* resolve */
	NULL,						/* rollback */
	backend_search_details,				/* search_details */
	backend_search_files,				/* search_file */
	backend_search_groups,				/* search_group */
	backend_search_names,				/* search_name */
	backend_update_packages,			/* update_packages */
	backend_update_system,				/* update_system */
	backend_what_provides,				/* what_provides */
	NULL,						/* simulate_install_files */
	backend_simulate_install_packages,		/* simulate_install_packages */
	backend_simulate_remove_packages,		/* simulate_remove_packages */
	backend_simulate_update_packages,		/* simulate_update_packages */
	NULL,						/* upgrade_system */
	NULL,						/* transaction_start */
	NULL						/* transaction_stop */
);

