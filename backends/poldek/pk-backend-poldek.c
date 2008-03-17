/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Marcin Banasiak <megabajt@pld-linux.org>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <pk-backend.h>
#include <pk-backend-thread.h>
#include <pk-filter.h>
#include <pk-network.h>

#include <log.h>
#include <capreq.h>
#include <poldek.h>
#include <poclidek/poclidek.h>
#include <pkgdir/pkgdir.h>
#include <pkgdir/source.h>
#include <pkgu.h>
#include <pkgfl.h>
#include <pkgmisc.h>
#include <pm/pm.h>
#include <vfile/vfile.h>

static gchar* poldek_pkg_evr (const struct pkg *pkg);
static void poldek_backend_package (const struct pkg *pkg, gint status);
static long do_get_bytes_to_download (const struct poldek_ts *ts, const gchar *mark);

enum {
	PROGRESS_ENUM_INSTALL,
	PROGRESS_ENUM_REFRESH_CACHE
};

enum {
	SEARCH_ENUM_NAME,
	SEARCH_ENUM_GROUP,
	SEARCH_ENUM_DETAILS,
	SEARCH_ENUM_FILE
};

typedef struct {
	gint		mode;
	PkFilter	*filter;
	gchar		*search;
} SearchData;

/* used by get_depends and get_requires */
typedef struct {
	gchar		*package_id;
	PkFilter	*filter;
	gboolean	recursive;
} DepsData;

typedef struct {
	gchar		*package_id;
	gboolean	allow_deps;
} RemoveData;

typedef struct {
	gint		mode;
	gint		step; // current step

	/* Numer of sources to update. It's used only by refresh cache,
	 * as each source can have multiple files to download. I don't
	 * know how to get numer of files which will be downloaded. */
	gint		nsources;

	long		bytesget;
	long		bytesdownload;

	gint		percentage;
	gint		subpercentage;
} PercentageData;

/* used by install / update */
typedef struct {
	PercentageData	*pd;
	gchar		*package_id;
} InstallData;

typedef struct {
	InstallData	*idata;
	RemoveData	*rdata;
} TsConfirmData;

/* global variables */
static PkBackendThread	*thread;
static PkNetwork	*network;

static gint verbose = 1;
static gint ref = 0;

static struct poldek_ctx	*ctx = NULL;
static struct poclidek_ctx	*cctx = NULL;

/**
 * poldek_get_bytes_to_download:
 *
 * Returns: bytes to download
 */
static long
poldek_get_bytes_to_download (const struct poldek_ts *ts)
{
	long	bytes = 0;

	bytes += do_get_bytes_to_download (ts, "I");
	bytes += do_get_bytes_to_download (ts, "D");

	return bytes;
}

static long
do_get_bytes_to_download (const struct poldek_ts *ts, const gchar *mark)
{
	tn_array	*pkgs = NULL;
	gint		i;
	long		bytes = 0;

	pkgs = poldek_ts_get_summary (ts, mark);

	if (pkgs) {
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
		n_array_free (pkgs);
	}

	return bytes;
}



/**
 * FIXME: better refresh_cache progress
 * VF_PROGRESS
 */
static void*
poldek_vf_progress_new (void *data, const gchar *label)
{
	PercentageData	*bar = (PercentageData*) data;
	if (bar->mode == PROGRESS_ENUM_INSTALL) {
	gchar		*filename = g_path_get_basename (label), *pkgname, *command;
	struct poclidek_rcmd *rcmd;
	tn_array	*pkgs = NULL;
	struct pkg	*pkg = NULL;

	pkgname = g_strndup (filename, (sizeof(gchar)*strlen(filename)-4));

	command = g_strdup_printf ("cd /all-avail; ls -q %s", pkgname);

	rcmd = poclidek_rcmd_new (cctx, NULL);
	poclidek_rcmd_execline (rcmd, command);
	pkgs = poclidek_rcmd_get_packages (rcmd);

	if (pkgs) {
		pkg = n_array_nth (pkgs, 0);

		poldek_backend_package (pkg, PK_INFO_ENUM_DOWNLOADING);
	}

	poclidek_rcmd_free (rcmd);

	g_free (pkgname);
	g_free (filename);
	}
	return bar;
}

static void
poldek_vf_progress (void *bar, long total, long amount)
{
	PercentageData	*pd = (PercentageData*) bar;
	gint		tmp_subpercentage;
	PkBackend	*backend;

	backend = pk_backend_thread_get_backend (thread);

	tmp_subpercentage = (gint)(((float)amount / (float)total) * 100);

	if (pd->mode == PROGRESS_ENUM_REFRESH_CACHE) {
		if (tmp_subpercentage >= pd->subpercentage) {
			if (pd->step - 1 == 0)
				pd->percentage = 1;
			else
				pd->percentage = (gint)(((pd->step - 1) / pd->nsources) * 100);

			pd->subpercentage = tmp_subpercentage;
		} else {
			if (pd->step == pd->nsources) {
				pd->percentage = 100;
			} else {
				pd->percentage = (gint)(((float)pd->step / (float)pd->nsources) * 100);
			}

			pd->subpercentage = 100;
		}
	} else {
		if (tmp_subpercentage >= pd->subpercentage) {
			pd->percentage = (gint)(((float)(pd->bytesget + amount) / (float)pd->bytesdownload) * 100);
			pd->subpercentage = tmp_subpercentage;
		} else {
			pd->bytesget += total;
			pd->subpercentage = 100;
		}
	}
	pk_backend_set_percentage (backend, pd->percentage);
	pk_backend_set_sub_percentage (backend, pd->subpercentage);
}

