/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2012 Marcin Banasiak <megabajt@pld-linux.org>
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

#include <sys/types.h>
#include <pwd.h>

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
static void poldek_backend_package (PkBackendJob *job, struct pkg *pkg, PkInfoEnum infoenum, PkBitfield filters);
static long do_get_bytes_to_download (struct poldek_ts *ts, tn_array *pkgs);
static gint do_get_files_to_download (const struct poldek_ts *ts, const gchar *mark);
static void pb_load_packages (PkBackendJob *job);
static void poldek_backend_set_allow_cancel (PkBackendJob *job, gboolean allow_cancel, gboolean reset);

static void pb_error_show (PkBackendJob *job, PkErrorEnum errorcode);
static void pb_error_clean (void);

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
} PkBackendPoldekProgressData;

typedef struct {
	PkBackendPoldekProgressData	*progress_data;

	tn_array			*to_install_pkgs;
	tn_array			*to_update_pkgs;
	tn_array			*to_remove_pkgs;

	guint				to_install;
} PkBackendPoldekJobData;

typedef struct {
	struct poldek_ctx	*ctx;
	struct poclidek_ctx	*cctx;
	struct pkgdb		*db;
} PkBackendPoldekPriv;

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

typedef enum {
	PB_RPM_STATE_ENUM_NONE = 0,
	PB_RPM_STATE_ENUM_INSTALLING = (1 << 1),
	PB_RPM_STATE_ENUM_REPACKAGING = (1 << 2)
} PbRpmState;

/* I need this to avoid showing error messages more than once.
 * It's initalized by backend_initalize() and destroyed by
 * pk_backend_destroy(), but every method should clean it at the
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

static PkBackendPoldekPriv *priv = NULL;

/**
 * execute_command:
 *
 * Execute specified command.
 *
 * Returns TRUE on success, FALSE when some error occurs.
 **/
static gboolean
execute_command (const gchar *format, ...) G_GNUC_PRINTF(1, 2);

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

	ts = poldek_ts_new (priv->ctx, 0);
	rcmd = poclidek_rcmd_new (priv->cctx, ts);

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
execute_packages_command (const gchar *format, ...) G_GNUC_PRINTF(1, 2);

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

	rcmd = poclidek_rcmd_new (priv->cctx, NULL);

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
 *	  present in lang string. Returned value must be released.
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
get_locale_variants (const gchar *lang)
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
pkg_uinf_i18n (PkBackendJob *job, struct pkg *pkg)
{
	struct pkguinf *pkgu = NULL;
	const gchar *lang = NULL;

	lang = pk_backend_job_get_locale (job);

	if (lang) {
		tn_array *langs;

		langs = get_locale_variants (lang);
		pkgu = pkg_xuinf (pkg, langs);
	} else {
		pkgu = pkg_uinf (pkg);
	}

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
poldek_vf_progress_new (void *user_data, const gchar *label)
{
	PkBackendJob *job = (PkBackendJob *) user_data;
	PkRoleEnum role;

	role = pk_backend_job_get_role (job);

	if (role == PK_ROLE_ENUM_INSTALL_PACKAGES || role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		gchar *filename = g_path_get_basename (label), *pkgname;
		tn_array *packages = NULL;

		pkgname = g_strndup (filename, (sizeof(gchar)*strlen(filename)-4));

		pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);

		if ((packages = execute_packages_command ("cd /all-avail; ls -q %s", pkgname)) != NULL) {
			struct pkg *pkg = n_array_nth (packages, 0);

			poldek_backend_package (job, pkg, PK_INFO_ENUM_DOWNLOADING, PK_FILTER_ENUM_NONE);

			n_array_free (packages);
		}

		g_free (pkgname);
		g_free (filename);
	}

	return user_data;
}

static void
poldek_vf_progress (void *user_data, long total, long amount)
{
	PkBackendJob *job = (PkBackendJob *) user_data;
	PkBackendPoldekJobData *job_data = pk_backend_job_get_user_data (job);
	PkBackendPoldekProgressData *pd = job_data->progress_data;
	PkRoleEnum role;

	role = pk_backend_job_get_role (job);

	if (role == PK_ROLE_ENUM_INSTALL_PACKAGES || role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
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

		//pk_backend_set_sub_percentage (backend, pd->subpercentage);

	} else if (role == PK_ROLE_ENUM_REFRESH_CACHE) {
		if (pd->step == 0)
			pd->percentage = 1;
		else
			pd->percentage = (gint)(((float)pd->step / (float)pd->nsources) * 100);
	}

	pk_backend_job_set_percentage (job, pd->percentage);
}

