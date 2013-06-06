/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * Most of this code was taken from Zif, libzif/zif-transaction.c
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

#include <glib.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>

#include "hif-rpmts.h"
#include "hif-utils.h"

/**
 * hif_rpmts_add_install_filename:
 **/
gboolean
hif_rpmts_add_install_filename (rpmts ts,
				const gchar *filename,
				gboolean allow_untrusted,
				gboolean is_update,
				GError **error)
{
	gint res;
	Header hdr;
	FD_t fd;
	gboolean ret = FALSE;

	/* open this */
	fd = Fopen (filename, "r.ufdio");
	res = rpmReadPackageFile (ts,
				  fd,
				  filename,
				  &hdr);
	Fclose (fd);

	/* be less strict when we're allowing untrusted transactions */
	if (allow_untrusted) {
		switch (res) {
		case RPMRC_NOKEY:
		case RPMRC_NOTFOUND:
		case RPMRC_NOTTRUSTED:
		case RPMRC_OK:
			break;
		case RPMRC_FAIL:
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "signature does not verify for %s",
				     filename);
			goto out;
		default:
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "failed to open (generic error): %s",
				     filename);
			goto out;
		}
	} else {
		switch (res) {
		case RPMRC_OK:
			break;
		case RPMRC_NOTTRUSTED:
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "failed to verify key for %s",
				     filename);
			goto out;
		case RPMRC_NOKEY:
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "public key unavailable for %s",
				     filename);
			goto out;
		case RPMRC_NOTFOUND:
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "signature not found for %s",
				     filename);
			goto out;
		case RPMRC_FAIL:
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "signature does not verify for %s",
				     filename);
			goto out;
		default:
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "failed to open (generic error): %s",
				     filename);
			goto out;
		}
	}

	/* add to the transaction */
	res = rpmtsAddInstallElement (ts,
				      hdr,
				      (fnpyKey) filename,
				      is_update,
				      NULL);
	if (res != 0) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "failed to add install element: %s [%i]",
			     filename, res);
		goto out;
	}
	ret = TRUE;
out:
	return ret;
}

/**
 * hif_rpmts_get_problem_str:
 **/
static gchar *
hif_rpmts_get_problem_str (rpmProblem prob)
{
	const char *generic_str;
	const char *pkg_nevr;
	const char *pkg_nevr_alt;
	goffset diskspace;
	rpmProblemType type;
	gchar *str = NULL;

	/* get data from the problem object */
	type = rpmProblemGetType (prob);
	pkg_nevr = rpmProblemGetPkgNEVR (prob);
	pkg_nevr_alt = rpmProblemGetAltNEVR (prob);
	generic_str = rpmProblemGetStr (prob);

	switch (type) {
	case RPMPROB_BADARCH:
		str = g_strdup_printf ("package %s is for a different architecture",
				       pkg_nevr);
		break;
	case RPMPROB_BADOS:
		str = g_strdup_printf ("package %s is for a different operating system",
				       pkg_nevr);
		break;
	case RPMPROB_PKG_INSTALLED:
		str = g_strdup_printf ("package %s is already installed",
				       pkg_nevr);
		break;
	case RPMPROB_BADRELOCATE:
		str = g_strdup_printf ("path %s is not relocatable for package %s",
				       generic_str,
				       pkg_nevr);
		break;
	case RPMPROB_REQUIRES:
		str = g_strdup_printf ("package %s has unsatisfied Requires: %s",
				       pkg_nevr,
				       generic_str);
		break;
	case RPMPROB_CONFLICT:
		str = g_strdup_printf ("package %s has unsatisfied Conflicts: %s",
				       pkg_nevr,
				       generic_str);
		break;
	case RPMPROB_NEW_FILE_CONFLICT:
		str = g_strdup_printf ("file %s conflicts between attemped installs of %s",
				       generic_str,
				       pkg_nevr);
		break;
	case RPMPROB_FILE_CONFLICT:
		str = g_strdup_printf ("file %s from install of %s conflicts with file from %s",
				       generic_str,
				       pkg_nevr,
				       pkg_nevr_alt);
		break;
	case RPMPROB_OLDPACKAGE:
		str = g_strdup_printf ("package %s (newer than %s) is already installed",
				       pkg_nevr,
				       pkg_nevr_alt);
		break;
	case RPMPROB_DISKSPACE:
	case RPMPROB_DISKNODES:
		diskspace = rpmProblemGetDiskNeed (prob);
		str = g_strdup_printf ("installing package %s needs %" G_GOFFSET_FORMAT
				       " on the %s filesystem",
				       pkg_nevr,
				       diskspace,
				       generic_str);
		break;
	case RPMPROB_OBSOLETES:
		str = g_strdup_printf ("package %s is obsoleted by %s",
				       pkg_nevr,
				       pkg_nevr_alt);
		break;
	}
	return str;
}