static void
poldek_vf_progress_reset (void *bar)
{
	PercentageData *pd = (PercentageData*) bar;

	pd->subpercentage = 0;
}

/**
 * ts_confirm:
 * Returns Yes - 1
 *	    No - 0
 */
static int
ts_confirm (void *data, struct poldek_ts *ts)
{
	tn_array	*ipkgs = NULL, *dpkgs = NULL, *rpkgs = NULL, *upkgs = NULL;
	TsConfirmData	*tsd = (TsConfirmData *)data;
	gint		i = 0, j = 0, result = 1;

	ipkgs = poldek_ts_get_summary (ts, "I");
	dpkgs = poldek_ts_get_summary (ts, "D");
	rpkgs = poldek_ts_get_summary (ts, "R");

	switch (poldek_ts_get_type (ts)) {
		case POLDEK_TS_TYPE_INSTALL:
			upkgs = n_array_new (2, NULL, NULL);

			tsd->idata->pd->step = 0;
			tsd->idata->pd->bytesdownload = poldek_get_bytes_to_download (ts);

			if (rpkgs) {
				for (i = 0; i < n_array_size (ipkgs); i++) {
					struct pkg	*ipkg = n_array_nth (ipkgs, i);

					for (j = 0; j < n_array_size (rpkgs); j++) {
						struct pkg	*rpkg = n_array_nth (rpkgs, j);

						if (pkg_cmp_name (ipkg, rpkg) == 0) {
							n_array_push (upkgs, pkg_link (rpkg));
							break;
						}
					}

				}

				if (dpkgs) {
					for (i = 0; i < n_array_size (dpkgs); i++) {
						struct pkg	*dpkg = n_array_nth (dpkgs, i);

						for (j = 0; j < n_array_size (rpkgs); j++) {
							struct pkg	*rpkg = n_array_nth (rpkgs, j);

							if (pkg_cmp_name (dpkg, rpkg) == 0) {
								n_array_push (upkgs, pkg_link (rpkg));
								break;
							}
						}
					}
				}
			}

			if (rpkgs) {
				for (i = 0; i < n_array_size (rpkgs); i++) {
					struct pkg	*rpkg = n_array_nth (rpkgs, i);
					gboolean	canprint = TRUE;

					for (j = 0; j < n_array_size (upkgs); j++) {
						struct pkg	*upkg = n_array_nth (upkgs, j);

						if (pkg_cmp_name (upkg, rpkg) == 0) {
							canprint = FALSE;
							break;
						}
					}
					if (canprint)
						poldek_backend_package (rpkg, PK_INFO_ENUM_REMOVING);
				}
			}

			if (dpkgs) {
				for (i = 0; i < n_array_size (dpkgs); i++) {
					struct pkg	*dpkg = n_array_nth (dpkgs, i);
					gboolean	canprint = TRUE;

					for (j = 0; j < n_array_size (upkgs); j++) {
						struct pkg	*upkg = n_array_nth (upkgs, j);

						if (pkg_cmp_name (upkg, dpkg) == 0) {
							canprint = FALSE;
							break;
						}
					}
					if (canprint)
						poldek_backend_package (dpkg, PK_INFO_ENUM_INSTALLING);
				}
			}

			if (ipkgs) {
				for (i = 0; i < n_array_size (ipkgs); i++) {
					struct pkg	*ipkg = n_array_nth (ipkgs, i);
					gboolean	canprint = TRUE;

					for (j = 0; j < n_array_size (upkgs); j++) {
						struct pkg	*upkg = n_array_nth (upkgs, j);

						if (pkg_cmp_name (upkg, ipkg) == 0) {
							canprint = FALSE;
							break;
						}
					}
					if (canprint)
						poldek_backend_package (ipkg, PK_INFO_ENUM_INSTALLING);
				}
			}

			for (i = 0; i < n_array_size (upkgs); i++) {
				struct pkg	*upkg = n_array_nth (upkgs, i);

				poldek_backend_package (upkg, PK_INFO_ENUM_UPDATING);
			}

			n_array_free (upkgs);

			break;
		case POLDEK_TS_TYPE_UNINSTALL:
			if (dpkgs) {
				if ((tsd->rdata->allow_deps == FALSE) && (n_array_size (dpkgs) > 0)) {
					result = 0;
					break;
				}

				for (i = 0; i < n_array_size (dpkgs); i++) {
					struct pkg	*pkg = n_array_nth (dpkgs, i);

					poldek_backend_package (pkg, PK_INFO_ENUM_REMOVING);
				}
			}

			if (rpkgs) {
				for (i = 0; i < n_array_size (rpkgs); i++) {
					struct pkg	*pkg = n_array_nth (rpkgs, i);

					poldek_backend_package (pkg, PK_INFO_ENUM_REMOVING);
				}
			}

			break;
	}

	if (ipkgs)
		n_array_free (ipkgs);

	if (dpkgs)
		n_array_free (dpkgs);

	if (rpkgs)
		n_array_free (rpkgs);

	return result;
}