static void
poldek_vf_progress_reset (void *user_data)
{
	PkBackendJob *job = (PkBackendJob *) user_data;
	PkBackendPoldekJobData *job_data = pk_backend_job_get_user_data (job);

	job_data->progress_data->subpercentage = 0;
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
get_ts_summary (PkBackendJob *job, tn_array *ipkgs, tn_array *dpkgs, tn_array *rpkgs,
		tn_array **install_pkgs, tn_array **update_pkgs, tn_array **remove_pkgs)
{
	PkRoleEnum role;
	guint  i;

	role = pk_backend_job_get_role (job);

	if (role == PK_ROLE_ENUM_INSTALL_PACKAGES || role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		*install_pkgs = n_array_new (2, (tn_fn_free)pkg_free, (tn_fn_cmp)pkg_cmp_name_evr);
		*update_pkgs = n_array_new (2, (tn_fn_free)pkg_free, (tn_fn_cmp)pkg_cmp_name_evr);
	}

	*remove_pkgs = n_array_new (2, (tn_fn_free)pkg_free, (tn_fn_cmp)pkg_cmp_name_evr);

	switch (role) {
		case PK_ROLE_ENUM_INSTALL_PACKAGES:
		case PK_ROLE_ENUM_UPDATE_PACKAGES:
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
		case PK_ROLE_ENUM_REMOVE_PACKAGES:
			/* copy packages from rpkgs and dpkgs to remove_pkgs */
			if (rpkgs)
				n_array_concat_ex (*remove_pkgs, rpkgs, (tn_fn_dup)pkg_link);

			if (dpkgs)
				n_array_concat_ex (*remove_pkgs, dpkgs, (tn_fn_dup)pkg_link);

			break;
		default:
			g_error ("Unknown role value: %d", role);
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
	PkBackendJob *job = (PkBackendJob *) data;
	PkBackendPoldekJobData *job_data = pk_backend_job_get_user_data (job);
	PkBackendPoldekProgressData *pd = job_data->progress_data;
	tn_array	*ipkgs, *dpkgs, *rpkgs;
	size_t		i = 0;
	gint		result = 1;

	ipkgs = poldek_ts_get_summary (ts, "I");
	dpkgs = poldek_ts_get_summary (ts, "D");
	rpkgs = poldek_ts_get_summary (ts, "R");

	if (poldek_ts_get_type (ts) == POLDEK_TS_TYPE_INSTALL) {
		tn_array *update_pkgs, *remove_pkgs, *install_pkgs;
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

		job_data->to_install = to_install;

		job_data->to_update_pkgs = update_pkgs;
		job_data->to_remove_pkgs = remove_pkgs;
		job_data->to_install_pkgs = install_pkgs;
	} else if (poldek_ts_get_type (ts) == POLDEK_TS_TYPE_UNINSTALL) {
		GVariant *params = pk_backend_job_get_parameters (job);
		PkBitfield transaction_flags;
		gchar **package_ids;
		gboolean allow_deps;
		gboolean autoremove;

		g_variant_get (params, "(t^a&sbb)",
			       &transaction_flags,
			       &package_ids,
			       &allow_deps,
			       &autoremove);

		/* check if transaction can be performed */
		if (allow_deps == FALSE) {
			if (dpkgs && n_array_size (dpkgs) > 0) {
				result = 0;
			}
		}

		if (result == 1) { /* remove is allowed */
			pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);

			/* we shouldn't cancel remove proccess */
			poldek_backend_set_allow_cancel (job, FALSE, FALSE);

			if (dpkgs) {
				for (i = 0; i < n_array_size (dpkgs); i++) {
					struct pkg *pkg = n_array_nth (dpkgs, i);

					poldek_backend_package (job, pkg, PK_INFO_ENUM_REMOVING, PK_FILTER_ENUM_NONE);
				}
			}

			if (rpkgs) {
				for (i = 0; i < n_array_size (rpkgs); i++) {
					struct pkg *pkg = n_array_nth (rpkgs, i);

					poldek_backend_package (job, pkg, PK_INFO_ENUM_REMOVING, PK_FILTER_ENUM_NONE);
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
setup_vf_progress (struct vf_progress *vf_progress, PkBackendJob *job)
{
	vf_progress->data = job;
	vf_progress->new = poldek_vf_progress_new;
	vf_progress->progress = poldek_vf_progress;
	vf_progress->reset = poldek_vf_progress_reset;
	vf_progress->free = NULL;

	vfile_configure (VFILE_CONF_VERBOSE, &verbose);
	vfile_configure (VFILE_CONF_STUBBORN_NRETRIES, 5);

	poldek_configure (priv->ctx, POLDEK_CONF_VFILEPROGRESS, vf_progress);
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

static void
pk_backend_poldek_open_pkgdb (void)
{
	if (priv->db == NULL) {
		priv->db = pkgdb_open (poldek_get_pmctx (priv->ctx),
				       NULL, NULL, O_RDONLY, NULL);
	}
}

static gboolean
pkg_is_installed (struct pkg *pkg)
{
	gint cmprc, is_installed = 0;

	g_return_val_if_fail (pkg != NULL, FALSE);

	pk_backend_poldek_open_pkgdb ();

	if (priv->db) {
		is_installed = pkgdb_is_pkg_installed (priv->db, pkg, &cmprc);
	}

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
	struct poldek_ts *ts = NULL;
	gchar *pkgid = NULL;

	g_return_val_if_fail (localpath != NULL, NULL);

	ts = poldek_ts_new (priv->ctx, 0);

	pk_backend_poldek_open_pkgdb ();

	if (priv->db) {
		const struct pm_dbrec *ldbrec;
		struct pkgdb_it it;

		pkgdb_it_init (priv->db, &it, PMTAG_FILE, localpath);

		/* get only one package */
		if ((ldbrec = pkgdb_it_get (&it)) != NULL) {
			gchar *name = NULL, *version = NULL, *release = NULL, *arch = NULL;
			gint epoch;

			pm_dbrec_nevr (ldbrec, &name, &epoch, &version, &release, &arch, NULL);

			pkgid = g_strdup_printf ("%s-%s-%s.%s", name, version, release, arch);
		}

		pkgdb_it_destroy (&it);
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
	return poclidek_get_dent_packages (priv->cctx, POCLIDEK_INSTALLEDDIR);
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
	     struct pkg *pkg, PkBitfield filters, gboolean recursive)
{
	tn_array	*tmp = NULL;
	size_t		i;

	tmp = n_array_new (2, NULL, NULL);

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
		if (recursive && tmp && n_array_size (tmp) > 0) {
			for (i = 0; i < n_array_size (tmp); i++) {
				struct pkg	*p = n_array_nth (tmp, i);
				do_requires (installed, available, requires, p, filters, recursive);
			}
		}
	}

	n_array_free (tmp);
}

/**
 * do_depends:
 */
static void
do_depends (tn_array *installed, tn_array *available, tn_array *depends, struct pkg *pkg, PkBitfield filters, gboolean recursive)
{
	tn_array	*reqs = pkg->reqs;
	tn_array	*tmp = NULL;
	size_t		i;

	tmp = n_array_new (2, NULL, NULL);

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

			do_depends (installed, available, depends, p, filters, recursive);
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
poldek_backend_package (PkBackendJob *job, struct pkg *pkg, PkInfoEnum infoenum, PkBitfield filters)
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

	if ((pkgu = pkg_uinf_i18n (job, pkg))) {
		pk_backend_job_package (job, infoenum, package_id, pkguinf_get (pkgu, PKGUINF_SUMMARY));

		pkguinf_free (pkgu);
	} else {
		pk_backend_job_package (job, infoenum, package_id, "");
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
static void
search_package_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	GPtrArray *array = NULL;
	PkBitfield		filters;
	PkRoleEnum		role;
	gchar		       *search_cmd_available = NULL;
	gchar		       *search_cmd_installed = NULL;
	tn_array	       *pkgs = NULL;
	gchar		      **values = NULL;
	gchar		       *search;

	role = pk_backend_job_get_role (job);

	if (role == PK_ROLE_ENUM_WHAT_PROVIDES) {
		array = (GPtrArray *) user_data;

		g_ptr_array_add (array, NULL);

		values = (gchar **) array->pdata;

		g_variant_get (params, "(t)",
			       &filters);

		g_ptr_array_free (array, FALSE);
	} else {
		g_variant_get (params, "(t^a&s)",
			       &filters,
			       &values);
	}


	pb_load_packages (job);

	/* SearchName */
	if (role == PK_ROLE_ENUM_SEARCH_NAME) {
		search = g_strjoinv ("*", values);

		search_cmd_installed = g_strdup_printf ("ls -q *%s*", search);
		search_cmd_available = g_strdup (search_cmd_installed);

		g_free (search);
	/* SearchGroup */
	} else if (role == PK_ROLE_ENUM_SEARCH_GROUP) {
		GString	*command;
		guint		i;

		command = g_string_new ("search -qg --perlre /");

		for (i = 0; i < g_strv_length (values); i++) {
			const gchar *regex = NULL;

			regex = pld_group_get_regex_from_text (values[i]);

			if (regex == NULL) {
				pk_backend_job_error_code (job, PK_ERROR_ENUM_GROUP_NOT_FOUND,
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
	} else if (role == PK_ROLE_ENUM_SEARCH_FILE) {
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
	} else if (role == PK_ROLE_ENUM_WHAT_PROVIDES) {
		search = g_strjoinv ("\\|", values);

		search_cmd_installed = g_strdup_printf ("search -qp --perlre /%s/", search);
		search_cmd_available = g_strdup_printf ("search -qp --perlre /%s/", search);

		g_free (search);
	/* Resolve */
	} else if (role == PK_ROLE_ENUM_RESOLVE) {
		search = g_strjoinv(" ", values);

		search_cmd_installed = g_strdup_printf ("ls -q %s", search);
		search_cmd_available = g_strdup (search_cmd_installed);

		g_free (search);
	}

	if ((search_cmd_installed != NULL && search_cmd_available != NULL) || role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		tn_array *installed = NULL;
		tn_array *available = NULL;

		if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
			if (role == PK_ROLE_ENUM_SEARCH_DETAILS)
				installed = do_search_details ("cd /installed", values);
			else
				installed = execute_packages_command ("cd /installed; %s", search_cmd_installed);
		}

		if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
			if (role == PK_ROLE_ENUM_SEARCH_DETAILS)
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

			poldek_backend_package (job, pkg, PK_INFO_ENUM_UNKNOWN, filters);
		}
		n_array_free (pkgs);
	} else {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "Package not found");
	}

	if (sigint_reached ()) {
		switch (role) {
			case PK_ROLE_ENUM_SEARCH_NAME:
			case PK_ROLE_ENUM_SEARCH_GROUP:
			case PK_ROLE_ENUM_SEARCH_DETAILS:
			case PK_ROLE_ENUM_SEARCH_FILE:
			case PK_ROLE_ENUM_RESOLVE:
				pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Search cancelled.");
				break;
			default:
				pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Transaction cancelled.");
		}
	}

	g_free (search_cmd_installed);
	g_free (search_cmd_available);

out:
	if (array)
		g_ptr_array_unref (array);
}

static void
update_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	struct vf_progress	vf_progress;
	guint			i, toupdate = 0;
	gchar **package_ids, *command;
	GString *cmd;
	PkBitfield transaction_flags;

	g_variant_get (params, "(t^a&s)",
		       &transaction_flags,
		       &package_ids);

	setup_vf_progress (&vf_progress, job);

	pb_load_packages (job);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);
	pb_error_clean ();

	cmd = g_string_new ("upgrade ");

	if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD)) {
		g_string_append_printf (cmd, "--fetch ");
	}

	for (i = 0; i < g_strv_length (package_ids); i++) {
		struct pkg *pkg;

		pkg = poldek_get_pkg_from_package_id (package_ids[i]);

		g_string_append_printf (cmd, "%s-%s-%s.%s ", pkg->name,
					pkg->ver, pkg->rel, pkg_arch (pkg));

		toupdate++;

		pkg_free (pkg);
	}

	command = g_string_free (cmd, FALSE);

	if (toupdate > 0) {
		if (execute_command ("%s", command) == FALSE) {
			pb_error_show (job, PK_ERROR_ENUM_TRANSACTION_ERROR);
		}
	}

	g_free (command);
}

/**
 * do_simulate_packages:
 */
static void
do_simulate_packages (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	struct poclidek_rcmd *rcmd = NULL;
	struct poldek_ts     *ts = NULL;
	GString      *buf = NULL;
	gchar	*cmd = NULL;
	gchar       **package_ids = NULL;
	const gchar  *command = NULL;
	guint	 i;
	PkRoleEnum role;
	PkBitfield transaction_flags;
	gboolean allow_deps = FALSE;
	gboolean autoremove = FALSE;

	role = pk_backend_job_get_role (job);

	if (role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		command = "cd /all-avail; install --test";

		g_variant_get (params, "(t^a&s)",
			       &transaction_flags,
			       &package_ids);
	} else if (role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		command = "cd /all-avail; upgrade --test";

		g_variant_get (params, "(t^a&s)",
			       &transaction_flags,
			       &package_ids);
	} else if (role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		command = "cd /all-avail; uninstall --test";

		g_variant_get (params, "(t^a&sbb)",
	                       &transaction_flags,
	                       &package_ids,
	                       &allow_deps,
	                       &autoremove);
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);

	buf = g_string_new (command);

	for (i = 0; i < g_strv_length (package_ids); i++) {
		gchar *nvra = poldek_get_nvra_from_package_id (package_ids[i]);

		g_string_append_c (buf, ' ');
		g_string_append (buf, nvra);

		g_free (nvra);
	}

	cmd = g_string_free (buf, FALSE);

	ts = poldek_ts_new (priv->ctx, 0);
	rcmd = poclidek_rcmd_new (priv->cctx, ts);

	ts->setop(ts, POLDEK_OP_PARTICLE, 0);

	if (poclidek_rcmd_execline (rcmd, cmd)) {
		tn_array *ipkgs = NULL, *dpkgs = NULL, *rpkgs = NULL;
		tn_array *install_pkgs = NULL, *update_pkgs = NULL, *remove_pkgs = NULL;

		ipkgs = poldek_ts_get_summary (ts, "I");
		dpkgs = poldek_ts_get_summary (ts, "D");
		rpkgs = poldek_ts_get_summary (ts, "R");

		get_ts_summary (job, ipkgs, dpkgs, rpkgs, &install_pkgs, &update_pkgs, &remove_pkgs);

		if (install_pkgs) {
			for (i = 0; i < n_array_size (install_pkgs); i++) {
				struct pkg *pkg = n_array_nth (install_pkgs, i);

				poldek_backend_package (job, pkg, PK_INFO_ENUM_INSTALLING, PK_FILTER_ENUM_NONE);
			}

			n_array_free (install_pkgs);
		}

		if (update_pkgs) {
			for (i = 0; i < n_array_size (update_pkgs); i++) {
				struct pkg *pkg = n_array_nth (update_pkgs, i);

				poldek_backend_package (job, pkg, PK_INFO_ENUM_UPDATING, PK_FILTER_ENUM_NONE);
			}

			n_array_free (update_pkgs);
		}

		if (remove_pkgs) {
			for (i = 0; i < n_array_size (remove_pkgs); i++) {
				struct pkg *pkg = n_array_nth (remove_pkgs, i);

				poldek_backend_package (job, pkg, PK_INFO_ENUM_REMOVING, PK_FILTER_ENUM_NONE);
			}

			n_array_free (remove_pkgs);
		}
	}

	g_free (cmd);

	poclidek_rcmd_free (rcmd);
	poldek_ts_free (ts);
}

static void
pb_load_packages (PkBackendJob *job)
{
	gboolean	allow_cancel = pk_backend_job_get_allow_cancel (job);

	/* this operation can't be cancelled, so if enabled, set allow_cancel to FALSE */
	if (allow_cancel)
		poldek_backend_set_allow_cancel (job, FALSE, FALSE);

	/* load information about installed and available packages */
	poclidek_load_packages (priv->cctx, POCLIDEK_LOAD_ALL);

	if (allow_cancel)
		poldek_backend_set_allow_cancel (job, TRUE, FALSE);
}

static void
pb_error_show (PkBackendJob *job, PkErrorEnum errorcode)
{
	if (sigint_reached()) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Action cancelled.");
		return;
	}

	/* Before emiting error_code try to find the most suitable PkErrorEnum */
	if (g_strrstr (pberror->tslog->str, " unresolved depend") != NULL)
		errorcode = PK_ERROR_ENUM_DEP_RESOLUTION_FAILED;
	else if (g_strrstr (pberror->tslog->str, " conflicts") != NULL)
		errorcode = PK_ERROR_ENUM_FILE_CONFLICTS;

	pk_backend_job_error_code (job, errorcode, "%s", pberror->tslog->str);
}

/**
 * pb_error_check:
 *
 * When we try to install already installed package, poldek won't report any error
 * just show message like 'liferea-1.4.11-2.i686: equal version installed, skipped'.
 * This function checks if it happens and if yes, emits error_code and returns TRUE.
 **/
static gboolean
pb_error_check (PkBackendJob *job)
{
	PkErrorEnum	errorcode = PK_ERROR_ENUM_UNKNOWN;

	if (g_strrstr (pberror->tslog->str, " version installed, skipped") != NULL)
		errorcode = PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED;

	if (errorcode != PK_ERROR_ENUM_UNKNOWN) {
		pk_backend_job_error_code (job, errorcode, "%s", pberror->tslog->str);
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
show_rpm_progress (PkBackendJob *job, gchar *message)
{
	PkBackendPoldekJobData *job_data = pk_backend_job_get_user_data (job);

	g_return_if_fail (message != NULL);

	if (pberror->rpmstate & PB_RPM_STATE_ENUM_REPACKAGING) {
		g_debug ("repackaging '%s'", message);
	} else if (pberror->rpmstate & PB_RPM_STATE_ENUM_INSTALLING) {
		tn_array *upkgs, *ipkgs, *rpkgs, *arr = NULL;
		PkInfoEnum pkinfo;
		gint n = -2;

		g_debug ("installing or updating '%s'", message);

		ipkgs = job_data->to_install_pkgs;
		upkgs = job_data->to_update_pkgs;
		rpkgs = job_data->to_remove_pkgs;

		/* emit remove for packages marked for removal */
		if (rpkgs) {
			size_t i;

			/* XXX: don't release rpkgs array here! */
			for (i = 0; i < n_array_size (rpkgs); i++) {
				struct pkg *pkg = n_array_nth (rpkgs, i);

				poldek_backend_package (job, pkg, PK_INFO_ENUM_REMOVING, PK_FILTER_ENUM_NONE);

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

			poldek_backend_package (job, pkg, pkinfo, PK_FILTER_ENUM_NONE);

			n_array_remove_nth (arr, n);

			if (upkgs) {
				in_arrays += n_array_size (upkgs);
			}
			if (ipkgs) {
				in_arrays += n_array_size (ipkgs);
			}

			pk_backend_job_set_percentage (job, (gint)(((float)(job_data->to_install - in_arrays) / (float)job_data->to_install) * 100));
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
	PkBackendJob *job = (PkBackendJob *) data;

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
		pk_backend_job_message (job, PK_MESSAGE_ENUM_CONNECTION_REFUSED, "%s", message);
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
		poldek_backend_set_allow_cancel (job, FALSE, FALSE);
	} else if (strstr (message, "Repackaging...")) {
		pberror->rpmstate |= PB_RPM_STATE_ENUM_REPACKAGING;

		pk_backend_job_set_status (job, PK_STATUS_ENUM_REPACKAGING);
	} else if (strstr (message, "Upgrading...")) {
		pberror->rpmstate &= (~PB_RPM_STATE_ENUM_REPACKAGING);
	}
	if (pberror->rpmstate != PB_RPM_STATE_ENUM_NONE) {
		gchar *fn;

		if ((fn = get_filename_from_message (message)) == NULL)
			return;

		if ((pberror->rpmstate & PB_RPM_STATE_ENUM_REPACKAGING) == FALSE) {
			PkRoleEnum role = pk_backend_job_get_role (job);

			/* set proper status */
			if (role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
				pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);
			} else if (role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
				pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);
			}
		}

		show_rpm_progress (job, fn);
	}
}

static void
poldek_backend_set_allow_cancel (PkBackendJob *job, gboolean allow_cancel, gboolean reset)
{
	if (reset)
		sigint_reset ();

	pk_backend_job_set_allow_cancel (job, allow_cancel);
}

static void
do_poldek_init (PkBackend *backend)
{
	poldeklib_init ();

	priv->ctx = poldek_new (0);

	poldek_load_config (priv->ctx, "/etc/poldek/poldek.conf", NULL, 0);

	poldek_setup (priv->ctx);

	priv->cctx = poclidek_new (priv->ctx);

	poldek_set_verbose (1);
	/* disable LOGFILE and LOGTTY logging */
	poldek_configure (priv->ctx, POLDEK_CONF_LOGFILE, NULL);
	poldek_configure (priv->ctx, POLDEK_CONF_LOGTTY, NULL);

	/* disable unique package names */
	poldek_configure (priv->ctx, POLDEK_CONF_OPT, POLDEK_OP_UNIQN, 0);

	poldek_configure (priv->ctx, POLDEK_CONF_OPT, POLDEK_OP_LDALLDESC, 1);

	/* poldek has to ask. Otherwise callbacks won't be used */
	poldek_configure (priv->ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_INST, 1);
	poldek_configure (priv->ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_UNINST, 1);
	/* (...), but we don't need choose_equiv callback */
	poldek_configure (priv->ctx, POLDEK_CONF_OPT, POLDEK_OP_EQPKG_ASKUSER, 0);

	/* Install all suggested packages by default */
	poldek_configure (priv->ctx, POLDEK_CONF_CHOOSESUGGESTS_CB, suggests_callback, NULL);

	sigint_init ();
}

static void
do_poldek_destroy (PkBackend *backend)
{
	sigint_destroy ();

	if (priv->db != NULL) {
		pkgdb_close (priv->db);
		priv->db = NULL;
	}

	poclidek_free (priv->cctx);
	poldek_free (priv->ctx);

	poldeklib_destroy ();
}

static void
poldek_reload (PkBackendJob *job, gboolean load_packages) {
	do_poldek_destroy (pk_backend_job_get_backend (job));
	do_poldek_init (pk_backend_job_get_backend (job));

	if (load_packages)
		pb_load_packages (job);
}

/**
 * pk_backend_get_description:
 */
const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return "Poldek";
}

/**
 * pk_backend_get_author:
 */
const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return "Marcin Banasiak <megabajt@pld-linux.org>";
}

/**
 * pk_backend_initialize:
 */
void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	struct passwd *uid_ent = NULL;

	g_debug ("backend initalize start");

	/* looks like rpm5 needs HOME to be set */
	if ((uid_ent = getpwuid (getuid ())) == NULL) {
		g_error ("Failed to set HOME");
	}

	setenv ("HOME", uid_ent->pw_dir, 0);

	clv = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)n_array_free);

	pberror = g_new0 (PbError, 1);
	pberror->tslog = g_string_new ("");

	priv = g_new0 (PkBackendPoldekPriv, 1);

	do_poldek_init (backend);

	g_debug ("backend initalize end");
}
/**
 * pk_backend_destroy:
 */
