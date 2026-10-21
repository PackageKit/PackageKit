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
 * Package files a client has: their details and file lists come from the
 * header with librpm. install-files installs them with a context of their
 * own, which has one more source, a directory with links to the files, so
 * poldek resolves their dependencies from the repositories as for any other.
 */

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <pk-enum.h>
#include <pk-package-id.h>

#include <rpm/header.h>
#include <rpm/rpmfi.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmts.h>

#include <poldek/poldek.h>

#include "groups.h"
#include "local.h"
#include "pkgid.h"
#include "protocol.h"
#include "query.h"
#include "transaction.h"

/* The header of the package file @path, or NULL with an error. Signatures are for install to check. */
static Header
pk_poldek_local_read (const gchar *path, GError **error)
{
	rpmts ts = rpmtsCreate ();
	FD_t fd = Fopen (path, "r.ufdio");
	Header h = NULL;

	rpmtsSetVSFlags (ts, RPMVSF_MASK_NODIGESTS | RPMVSF_MASK_NOSIGNATURES);
	if (fd != NULL && !Ferror (fd) && rpmReadPackageFile (ts, fd, path, &h) != RPMRC_OK)
		h = headerFree (h);
	if (fd != NULL)
		Fclose (fd);
	rpmtsFree (ts);
	if (h == NULL)
		g_set_error (error,
			     PK_POLDEK_ERROR,
			     PK_ERROR_ENUM_INVALID_PACKAGE_FILE,
			     "%s is not a package",
			     path);
	return h;
}

static gchar *
pk_poldek_local_id (Header h)
{
	g_autofree gchar *evr = NULL;
	g_autofree gchar *id = NULL;
	guint64 epoch = headerGetNumber (h, RPMTAG_EPOCH);

	if (headerIsEntry (h, RPMTAG_EPOCH) && epoch > 0)
		evr = g_strdup_printf ("%" G_GUINT64_FORMAT ":%s-%s",
				       epoch,
				       headerGetString (h, RPMTAG_VERSION),
				       headerGetString (h, RPMTAG_RELEASE));
	else
		evr = g_strdup_printf ("%s-%s",
				       headerGetString (h, RPMTAG_VERSION),
				       headerGetString (h, RPMTAG_RELEASE));
	id = pk_package_id_build (headerGetString (h, RPMTAG_NAME),
				  evr,
				  headerGetString (h, RPMTAG_ARCH),
				  PK_POLDEK_ORIGIN_LOCAL,
				  "");
	/* package headers have no encoding, ids are UTF-8 */
	return g_utf8_make_valid (id, -1);
}

void
pk_poldek_get_details_local (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_auto(GStrv) files = pk_poldek_require_strv (job, req, "files");

	for (guint i = 0; files != NULL && files[i] != NULL; i++) {
		GError *error = NULL;
		Header h = pk_poldek_local_read (files[i], &error);
		g_autofree gchar *id = NULL;
		json_t *item;

		if (h == NULL) {
			pk_poldek_job_take_error (job, error);
			return;
		}
		id = pk_poldek_local_id (h);
		item = json_pack ("{s:s, s:s, s:I}",
				  "package_id",
				  id,
				  "group",
				  pk_group_enum_to_string (
				      pk_poldek_group_from_rpm (headerGetString (h, RPMTAG_GROUP))),
				  "size",
				  (json_int_t) headerGetNumber (h, RPMTAG_LONGSIZE));
		pk_poldek_json_set_text (item, "summary", headerGetString (h, RPMTAG_SUMMARY));
		pk_poldek_json_set_text (item,
					 "description",
					 headerGetString (h, RPMTAG_DESCRIPTION));
		pk_poldek_json_set_text (item, "license", headerGetString (h, RPMTAG_LICENSE));
		pk_poldek_json_set_text (item, "url", headerGetString (h, RPMTAG_URL));
		pk_poldek_job_queue (job, "details", item);
		headerFree (h);
	}
}

void
pk_poldek_get_files_local (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_auto(GStrv) files = pk_poldek_require_strv (job, req, "files");

	for (guint i = 0; files != NULL && files[i] != NULL; i++) {
		GError *error = NULL;
		Header h = pk_poldek_local_read (files[i], &error);
		g_autofree gchar *id = NULL;
		json_t *paths;
		rpmfi fi;

		if (h == NULL) {
			pk_poldek_job_take_error (job, error);
			return;
		}
		id = pk_poldek_local_id (h);
		paths = json_object ();
		fi = rpmfiNew (NULL, h, RPMTAG_BASENAMES, RPMFI_NOHEADER);
		while (rpmfiNext (fi) >= 0)
			pk_poldek_files_add (paths, rpmfiFN (fi), rpmfiFMode (fi));
		rpmfiFree (fi);
		headerFree (h);
		pk_poldek_job_emit (job,
				    "files",
				    json_pack ("{s:s, s:o}", "package_id", id, "files", paths));
	}
}