/**
 * setup_vf_progress:
 */
static void
setup_vf_progress (struct vf_progress *vf_progress, PercentageData *pd)
{
	vf_progress->data = pd;
	vf_progress->new = poldek_vf_progress_new;
	vf_progress->progress = poldek_vf_progress;
	vf_progress->reset = poldek_vf_progress_reset;
	vf_progress->free = NULL;

	vfile_configure (VFILE_CONF_VERBOSE, &verbose);
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
	PkPackageId	*pi;
	gchar		*vr, *result;

	pi = pk_package_id_new_from_string (package_id);
	vr = poldek_get_vr_from_package_id_evr (pi->version);

	result = g_strdup_printf ("%s-%s.%s", pi->name, vr, pi->arch);

	g_free (vr);
	pk_package_id_free (pi);

	return result;
}

/**
 * poldek_get_installed_packages:
 */
static tn_array*
poldek_get_installed_packages (void)
{
	struct poclidek_rcmd	*rcmd = NULL;
	tn_array		*arr = NULL;

	rcmd = poclidek_rcmd_new (cctx, NULL);
	poclidek_rcmd_execline (rcmd, "cd /installed; ls -q *");

	arr = poclidek_rcmd_get_packages (rcmd);

	poclidek_rcmd_free (rcmd);

	return arr;
}

/**
 * do_requires:
 */
static void
do_requires (tn_array *installed, tn_array *available, tn_array *requires, struct pkg *pkg, DepsData *data)
{
	gint	i;

	if (data->filter->installed) {
		for (i = 0; i < n_array_size (installed); i++) {
			struct pkg      *ipkg = n_array_nth (installed, i);
			int j;

			/* self match */
			if (pkg_cmp_name_evr (pkg, ipkg) == 0)
				continue;

	                /* skip when there is no reqs */
        	        if (!ipkg->reqs)
				continue;

			for (j = 0; j < n_array_size (ipkg->reqs); j++) {
				struct capreq   *req = n_array_nth (ipkg->reqs, j);

				if (capreq_is_rpmlib (req))
					continue;
				else if (capreq_is_file (req))
					continue;

				if (pkg_satisfies_req (pkg, req, 1)) {
					n_array_push (requires, pkg_link (ipkg));
                                	break;
				}
                	}
                }
        }
        if (data->filter->not_installed) {
	        for (i = 0; i < n_array_size (available); i++) {
        	        struct pkg      *apkg = n_array_nth (available, i);
	                int j;

			/* self match */
			if (pkg_cmp_name_evr (pkg, apkg) == 0)
				continue;

	                if (!apkg->reqs)
	                        continue;

	                for (j = 0; j < n_array_size (apkg->reqs); j++) {
	                        struct capreq   *req = n_array_nth (apkg->reqs, j);

	                        if (capreq_is_rpmlib (req))
	                                continue;
	                        else if (capreq_is_file (req))
	                                continue;

	                        if (pkg_satisfies_req (pkg, req, 1)) {
	                                int k, res = -1;
	                                for (k = 0; k < n_array_size (requires); k++) {
						struct pkg      *p = n_array_nth (requires, k);

	                                        if (pkg_cmp_name_evr_rev (apkg, p) == 0) {
	                                                res = k;
	                                                break;
	                                        }
	                                }

	                                if (res == -1)
	                                        n_array_push (requires, pkg_link (apkg));

	                                break;
                        	}
                	}
        	}
        }
}

/**
 * do_depends:
 */
static void
do_depends (tn_array *installed, tn_array *available, tn_array *depends, struct pkg *pkg, DepsData *data)
{
	tn_array	*reqs = pkg->reqs;
	tn_array	*tmp = NULL;
	gint		i;

	tmp = n_array_new (2, NULL, NULL);

	/* nothing to do */
	if (!reqs || (reqs && n_array_size (reqs) < 1))
		return;

	for (i = 0; i < n_array_size (reqs); i++) {
		struct capreq	*req = n_array_nth (reqs, i);
		gboolean	found = FALSE;
		gint		j;

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
		if (data->filter->installed) {
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
		if (data->filter->not_installed) {
			for (j = 0; j < n_array_size (available); j++) {
				struct pkg	*p = n_array_nth (available, j);

				if (pkg_satisfies_req (p, req, 1)) {
					n_array_push (depends, pkg_link (p));
					n_array_push (tmp, pkg_link (p));
					break;
				}
			}
		}
	}

	if (data->recursive && tmp && n_array_size (tmp) > 0) {
		for (i = 0; i < n_array_size (tmp); i++) {
			struct pkg	*p = n_array_nth (tmp, i);

			do_depends (installed, available, depends, p, data);
		}
	}

	n_array_free (tmp);
}

/**
 * poldek_backend_package:
 */
static void
poldek_backend_package (const struct pkg *pkg, gint status)
{
	PkBackend	*backend;
	struct pkguinf	*pkgu;
	gchar		*evr, *package_id, *poldek_dir;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);
	g_return_if_fail (backend != NULL);

	evr = poldek_pkg_evr (pkg);

	if (pkg->recno == 0)
	{
		if (status == PK_INFO_ENUM_UNKNOWN)
			status = PK_INFO_ENUM_AVAILABLE;

		poldek_dir = g_strdup ("all-avail");
	} else {
		if (status == PK_INFO_ENUM_UNKNOWN)
			status = PK_INFO_ENUM_INSTALLED;

		poldek_dir = g_strdup ("installed");
	}

	package_id = pk_package_id_build (pkg->name,
					  evr,
					  pkg_arch (pkg),
					  poldek_dir);

	pkgu = pkg_uinf (pkg);

	if (pkgu) {
		pk_backend_package (backend, status, package_id, pkguinf_get (pkgu, PKGUINF_SUMMARY));
		pkguinf_free (pkgu);
	} else {
		pk_backend_package (backend, status, package_id, "");
	}

	g_free (evr);
	g_free (package_id);
	g_free (poldek_dir);
}