void
pk_backend_destroy (PkBackend *backend)
{
	do_poldek_destroy (backend);

	g_free (priv);

	/* release PbError struct */
	g_free (pberror->vfffmsg);
	g_string_free (pberror->tslog, TRUE);

	g_hash_table_destroy (clv);

	g_free (pberror);
}

/**
 * pk_backend_start_job:
 */
void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
	PkBackendPoldekJobData *job_data;

	job_data = g_new0 (PkBackendPoldekJobData, 1);
	pk_backend_job_set_user_data (job, job_data);

	job_data->progress_data = g_new0 (PkBackendPoldekProgressData, 1);

	poldek_log_set_appender ("PackageKit", (void *) job, NULL, 0, (poldek_vlog_fn) poldek_backend_log);

	poldek_configure (priv->ctx, POLDEK_CONF_TSCONFIRM_CB, ts_confirm, job);
}

/**
 * pk_backend_stop_job:
 */
void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
	PkBackendPoldekJobData *job_data = pk_backend_job_get_user_data (job);

	if (job_data->progress_data != NULL)
		g_free (job_data->progress_data);

	n_array_cfree (&job_data->to_install_pkgs);
	n_array_cfree (&job_data->to_update_pkgs);
	n_array_cfree (&job_data->to_remove_pkgs);

	g_free (job_data);

	// close pkgdb as well
	if (priv->db != NULL) {
		pkgdb_close (priv->db);
		priv->db = NULL;
	}

	pk_backend_job_set_user_data (job, NULL);
}

