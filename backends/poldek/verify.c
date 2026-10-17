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
 * Signature checks for only-trusted, with librpm against the keyring of the
 * root being managed. poldek's own check uses the host's keyring, and checks
 * signatures only when it installs, not for downloads.
 */

#include <rpm/header.h>
#include <rpm/rpmcli.h>
#include <rpm/rpmio.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmts.h>

#include <pk-enum.h>

#include "job.h"
#include "verify.h"

/*
 * The verdict: rpm -K with signatures and digests required, so a package
 * without a signature fails too. rpmReadPackageFile() is no verdict, it
 * accepts an unsigned package.
 */
static int
pk_poldek_verify_signatures (rpmts ts, const gchar *path)
{
	struct rpmQVKArguments_s qva = { 0 };
	FD_t fd = Fopen (path, "r.ufdio");
	int rc;

	if (fd == NULL || Ferror (fd)) {
		if (fd != NULL)
			Fclose (fd);
		return -1;
	}
	qva.qva_mode = 'K';
	rc = rpmVerifySignatures (&qva, ts, fd, path);
	Fclose (fd);
	return rc;
}

static Header
pk_poldek_verify_read_header (rpmts ts, const gchar *path, rpmVSFlags flags, rpmRC *rc)
{
	FD_t fd = Fopen (path, "r.ufdio");
	Header h = NULL;

	*rc = RPMRC_FAIL;
	if (fd == NULL || Ferror (fd)) {
		if (fd != NULL)
			Fclose (fd);
		return NULL;
	}
	rpmtsSetVSFlags (ts, flags);
	*rc = rpmReadPackageFile (ts, fd, path, &h);
	Fclose (fd);
	return h;
}

/* Only says which error a package that failed is; it never accepts one. */
static PkErrorEnum
pk_poldek_verify_classify (rpmts ts, const gchar *path)
{
	Header h;
	gboolean signed_pkg;
	rpmRC rc;

	/* not even a package without any checks */
	h = pk_poldek_verify_read_header (ts,
					  path,
					  RPMVSF_MASK_NODIGESTS | RPMVSF_MASK_NOSIGNATURES,
					  &rc);
	if (h == NULL)
		return PK_ERROR_ENUM_INVALID_PACKAGE_FILE;
	signed_pkg = headerIsEntry (h, RPMTAG_OPENPGP) || headerIsEntry (h, RPMTAG_RSAHEADER) ||
		     headerIsEntry (h, RPMTAG_DSAHEADER) || headerIsEntry (h, RPMTAG_SIGGPG) ||
		     headerIsEntry (h, RPMTAG_SIGPGP);
	headerFree (h);
	if (!signed_pkg)
		return PK_ERROR_ENUM_MISSING_GPG_SIGNATURE;
	h = pk_poldek_verify_read_header (ts, path, 0, &rc);
	headerFree (h);
	if (rc == RPMRC_NOKEY || rc == RPMRC_NOTTRUSTED)
		return PK_ERROR_ENUM_GPG_FAILURE;
	return PK_ERROR_ENUM_BAD_GPG_SIGNATURE;
}

gboolean
pk_poldek_verify_package (const gchar *root, const gchar *path, GError **error)
{
	rpmts ts = rpmtsCreate ();
	gboolean ok;

	rpmtsSetRootDir (ts, root);
	rpmtsSetVfyFlags (ts, 0);
	rpmtsSetVfyLevel (ts, RPMSIG_SIGNATURE_TYPE | RPMSIG_DIGEST_TYPE);
	ok = pk_poldek_verify_signatures (ts, path) == 0;
	if (!ok) {
		PkErrorEnum code = pk_poldek_verify_classify (ts, path);
		g_autofree gchar *name = g_path_get_basename (path);

		g_set_error (error,
			     PK_POLDEK_ERROR,
			     code,
			     "%s: %s",
			     name,
			     code == PK_ERROR_ENUM_MISSING_GPG_SIGNATURE ? "no signature"
			     : code == PK_ERROR_ENUM_GPG_FAILURE
				 ? "signed with a key that is not trusted"
			     : code == PK_ERROR_ENUM_BAD_GPG_SIGNATURE ? "bad signature or digest"
								       : "not a package");
	}
	rpmtsFree (ts);
	return ok;
}