/**
 * poldek_get_pkg_from_package_id:
 */
static struct pkg*
poldek_get_pkg_from_package_id (const gchar *package_id)
{
	PkPackageId		*pi;
	struct poclidek_rcmd	*rcmd;
	struct pkg		*result = NULL;
	gchar			*vr, *command;

	pi = pk_package_id_new_from_string (package_id);

	rcmd = poclidek_rcmd_new (cctx, NULL);

	vr = poldek_get_vr_from_package_id_evr (pi->version);
	command = g_strdup_printf ("cd /%s; ls -q %s-%s.%s", pi->data, pi->name, vr, pi->arch);

	if (poclidek_rcmd_execline (rcmd, command))
	{
		tn_array	*pkgs = NULL;

		pkgs = poclidek_rcmd_get_packages (rcmd);

		if (n_array_size (pkgs) > 0)
		{
			/* only one package is needed */
			result = pkg_link (n_array_nth (pkgs, 0));
		}
		n_array_free (pkgs);
	}

	poclidek_rcmd_free (rcmd);

	pk_package_id_free (pi);

	g_free (vr);
	g_free (command);

	return result;
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

/**
 * search_package:
 */
static gboolean
search_package (PkBackendThread *thread, gpointer data)
{
	SearchData	 	*d = (SearchData*) data;
	gchar			*search_inst = NULL;
	struct poclidek_rcmd	*cmd = NULL;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);
	g_return_val_if_fail (backend != NULL, FALSE);

	cmd = poclidek_rcmd_new (cctx, NULL);

	switch (d->mode)
	{
		case SEARCH_ENUM_NAME:
			search_inst = g_strdup ("ls -q");
			break;
		case SEARCH_ENUM_GROUP:
			search_inst = g_strdup ("search -qg");
			break;
		case SEARCH_ENUM_DETAILS:
			search_inst = g_strdup ("search -dsq");
			break;
		case SEARCH_ENUM_FILE:
			search_inst = g_strdup ("search -qlf");
			break;
		default:
			/* Error */
			break;
	}

	if (cmd != NULL && search_inst)
	{
		gchar		*command = NULL;
		tn_array	*pkgs = NULL, *installed = NULL, *available = NULL;

		if (d->filter->installed)
		{
			command = g_strdup_printf ("cd /installed; %s *%s*", search_inst, d->search);
			if (poclidek_rcmd_execline (cmd, command))
				installed = poclidek_rcmd_get_packages (cmd);

			g_free (command);
		}
		if (d->filter->not_installed)
		{
			command = g_strdup_printf ("cd /all-avail; %s *%s*", search_inst, d->search);
			if (poclidek_rcmd_execline (cmd, command))
				available = poclidek_rcmd_get_packages (cmd);

			g_free (command);
		}

		if (d->filter->installed && d->filter->not_installed && installed && available)
		{
			pkgs = n_array_concat_ex (installed, available, (tn_fn_dup)pkg_link);

			n_array_sort_ex(pkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev_recno);

			n_array_free (installed);
			n_array_free (available);
		}
		else if (!d->filter->installed || available)
			pkgs = available;
		else if (!d->filter->not_installed || installed)
			pkgs = installed;

		if (pkgs)
		{
			gint	i;

			for (i = 0; i < n_array_size (pkgs); i++)
			{
				struct pkg	*pkg = n_array_nth (pkgs, i);

				/* development filter */
				if (!d->filter->devel || !d->filter->not_devel)
				{
					/* devel in filter */
					if (d->filter->devel && !poldek_pkg_is_devel (pkg))
						continue;

					/* ~devel in filter */
					if (d->filter->not_devel && poldek_pkg_is_devel (pkg))
						continue;
				}

				/* gui filter */
				if (!d->filter->gui || !d->filter->not_gui)
				{
					/* gui in filter */
					if (d->filter->gui && !poldek_pkg_is_gui (pkg))
						continue;

					/* ~gui in filter */
					if (d->filter->not_gui && poldek_pkg_is_gui (pkg))
						continue;
				}

				poldek_backend_package (pkg, PK_INFO_ENUM_UNKNOWN);
			}
			n_array_free (pkgs);
		} else {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "Package not found");
		}

		g_free (search_inst);

		poclidek_rcmd_free (cmd);
	}

	pk_filter_free (d->filter);
	g_free (d->search);
	g_free (d);

	pk_backend_finished (backend);

	return TRUE;
}