/**
 * pk_backend_get_groups:
 */
PkBitfield
pk_backend_get_groups (PkBackend *backend)
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
 * pk_backend_get_filters:
 */
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_NEWEST,
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		-1);
}

/**
 * pk_backend_get_mime_types:
 */
gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
	const gchar *mime_types[] = {
				"application/x-rpm",
				NULL };
	return g_strdupv ((gchar **) mime_types);
}

/**
 * pk_backend_cancel:
 */
void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_CANCEL);

	sigint_emit ();
}

/**
 * backend_download_packages_thread:
 */
static void
backend_download_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	PkBackendPoldekJobData *job_data = pk_backend_job_get_user_data (job);
	PkBackendPoldekProgressData *pd = job_data->progress_data;
	struct poldek_ts *ts;
	struct vf_progress vf_progress;
	tn_array *pkgs;
	gchar **package_ids;
	const gchar *destdir;
	guint i;

	g_variant_get (params, "(^a&ss)",
		       &package_ids,
		       &destdir);

	pkgs = n_array_new (10, (tn_fn_free)pkg_free, NULL);

	ts = poldek_ts_new (priv->ctx, 0);

	setup_vf_progress (&vf_progress, job);

	pb_load_packages (job);

	for (i = 0; i < g_strv_length (package_ids); i++) {
		struct pkg *pkg = poldek_get_pkg_from_package_id (package_ids[i]);

		n_array_push (pkgs, pkg_link (pkg));

		pkg_free (pkg);
	}

	pd->bytesget = 0;
	pd->bytesdownload = poldek_get_bytes_to_download (ts, pkgs);

	if (packages_fetch (poldek_get_pmctx (ts->ctx), pkgs, destdir, 1)) {
		for (i = 0; i < n_array_size (pkgs); i++) {
			struct pkg *pkg = n_array_nth (pkgs, i);
			gchar *package_id;
			gchar *path;
			gchar *to_strv[] = { NULL, NULL };
			gchar buf[256];

			package_id = package_id_from_pkg (pkg, NULL, PK_FILTER_ENUM_NONE);
			path = g_build_filename (destdir, pkg_filename (pkg, buf, sizeof (buf)), NULL);
			to_strv[0] = path;

			pk_backend_job_files (job, package_id, to_strv);

			g_free (package_id);
			g_free (path);
		}
	}

	poldek_ts_free (ts);
}