/**
 * hif_rpmts_look_for_problems:
 **/
gboolean
hif_rpmts_look_for_problems (rpmts ts, GError **error)
{
	gboolean ret = TRUE;
	GString *string = NULL;
	rpmProblem prob;
	rpmpsi psi;
	rpmps probs = NULL;
	gchar *msg;

	/* get a list of problems */
	probs = rpmtsProblems (ts);
	if (rpmpsNumProblems (probs) == 0)
		goto out;

	/* parse problems */
	string = g_string_new ("");
	psi = rpmpsInitIterator (probs);
	while (rpmpsNextIterator (psi) >= 0) {
		prob = rpmpsGetProblem (psi);
		msg = hif_rpmts_get_problem_str (prob);
		g_string_append (string, msg);
		g_string_append (string, "\n");
		g_free (msg);
	}
	rpmpsFreeIterator (psi);

	/* set error */
	ret = FALSE;

	/* we failed, and got a reason to report */
	if (string->len > 0) {
		g_string_set_size (string, string->len - 1);
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "Error running transaction: %s",
			     string->str);
		goto out;
	}

	/* we failed, and got no reason why */
	g_set_error_literal (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "Error running transaction and no problems were reported!");
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	rpmpsFree (probs);
	return ret;
}

/**
 * hif_rpmts_find_package:
 **/
static Header
hif_rpmts_find_package (rpmts ts, HyPackage pkg, GError **error)
{
	Header hdr = NULL;
	rpmdbMatchIterator iter;
	unsigned int recOffset;

	/* find package by db-id */
	recOffset = hy_package_get_rpmdbid (pkg);
	iter = rpmtsInitIterator(ts, RPMDBI_PACKAGES,
				 &recOffset, sizeof(recOffset));
	if (iter == NULL) {
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "failed to setup rpmts iter");
		goto out;
	}
	hdr = rpmdbNextIterator (iter);
	if (hdr == NULL) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_FILE_NOT_FOUND,
			     "failed to find package %s",
			     hy_package_get_name (pkg));
		goto out;
	}

	/* success */
	headerLink (hdr);
out:
	if (iter != NULL)
		rpmdbFreeIterator (iter);
	return hdr;
}

/**
 * hif_rpmts_add_remove_pkg:
 **/
gboolean
hif_rpmts_add_remove_pkg (rpmts ts, HyPackage pkg, GError **error)
{
	gboolean ret = TRUE;
	gint retval;
	Header hdr;

	hdr = hif_rpmts_find_package (ts, pkg, error);
	if (hdr == NULL) {
		ret = FALSE;
		goto out;
	}

	/* remove it */
	retval = rpmtsAddEraseElement (ts, hdr, -1);
	if (retval != 0) {
		ret = FALSE;
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "could not add erase element %s (%i)",
			     hy_package_get_name (pkg), retval);
		goto out;
	}
out:
	if (hdr != NULL)
		headerFree (hdr);
	return ret;
}