static void
poldek_backend_log (void *data, int pri, char *message)
{
	const gchar *msg = strchr (message, ':');
	PkBackend	*backend;

	backend = pk_backend_thread_get_backend (thread);
	if (msg) {

		if (strcmp (msg+(2*sizeof(char)), "equal version installed, skipped\n") == 0)
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED, "Package already installed");
		else if (strcmp (msg+(2*sizeof(char)), "refusing to upgrade held package\n") == 0)
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, "Refusing to upgrade held package");
	}
}

/**
 * backend_initalize:
 */
static void
backend_initalize (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	thread = pk_backend_thread_new ();

	/* reference count for the global variables */
	if (ref++ > 1)
		return;

	network = pk_network_new ();

	poldeklib_init ();

	ctx = poldek_new (0);
	poldek_load_config (ctx, "/etc/poldek/poldek.conf", NULL, 0);

	poldek_setup (ctx);

	cctx = poclidek_new (ctx);

	poldek_set_verbose (1);
	/* disable LOGFILE and LOGTTY logging */
	poldek_configure (ctx, POLDEK_CONF_LOGFILE, NULL);
	poldek_configure (ctx, POLDEK_CONF_LOGTTY, NULL);

	poldek_log_set_appender ("PackageKit", NULL, NULL, 0, (poldek_vlog_fn)poldek_backend_log);

	/* unique package names */
	poldek_configure (ctx, POLDEK_CONF_OPT, POLDEK_OP_UNIQN, 1);

	/* poldek has to ask. Otherwise callbacks won't be used */
	poldek_configure (ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_INST, 1);
	poldek_configure (ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_UNINST, 1);
	/* (...), but we don't need choose_equiv callback */
	poldek_configure (ctx, POLDEK_CONF_OPT, POLDEK_OP_EQPKG_ASKUSER, 0);
}
/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	g_object_unref (thread);

	if (ref-- > 0)
		return;

	poclidek_free (cctx);
	poldek_free (ctx);

	poldeklib_destroy ();
}

/**
 * backend_get_filters:
 */
static void
backend_get_filters (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);

	pk_enum_list_append_multiple (elist,
				      PK_FILTER_ENUM_GUI,
				      PK_FILTER_ENUM_INSTALLED,
				      PK_FILTER_ENUM_DEVELOPMENT,
				    /*  PK_FILTER_ENUM_FREE,*/
				      -1);
}

/**
 * backend_get_depends:
 */
static gboolean
backend_get_depends_thread (PkBackendThread *thread, gpointer data)
{
	DepsData	*d = (DepsData*) data;
	PkBackend	*backend;
	struct pkg	*pkg;
	tn_array	*deppkgs, *available, *installed;
	gint		i;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);
	g_return_val_if_fail (backend != NULL, FALSE);

	deppkgs = n_array_new (2, NULL, NULL);

	installed = poldek_get_installed_packages ();
	available = poldek_get_avail_packages (ctx);

	pkg = poldek_get_pkg_from_package_id (d->package_id);

	do_depends (installed, available, deppkgs, pkg, d);

	n_array_sort_ex(deppkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);

	for (i = 0; i < n_array_size (deppkgs); i++) {
		struct pkg	*p = n_array_nth (deppkgs, i);

		poldek_backend_package (p, PK_INFO_ENUM_UNKNOWN);
	}

	pkg_free (pkg);

	n_array_free (deppkgs);
	n_array_free (available);
	n_array_free (installed);

	g_free (d->package_id);
	pk_filter_free (d->filter);
	g_free (d);

	pk_backend_finished (backend);

	return TRUE;
}

static void
backend_get_depends (PkBackend *backend, const gchar *filter, const gchar *package_id, gboolean recursive)
{
	DepsData	*data = g_new0 (DepsData, 1);

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	data->package_id = g_strdup (package_id);
	data->filter = pk_filter_new_from_string (filter);
	data->recursive = recursive;
	pk_backend_thread_create (thread, backend_get_depends_thread, data);
}

/**
 * backend_get_description:
 */
static gboolean
backend_get_description_thread (PkBackendThread *thread, gchar *package_id)
{
	PkBackend	*backend;
	struct pkg	*pkg = NULL;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);
	g_return_val_if_fail (backend != NULL, FALSE);

	pkg = poldek_get_pkg_from_package_id (package_id);

	if (pkg)
	{
		struct pkguinf	*pkgu = NULL;

		pkgu = pkg_uinf (pkg);

		if (pkgu) {
			pk_backend_description (backend,
						package_id,
						pkguinf_get (pkgu, PKGUINF_LICENSE),
						PK_GROUP_ENUM_OTHER,
						pkguinf_get (pkgu, PKGUINF_DESCRIPTION),
						pkguinf_get (pkgu, PKGUINF_URL),
						pkg->size);
			pkguinf_free (pkgu);
		} else {
			pk_backend_description (backend,
						package_id,
						"",
						PK_GROUP_ENUM_OTHER,
						"",
						"",
						pkg->size);
		}

		pkg_free (pkg);
	}

	g_free (package_id);

	pk_backend_finished (backend);

	return TRUE;
}