void
pk_backend_download_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids,
			      const gchar *directory)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
	poldek_backend_set_allow_cancel (job, FALSE, TRUE);
	pb_error_clean ();

	pk_backend_job_thread_create (job, backend_download_packages_thread, NULL, NULL);
}

/**
 * backend_depends_on_thread:
 */
static void
backend_depends_on_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	PkBitfield filters;
	gboolean recursive;
	gchar **package_ids;
	struct pkg *pkg;
	tn_array *deppkgs, *available, *installed;
	size_t i;

	g_variant_get (params, "(t^a&sb)",
		       &filters,
		       &package_ids,
		       &recursive);

	pb_load_packages (job);

	deppkgs = n_array_new (2, NULL, NULL);

	installed = poldek_get_installed_packages ();
	available = poldek_get_avail_packages (priv->ctx);

	pkg = poldek_get_pkg_from_package_id (package_ids[0]);

	do_depends (installed, available, deppkgs, pkg, filters, recursive);

	n_array_sort_ex(deppkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);

	for (i = 0; i < n_array_size (deppkgs); i++) {
		struct pkg	*p = n_array_nth (deppkgs, i);

		poldek_backend_package (job, p, PK_INFO_ENUM_UNKNOWN, filters);
	}

	pkg_free (pkg);

	n_array_free (deppkgs);
	n_array_free (available);
	n_array_free (installed);
}