static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	pk_backend_thread_create (thread,
				  (PkBackendThreadFunc)backend_get_description_thread,
				  g_strdup (package_id));
}

/**
 * backend_get_files:
 */
static gboolean
backend_get_files_thread (PkBackendThread *thread, gchar *package_id)
{
	PkBackend	*backend;
	struct pkg	*pkg;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);
	g_return_val_if_fail (backend != NULL, FALSE);

	pkg = poldek_get_pkg_from_package_id (package_id);

	if (pkg)
	{
		struct pkgflist		*flist = pkg_get_flist (pkg);
		GString			*filelist;
		gchar			*result, *sep;
		gint			i, j;

		sep = "";

		if (!flist)
		{
			pkg_free (pkg);
			pk_backend_finished (backend);
			return TRUE;
		}

		filelist = g_string_new ("");

		for (i = 0; i < n_tuple_size (flist->fl); i++)
		{
			struct pkgfl_ent	*flent = n_tuple_nth (flist->fl, i);
			gchar			*dirname;

			dirname = g_strdup_printf ("%s%s", *flent->dirname == '/' ? "" : "/", flent->dirname);

			for (j = 0; j < flent->items; j++)
			{
				struct flfile	*f = flent->files[j];

				if (strcmp (dirname, "/") == 0)
					g_string_append_printf (filelist, "%s/%s", sep, f->basename);
				else
					g_string_append_printf (filelist, "%s%s/%s", sep, dirname, f->basename);

				sep = ";";
			}
			g_free (dirname);
		}

		result = g_string_free (filelist, FALSE);

		pk_backend_files (backend, package_id, result);

		if (result)
			g_free (result);

		pkg_free (pkg);
	}

	g_free (package_id);

	pk_backend_finished (backend);

	return TRUE;
}

static void
backend_get_files (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	pk_backend_thread_create (thread,
				  (PkBackendThreadFunc)backend_get_files_thread,
				  g_strdup (package_id));
}

/**
 * FIXME: recursive currently omited
 * backend_get_requires:
 */
static gboolean
backend_get_requires_thread (PkBackendThread *thread, gpointer data)
{
	DepsData	*d = (DepsData*) data;
	PkBackend	*backend;
	struct pkg	*pkg;
	tn_array	*reqpkgs, *available, *installed;
	gint		i;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);
	g_return_val_if_fail (backend != NULL, FALSE);

	reqpkgs = n_array_new (2, NULL, NULL);

	pkg = poldek_get_pkg_from_package_id (d->package_id);
	installed = poldek_get_installed_packages ();
	available = poldek_get_avail_packages (ctx);

	do_requires (installed, available, reqpkgs, pkg, d);

	/* sort output */
	n_array_sort_ex(reqpkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);

	for (i = 0; i < n_array_size (reqpkgs); i++) {
		struct pkg	*p = n_array_nth (reqpkgs, i);

		poldek_backend_package (p, PK_INFO_ENUM_UNKNOWN);
	}

	n_array_free (reqpkgs);
	n_array_free (installed);
	n_array_free (available);

	g_free (d->package_id);
	pk_filter_free (d->filter);
	g_free (d);

	pk_backend_finished (backend);

	return TRUE;
}

static void
backend_get_requires (PkBackend	*backend, const gchar *filter, const gchar *package_id, gboolean recursive)
{
	DepsData	*data = g_new0 (DepsData, 1);

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	data->package_id = g_strdup (package_id);
	data->filter = pk_filter_new_from_string (filter);
	data->recursive = recursive;
	pk_backend_thread_create (thread, backend_get_requires_thread, data);
}

/**
 * backend_get_updates:
 */
static gboolean
backend_get_updates_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend		*backend;
	struct poclidek_rcmd	*rcmd = NULL;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);
	g_return_val_if_fail (backend != NULL, FALSE);

	rcmd = poclidek_rcmd_new (cctx, NULL);

	if (rcmd) {
		if (poclidek_rcmd_execline (rcmd, "cd /all-avail; ls -q -u")) {
			tn_array	*pkgs = NULL;
			gint		i;

			pkgs = poclidek_rcmd_get_packages (rcmd);

			for (i = 0; i < n_array_size (pkgs); i++) {
				struct pkg	*pkg = n_array_nth (pkgs, i);

				poldek_backend_package (pkg, PK_INFO_ENUM_NORMAL);
			}
			n_array_free (pkgs);
		}
	}

	poclidek_rcmd_free (rcmd);

	pk_backend_finished (backend);

	return TRUE;
}

static void
backend_get_updates (PkBackend *backend, const gchar *filter)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	pk_backend_thread_create (thread, backend_get_updates_thread, NULL);
}

/**
 * backend_install_package:
 */