void
pk_backend_depends_on (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, FALSE, TRUE);
	pb_error_clean ();

	pk_backend_job_thread_create (job, backend_depends_on_thread, NULL, NULL);
}

/**
 * backend_get_details_thread:
 */
static void
backend_get_details_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar **package_ids;
	guint n;

	g_variant_get (params, "(^a&s)",
		       &package_ids);

	pb_load_packages (job);

	for (n = 0; n < g_strv_length (package_ids); n++) {
		struct pkg *pkg = NULL;

		if ((pkg = poldek_get_pkg_from_package_id (package_ids[n])) != NULL) {
			struct pkguinf *pkgu = NULL;
			PkGroupEnum group;

			group = pld_group_to_enum (pkg_group (pkg));

			if ((pkgu = pkg_uinf_i18n (job, pkg)) != NULL) {
				pk_backend_job_details (job,
							package_ids[n],
							NULL,
							pkguinf_get (pkgu, PKGUINF_LICENSE),
							group,
							pkguinf_get (pkgu, PKGUINF_DESCRIPTION),
							pkguinf_get (pkgu, PKGUINF_URL),
							pkg->fsize);
				pkguinf_free (pkgu);
			} else {
				pk_backend_job_details (job,
							package_ids[n],
							"",
							"",
							group,
							"",
							"",
							pkg->fsize);
			}

			pkg_free (pkg);
		}
	}
}

void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, FALSE, TRUE);
	pb_error_clean ();

	pk_backend_job_thread_create (job, backend_get_details_thread, NULL, NULL);
}

/**
 * backend_get_files_thread:
 */
static void
backend_get_files_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar **package_ids;
	size_t n;

	g_variant_get (params, "(^a&s)",
		       &package_ids);

	pb_load_packages (job);

	for (n = 0; n < g_strv_length (package_ids); n++) {
		struct pkg *pkg;

		pkg = poldek_get_pkg_from_package_id (package_ids[n]);

		if (pkg != NULL) {
			struct pkgflist *flist = pkg_get_flist (pkg);
			GPtrArray *files;
			gint i, j;

			if (flist == NULL) {
				pkg_free (pkg);
				continue;
			}

			files = g_ptr_array_new_with_free_func(g_free);

			for (i = 0; i < n_tuple_size (flist->fl); i++) {
				struct pkgfl_ent *flent = n_tuple_nth (flist->fl, i);
				gchar *dirname;

				dirname = g_strdup_printf ("%s%s", *flent->dirname == '/' ? "" : "/", flent->dirname);

				for (j = 0; j < flent->items; j++) {
					struct flfile *f = flent->files[j];
					gchar *fname;

					if (strcmp (dirname, "/") == 0)
						fname = g_strdup_printf("/%s", f->basename);
					else
						fname = g_strdup_printf("%s/%s", dirname, f->basename);
					g_ptr_array_add (files, fname);
				}
				g_free (dirname);
			}

			g_ptr_array_add(files, NULL);
			pk_backend_job_files (job, package_ids[n], (gchar**)files->pdata);
			g_ptr_array_unref (files);

			pkg_free (pkg);
		}
	}
}

void
pk_backend_get_files (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, FALSE, TRUE);
	pb_error_clean ();

	pk_backend_job_thread_create (job, backend_get_files_thread, NULL, NULL);
}

/**
 * backend_get_packages_thread:
 */
static void
backend_get_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	PkBitfield	filters;
	tn_array       *installed = NULL;
	tn_array       *available = NULL;
	tn_array       *packages = NULL;
	guint		i;

	g_variant_get (params, "(t)",
		       &filters);

	pk_backend_job_set_percentage (job, 0);

	pb_load_packages (job);

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) == FALSE)
		installed = poldek_get_installed_packages ();

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) == FALSE)
		available = poldek_get_avail_packages (priv->ctx);

	pk_backend_job_set_percentage (job, 4);

	packages = do_post_search_process (installed, available);

	do_filtering (packages, filters);

	pk_backend_job_set_percentage (job, 10);

	if (packages != NULL) {
		for (i = 0; i < n_array_size (packages); i++) {
			struct pkg     *pkg = n_array_nth (packages, i);

			if (sigint_reached ())
				break;

			pk_backend_job_set_percentage (job, (guint)(10 + (90 * (float)(i + 1) / n_array_size (packages))));

			poldek_backend_package (job, pkg, PK_INFO_ENUM_UNKNOWN, filters);
		}
	}

	if (sigint_reached ())
		pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Action cancelled");
	else
		pk_backend_job_set_percentage (job, 100);

	n_array_cfree (&installed);
	n_array_cfree (&available);
	n_array_cfree (&packages);
}