static gboolean
backend_install_package_thread (PkBackendThread *thread, gpointer data)
{
	InstallData		*id = (InstallData *)data;
	PkBackend		*backend;
	struct poldek_ts	*ts;
	struct poclidek_rcmd	*rcmd;
	gchar			*command, *nvra;
	struct vf_progress	vf_progress;
	TsConfirmData		*tcd = g_new0 (TsConfirmData, 1);

	tcd->idata = id;
	id->pd->mode = PROGRESS_ENUM_INSTALL;

	setup_vf_progress (&vf_progress, id->pd);

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);
	g_return_val_if_fail (backend != NULL, FALSE);

	/* setup callbacks */
	poldek_configure (ctx, POLDEK_CONF_TSCONFIRM_CB, ts_confirm, tcd);

	ts = poldek_ts_new (ctx, 0);
	rcmd = poclidek_rcmd_new (cctx, ts);

	nvra = poldek_get_nvra_from_package_id (id->package_id);
	command = g_strdup_printf ("install %s", nvra);

	if (!poclidek_rcmd_execline (rcmd, command))
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, "Package can't be installed!");
	}

	g_free (nvra);
	g_free (command);

	poldek_ts_free (ts);
	poclidek_rcmd_free (rcmd);

	g_free (id->pd);
	g_free (id->package_id);
	g_free (id);

	pk_backend_finished (backend);

	return TRUE;
}

static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	InstallData	*data = g_new0 (InstallData, 1);

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);

	if (pk_network_is_online (network) == FALSE) {
		/* free allocated memory */
		if (data)
			g_free (data);

		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install package when offline!");
		pk_backend_finished (backend);
		return;
	}

	data->package_id = g_strdup (package_id);
	data->pd = g_new0 (PercentageData, 1);
	pk_backend_thread_create (thread, backend_install_package_thread, data);
}

/**
 * FIXME: force currently omited
 * backend_refresh_cache:
 */
static gboolean
backend_refresh_cache_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend	*backend;
	tn_array	*sources = NULL;
	PercentageData	*pp = g_new0 (PercentageData, 1);
	struct vf_progress	vfpro;

	setup_vf_progress (&vfpro, pp);

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);
	g_return_val_if_fail (backend != NULL, FALSE);

	pk_backend_set_percentage (backend, 0);

	sources = poldek_get_sources (ctx);

	if (sources) {
		gint i;

		pp->mode = PROGRESS_ENUM_REFRESH_CACHE;
		pp->step = 0;

		for (i = 0; i < n_array_size (sources); i++) {
			struct source	*src = n_array_nth (sources, i);

			if (src->flags & PKGSOURCE_NOAUTOUP)
				continue;
			else
				pp->nsources++;
		}

		for (i = 0; i < n_array_size (sources); i++) {
			struct source	*src = n_array_nth (sources, i);

			if (src->flags & PKGSOURCE_NOAUTOUP)
				continue;

			pp->step++;
			source_update (src, 0);
		}
	}

	poclidek_free (cctx);
	poldek_free (ctx);

	ctx = poldek_new (0);
	poldek_load_config (ctx, "/etc/poldek/poldek.conf", NULL, 0);

	poldek_setup (ctx);

	cctx = poclidek_new (ctx);

	pk_backend_finished (backend);

	return TRUE;
}

static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);

	if (pk_network_is_online (network) == FALSE)
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache when offline!");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_thread_create (thread, backend_refresh_cache_thread, NULL);
}

/**
 * backend_remove_package:
 */
static gboolean
backend_remove_package_thread (PkBackendThread *thread, gpointer data)
{
	RemoveData	*d = (RemoveData*) data;
	TsConfirmData		*tcd = g_new0 (TsConfirmData, 1);
	PkBackend	*backend;
	struct poclidek_rcmd	*rcmd;
	struct poldek_ts	*ts;
	gchar			*nvra, *command;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);
	g_return_val_if_fail (backend != NULL, FALSE);

	tcd->rdata = d;

	/* setup callbacks */
	poldek_configure (ctx, POLDEK_CONF_TSCONFIRM_CB, ts_confirm, tcd);

	ts = poldek_ts_new (ctx, 0);
	rcmd = poclidek_rcmd_new (cctx, ts);

	nvra = poldek_get_nvra_from_package_id (d->package_id);
	command = g_strdup_printf ("uninstall %s", nvra);

	if (!poclidek_rcmd_execline (rcmd, command))
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE, "Package can't be removed!");
	}

	poclidek_load_packages (cctx, POCLIDEK_LOAD_RELOAD);

	g_free (nvra);
	g_free (command);

	poldek_ts_free (ts);
	poclidek_rcmd_free (rcmd);

	g_free (d->package_id);
	g_free (d);
	g_free (tcd);

	pk_backend_finished (backend);

	return TRUE;
}

static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps, gboolean autoremove)
{
	RemoveData	*data = g_new0 (RemoveData, 1);

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);

	data->package_id = g_strdup (package_id);
	data->allow_deps = allow_deps;
	pk_backend_thread_create (thread, backend_remove_package_thread, data);
}