void
pk_backend_get_packages (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, TRUE, TRUE);
	pb_error_clean ();

	pk_backend_job_thread_create (job, backend_get_packages_thread, NULL, NULL);
}

/**
 * backend_required_by_thread:
 */
static void
backend_required_by_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	struct pkg	*pkg;
	tn_array	*reqpkgs, *available, *installed;
	size_t		i;
	gchar **package_ids;
	PkBitfield filters;
	gboolean recursive;

	g_variant_get (params, "(t^a&sb)",
		       &filters,
		       &package_ids,
		       &recursive);

	pb_load_packages (job);

	reqpkgs = n_array_new (2, NULL, NULL);

	pkg = poldek_get_pkg_from_package_id (package_ids[0]);
	installed = poldek_get_installed_packages ();
	available = poldek_get_avail_packages (priv->ctx);

	do_requires (installed, available, reqpkgs, pkg, filters, recursive);

	/* sort output */
	n_array_sort_ex(reqpkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);

	for (i = 0; i < n_array_size (reqpkgs); i++) {
		struct pkg	*p = n_array_nth (reqpkgs, i);

		poldek_backend_package (job, p, PK_INFO_ENUM_UNKNOWN, filters);
	}

	n_array_free (reqpkgs);
	n_array_free (installed);
	n_array_free (available);
}

void
pk_backend_required_by (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, FALSE, TRUE);
	pb_error_clean ();

	pk_backend_job_thread_create (job, backend_required_by_thread, NULL, NULL);
}

/**
 * pk_backend_get_update_detail:
 */
static GPtrArray *
get_obsoletedby_pkg (struct pkg *pkg)
{
	GPtrArray *obsoletes = NULL;
	tn_array *dbpkgs;
	size_t i;

	g_return_val_if_fail (pkg != NULL, NULL);

	obsoletes = g_ptr_array_new ();

	/* get installed packages */
	dbpkgs = poclidek_get_dent_packages (priv->cctx, POCLIDEK_INSTALLEDDIR);

	if (dbpkgs == NULL)
		return NULL;

	for (i = 0; i < n_array_size (dbpkgs); i++) {
		struct pkg *dbpkg = n_array_nth (dbpkgs, i);

		if (pkg_caps_obsoletes_pkg_caps (pkg, dbpkg)) {
			g_ptr_array_add (obsoletes, package_id_from_pkg (dbpkg, "installed", 0));
		}
	}

	n_array_free (dbpkgs);

	return obsoletes;
}

static void
backend_get_update_detail_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar **package_ids = NULL;
	guint n;

	g_variant_get (params, "(^a&s)",
		       &package_ids);

	pb_load_packages (job);

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
				GPtrArray *obsoletes = NULL;
				GPtrArray *updates = NULL;
				GPtrArray *cve_urls = NULL;
				const gchar *changes = NULL;
				tn_array *cves = NULL;
				struct pkguinf *upkg_uinf = NULL;

				upkg = poldek_get_pkg_from_package_id (package_ids[n]);

				updates = g_ptr_array_new ();
				g_ptr_array_add (updates, package_id_from_pkg (pkg, "installed", 0));

				obsoletes = get_obsoletedby_pkg (upkg);

				if ((upkg_uinf = pkg_uinf (upkg)) != NULL) {
					changes = pkguinf_get_changelog (upkg_uinf, pkg->btime);
				}

				cve_urls = g_ptr_array_new ();
				if ((cves = poldek_pkg_get_cves_from_pld_changelog (upkg, pkg->btime))) {
					guint i;

					for (i = 0; i < n_array_size (cves); i++) {
						gchar *cve = n_array_nth (cves, i);

						g_ptr_array_add (cve_urls,
								 g_strdup_printf ("http://web.nvd.nist.gov/view/vuln/detail?vulnId=%s", cve));
					}
				}

				g_ptr_array_add (updates, NULL);
				g_ptr_array_add (obsoletes, NULL);
				g_ptr_array_add (cve_urls, NULL);

				pk_backend_job_update_detail (job,
							  package_ids[n],
							  (gchar **) updates->pdata,
							  (gchar **) obsoletes->pdata,
							  NULL, // vendor urls
							  NULL, // bugzilla urls
							  (gchar **) cve_urls->pdata,
							  PK_RESTART_ENUM_NONE,
							  "", // update text
							  changes,
							  PK_UPDATE_STATE_ENUM_UNKNOWN,
							  NULL, // issued
							  NULL); // updated

				g_ptr_array_unref (updates);
				g_ptr_array_unref (obsoletes);
				g_ptr_array_unref (cve_urls);

				n_array_cfree (&cves);
			}

			n_array_free (packages);
		} else {
			pk_backend_job_error_code (job,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to find package %s",
					       package_ids[n]);
		}

		g_strfreev (parts);
	}
}

void
pk_backend_get_update_detail (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, FALSE, TRUE);
	pb_error_clean ();

	pk_backend_job_thread_create (job, backend_get_update_detail_thread, NULL, NULL);
}

/**
 * backend_get_updates_thread:
 */
static void
backend_get_updates_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	tn_array *packages = NULL;

	pb_load_packages (job);

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
				poldek_backend_package (job, pkg, PK_INFO_ENUM_BLOCKED, PK_FILTER_ENUM_NONE);
			else if (poldek_pkg_in_array (pkg, secupgrades, (tn_fn_cmp)pkg_cmp_name_evr))
				poldek_backend_package (job, pkg, PK_INFO_ENUM_SECURITY, PK_FILTER_ENUM_NONE);
			else
				poldek_backend_package (job, pkg, PK_INFO_ENUM_NORMAL, PK_FILTER_ENUM_NONE);
		}

		n_array_cfree (&secupgrades);
		n_array_free (packages);
	}

	if (sigint_reached ())
		pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Action cancelled.");
}

void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, TRUE, TRUE);
	pb_error_clean ();

	pk_backend_job_thread_create (job, backend_get_updates_thread, NULL, NULL);
}

/**
 * backend_install_packages_thread:
 */