/**
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, const gchar *filter, const gchar *package)
{
	SearchData	*data = g_new0 (SearchData, 1);

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	data->mode = SEARCH_ENUM_NAME;
	data->filter = pk_filter_new_from_string (filter);
	data->search = g_strdup (package);
	pk_backend_thread_create (thread, search_package, data);
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	SearchData	*data = g_new0 (SearchData, 1);

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	data->mode = SEARCH_ENUM_DETAILS;
	data->filter = pk_filter_new_from_string (filter);
	data->search = g_strdup (search);
	pk_backend_thread_create (thread, search_package, data);
}

/**
 * backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, const gchar *filter, const gchar *search)
{
	SearchData	*data = g_new0 (SearchData, 1);

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	data->mode = SEARCH_ENUM_FILE;
	data->filter = pk_filter_new_from_string (filter);
	data->search = g_strdup (search);
	pk_backend_thread_create (thread, search_package, data);
}

/**
 * backend_search_group:
 */
static void
backend_search_group (PkBackend *backend, const gchar *filter, const gchar *search)
{
	SearchData	*data = g_new0 (SearchData, 1);

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	data->mode = SEARCH_ENUM_GROUP;
	data->filter = pk_filter_new_from_string (filter);
	data->search = g_strdup (search);
	pk_backend_thread_create (thread, search_package, data);
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	SearchData	*data = g_new0 (SearchData, 1);

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	data->mode = SEARCH_ENUM_NAME;
	data->filter = pk_filter_new_from_string (filter);
	data->search = g_strdup (search);
	pk_backend_thread_create (thread, search_package, data);
}

/**
 * backend_update_package:
 */
static gboolean
backend_update_package_thread (PkBackendThread *thread, gpointer data)
{
	InstallData		*id = (InstallData *)data;
	PkBackend		*backend;
	struct poldek_ts	*ts;
	struct poclidek_rcmd	*rcmd;
	gchar			*command, *nvra;
	struct vf_progress	vf_progress;
	TsConfirmData		*tcd = g_new0 (TsConfirmData, 1);

	tcd->idata = id;

	setup_vf_progress (&vf_progress, id->pd);

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);
	g_return_val_if_fail (backend != NULL, FALSE);

	/* setup callbacks */
	poldek_configure (ctx, POLDEK_CONF_TSCONFIRM_CB, ts_confirm, tcd);

	ts = poldek_ts_new (ctx, 0);
	rcmd = poclidek_rcmd_new (cctx, ts);

	nvra = poldek_get_nvra_from_package_id (id->package_id);
	command = g_strdup_printf ("upgrade %s", nvra);

	if (!poclidek_rcmd_execline (rcmd, command))
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, "Cannot update package!");
	}

	g_free (nvra);
	g_free (command);

	poldek_ts_free (ts);
	poclidek_rcmd_free (rcmd);

	g_free (id->pd);
	g_free (id->package_id);
	g_free (id);

	pk_backend_finished (backend);

	return TRUE;
}

static void
backend_update_package (PkBackend *backend, const gchar *package_id)
{
	InstallData	*data = g_new0 (InstallData, 1);

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);

	if (pk_network_is_online (network) == FALSE) {
		/* free allocated memory */
		if (data)
			g_free (data);

		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot update package when offline!");
		pk_backend_finished (backend);
		return;
	}

	data->package_id = g_strdup (package_id);
	data->pd = g_new0 (PercentageData, 1);
	data->pd->mode = PROGRESS_ENUM_INSTALL;
	pk_backend_thread_create (thread, backend_update_package_thread, data);
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend)
{
	tn_array	*sources = NULL;

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	sources = poldek_get_sources (ctx);

	if (sources) {
		gint	i;

		for (i = 0; i < n_array_size (sources); i++) {
			struct source	*src = n_array_nth (sources, i);
			gboolean	enabled = TRUE;

			if (src->flags & PKGSOURCE_NOAUTO)
				enabled = FALSE;

			pk_backend_repo_detail (backend, src->name, src->path, enabled);
		}

		n_array_free (sources);
	}

	pk_backend_finished (backend);
}

PK_BACKEND_OPTIONS (
	"poldek",					/* description */
	"Marcin Banasiak <megabajt@pld-linux.org>",	/* author */
	backend_initalize,				/* initalize */
	backend_destroy,				/* destroy */
	NULL,						/* get_groups */
	backend_get_filters,				/* get_filters */
	NULL,						/* cancel */
	backend_get_depends,				/* get_depends */
	backend_get_description,			/* get_description */
	backend_get_files,				/* get_files */
	backend_get_requires,				/* get_requires */
	NULL,						/* get_update_detail */
	backend_get_updates,				/* get_updates */
	backend_install_package,			/* install_package */
	NULL,						/* install_file */
	backend_refresh_cache,				/* refresh_cache */
	backend_remove_package,				/* remove_package */
	backend_resolve,				/* resolve */
	NULL,						/* rollback */
	backend_search_details,				/* search_details */
	backend_search_file,				/* search_file */
	backend_search_group,				/* search_group */
	backend_search_name,				/* search_name */
	backend_update_package,				/* update_package */
	NULL,						/* update_system */
	backend_get_repo_list,				/* get_repo_list */
	NULL,						/* repo_enable */
	NULL,						/* repo_set_data */
	NULL						/* service pack */
);