static void
backend_install_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar			*command;
	struct vf_progress	vf_progress;
	gchar **package_ids;
	PkBitfield transaction_flags;
	GString *cmd;
	size_t i;

	/* FIXME: support only_trusted */
	g_variant_get (params, "(t^a&s)",
		       &transaction_flags,
		       &package_ids);

	setup_vf_progress (&vf_progress, job);

	pb_load_packages (job);

	cmd = g_string_new ("install ");

	if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD)) {
		g_string_append_printf (cmd, "--fetch ");
	}

	/* prepare command */
	for (i = 0; i < g_strv_length (package_ids); i++) {
		gchar	*nvra = poldek_get_nvra_from_package_id (package_ids[i]);

		g_string_append_printf (cmd, "%s ", nvra);

		g_free (nvra);
	}

	command = g_string_free (cmd, FALSE);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);

	if (execute_command ("%s", command)) {
		pb_error_check (job);
	} else {
		pb_error_show (job, PK_ERROR_ENUM_TRANSACTION_ERROR);
	}

	g_free (command);
}

void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job,
			     PkBitfield transaction_flags,
			     gchar **package_ids)
{
	if (!pk_backend_is_online (backend)) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_NETWORK, "Cannot install package when offline!");
		pk_backend_job_finished (job);
		return;
	}

	poldek_backend_set_allow_cancel (job, TRUE, TRUE);
	pb_error_clean ();

	if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		pk_backend_job_thread_create (job, do_simulate_packages, NULL, NULL);
	} else {
		pk_backend_job_thread_create (job, backend_install_packages_thread, NULL, NULL);
	}
}

/**
 * FIXME: force currently omited
 * pk_backend_refresh_cache:
 */
static void
backend_refresh_cache_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	PkBackendPoldekJobData *job_data = pk_backend_job_get_user_data (job);
	PkBackendPoldekProgressData *pd = job_data->progress_data;
	tn_array		*sources = NULL;
	struct vf_progress	vfpro;

	setup_vf_progress (&vfpro, job);

	pk_backend_job_set_percentage (job, 1);

	sources = poldek_get_sources (priv->ctx);

	if (sources) {
		size_t	i;

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

	poldek_reload (job, TRUE);

	pk_backend_job_set_percentage (job, 100);
}

void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
	if (!pk_backend_is_online (backend)) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache when offline!");
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);
	poldek_backend_set_allow_cancel (job, TRUE, TRUE);
	pb_error_clean ();

	pk_backend_job_thread_create (job, backend_refresh_cache_thread, NULL, NULL);
}

/**
 * backend_remove_packages_thread:
 */
static void
backend_remove_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	GString *cmd;
	gchar *command;
	PkBitfield transaction_flags;
	gchar **package_ids;
	gboolean allow_deps;
	gboolean autoremove;
	size_t i;

	g_variant_get (params, "(t^a&sbb)",
                       &transaction_flags,
                       &package_ids,
                       &allow_deps,
                       &autoremove);

	pb_load_packages (job);

	cmd = g_string_new ("uninstall ");

	/* prepare command */
	for (i = 0; i < g_strv_length (package_ids); i++) {
		gchar	*nvra = poldek_get_nvra_from_package_id (package_ids[i]);

		g_string_append_printf (cmd, "%s ", nvra);

		g_free (nvra);
	}

	command = g_string_free (cmd, FALSE);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);

	if (execute_command ("%s", command) == FALSE) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE, "%s", pberror->tslog->str);
	}

	g_free (command);
}

void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job,
			    PkBitfield transaction_flags,
			    gchar **package_ids,
			    gboolean allow_deps,
			    gboolean autoremove)
{
	poldek_backend_set_allow_cancel (job, FALSE, TRUE);
	pb_error_clean ();

	if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		pk_backend_job_thread_create (job, do_simulate_packages, NULL, NULL);
	} else {
		pk_backend_job_thread_create (job, backend_remove_packages_thread, NULL, NULL);
	}
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **packages)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, TRUE, TRUE);

	pk_backend_job_thread_create (job, search_package_thread, NULL, NULL);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **search)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, TRUE, TRUE);
	pb_error_clean ();
	pk_backend_job_thread_create (job, search_package_thread, NULL, NULL);
}

/**
 * pk_backend_search_files:
 */
void
pk_backend_search_files (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **search)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, TRUE, TRUE);
	pb_error_clean ();
	pk_backend_job_thread_create (job, search_package_thread, NULL, NULL);
}

/**
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **search)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, TRUE, TRUE);
	pb_error_clean ();
	pk_backend_job_thread_create (job, search_package_thread, NULL, NULL);
}

/**
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **search)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, TRUE, TRUE);
	pb_error_clean ();
	pk_backend_job_thread_create (job, search_package_thread, NULL, NULL);
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	if (!pk_backend_is_online (backend)) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_NETWORK, "Cannot update packages when offline!");
		pk_backend_job_finished (job);
		return;
	}

	poldek_backend_set_allow_cancel (job, TRUE, TRUE);
	pb_error_clean ();

	if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		pk_backend_job_thread_create (job, do_simulate_packages, NULL, NULL);
	} else {
		pk_backend_job_thread_create (job, update_packages_thread, NULL, NULL);
	}
}

/**
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	tn_array *sources = NULL;

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, FALSE, TRUE);
	pb_error_clean ();

	sources = poldek_get_sources (priv->ctx);

	if (sources) {
		size_t i;

		for (i = 0; i < n_array_size (sources); i++) {
			struct source *src = n_array_nth (sources, i);
			gboolean enabled = TRUE;

			if (src->flags & PKGSOURCE_NOAUTO)
				enabled = FALSE;

			pk_backend_job_repo_detail (job, src->path, src->name, enabled);
		}

		n_array_free (sources);
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_what_provides:
 */
void
pk_backend_what_provides (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	GPtrArray *array = NULL;
	guint i;

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	poldek_backend_set_allow_cancel (job, TRUE, TRUE);
	pb_error_clean ();

	/* prepare array of commands */
	array = g_ptr_array_new_with_free_func (g_free);

	for (i = 0; i < g_strv_length (values); i++) {
		g_ptr_array_add (array, g_strdup_printf ("%s", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("gstreamer0.10\\(%s\\)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("mimetype\\(%s\\)", values[i]));
	}

	pk_backend_job_thread_create (job, search_package_thread, array, NULL);
}
