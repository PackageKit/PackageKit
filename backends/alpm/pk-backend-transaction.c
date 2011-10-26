/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008-2010 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010-2011 Jonathan Conder <jonno.conder@gmail.com>
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

#include "pk-backend-alpm.h"
#include "pk-backend-error.h"
#include "pk-backend-packages.h"
#include "pk-backend-transaction.h"

static off_t dcomplete = 0;
static off_t dtotal = 0;

static pmpkg_t *dpkg = NULL;
static GString *dfiles = NULL;

static pmpkg_t *tpkg = NULL;
static GString *toutput = NULL;

static gchar *
pk_backend_resolve_path (PkBackend *self, const gchar *basename)
{
	const gchar *dirname;

	g_return_val_if_fail (self != NULL, NULL);
	g_return_val_if_fail (basename != NULL, NULL);

	dirname = pk_backend_get_string (self, "directory");

	g_return_val_if_fail (dirname != NULL, NULL);

	return g_build_filename (dirname, basename, NULL);
}

static gboolean
alpm_pkg_has_basename (pmpkg_t *pkg, const gchar *basename)
{
	const alpm_list_t *i;

	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (basename != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);

	if (g_strcmp0 (alpm_pkg_get_filename (pkg), basename) == 0) {
		return TRUE;
	}

	if (alpm_option_get_usedelta (alpm) == 0) {
		return FALSE;
	}

	for (i = alpm_pkg_get_deltas (pkg); i != NULL; i = i->next) {
		const gchar *patch = alpm_delta_get_filename (i->data);

		if (g_strcmp0 (patch, basename) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

static void
pk_backend_transaction_download_end (PkBackend *self)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (dpkg != NULL);

	pk_backend_pkg (self, dpkg, PK_INFO_ENUM_FINISHED);

	/* tell DownloadPackages what files were downloaded */
	if (dfiles != NULL) {
		gchar *package_id;

		package_id = alpm_pkg_build_id (dpkg);

		pk_backend_files (self, package_id, dfiles->str);

		g_free (package_id);
		g_string_free (dfiles, TRUE);
	}

	dpkg = NULL;
	dfiles = NULL;
}

static void
pk_backend_transaction_download_start (PkBackend *self, const gchar *basename)
{
	gchar *path;
	const alpm_list_t *i;

	g_return_if_fail (self != NULL);
	g_return_if_fail (basename != NULL);
	g_return_if_fail (alpm != NULL);

	/* continue or finish downloading the current package */
	if (dpkg != NULL) {
		if (alpm_pkg_has_basename (dpkg, basename)) {
			if (dfiles != NULL) {
				path = pk_backend_resolve_path (self, basename);
				g_string_append_printf (dfiles, ";%s", path);
				g_free (path);
			}

			return;
		} else {
			pk_backend_transaction_download_end (self);
			dpkg = NULL;
		}
	}

	/* figure out what the next package is */
	for (i = alpm_trans_get_add (alpm); i != NULL; i = i->next) {
		pmpkg_t *pkg = (pmpkg_t *) i->data;

		if (alpm_pkg_has_basename (pkg, basename)) {
			dpkg = pkg;
			break;
		}
	}

	if (dpkg == NULL) {
		return;
	}

	pk_backend_pkg (self, dpkg, PK_INFO_ENUM_DOWNLOADING);

	/* start collecting files for the new package */
	if (pk_backend_get_role (self) == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		path = pk_backend_resolve_path (self, basename);
		dfiles = g_string_new (path);
		g_free (path);
	}
}

static void
pk_backend_transaction_totaldlcb (off_t total)
{
	g_return_if_fail (backend != NULL);

	if (dtotal > 0 && dpkg != NULL) {
		pk_backend_transaction_download_end (backend);
	}

	dcomplete = 0;
	dtotal = total;
}

static void
pk_backend_transaction_dlcb (const gchar *basename, off_t complete, off_t total)
{
	guint percentage = 100, sub_percentage = 100;

	g_return_if_fail (basename != NULL);
	g_return_if_fail (complete <= total);
	g_return_if_fail (backend != NULL);

	if (total > 0) {
		sub_percentage = complete * 100 / total;
	}

	if (dtotal > 0) {
		percentage = (dcomplete + complete) * 100 / dtotal;
	} else if (dtotal < 0) {
		/* database files */
		percentage = (dcomplete * 100 + sub_percentage) / -dtotal;

		if (complete == total) {
			complete = total = 1;
		} else {
			complete = total + 1;
		}
	}

	if (complete == 0) {
		g_debug ("downloading file %s", basename);
		pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
		pk_backend_transaction_download_start (backend, basename);
	} else if (complete == total) {
		dcomplete += complete;
	}

	pk_backend_set_sub_percentage (backend, sub_percentage);
	pk_backend_set_percentage (backend, percentage);
}

static void
pk_backend_transaction_progress_cb (pmtransprog_t type, const gchar *target,
				    gint percent, gsize targets, gsize current)
{
	static gint recent = 101;
	gsize overall = percent + (current - 1) * 100;

	/* TODO: revert when fixed upstream */
	if (type == ALPM_PROGRESS_CONFLICTS_START ||
	    type == ALPM_PROGRESS_DISKSPACE_START ||
	    type == ALPM_PROGRESS_INTEGRITY_START) {
		if (current < targets) {
			overall = percent + current++ * 100;
		}
	}
	
	if (current < 1 || targets < current) {
		g_warning ("TODO: CURRENT/TARGETS FAILED for %d", type);
	}

	g_return_if_fail (target != NULL);
	g_return_if_fail (0 <= percent && percent <= 100);
	g_return_if_fail (1 <= current && current <= targets);
	g_return_if_fail (backend != NULL);

	/* update transaction progress */
	switch (type) {
		case ALPM_PROGRESS_ADD_START:
		case ALPM_PROGRESS_UPGRADE_START:
		case ALPM_PROGRESS_REMOVE_START:
		case ALPM_PROGRESS_CONFLICTS_START:
		case ALPM_PROGRESS_DISKSPACE_START:
		case ALPM_PROGRESS_INTEGRITY_START:
			if (percent == recent) {
				break;
			}

			pk_backend_set_sub_percentage (backend, percent);
			pk_backend_set_percentage (backend, overall / targets);
			recent = percent;

			g_debug ("%d%% of %s complete (%zu of %zu)", percent,
				 target, current, targets);
			break;

		default:
			g_warning ("unknown progress type %d", type);
			break;
	}
}

static void
pk_backend_install_ignorepkg (PkBackend *self, pmpkg_t *pkg, gint *result)
{
	gchar *output;

	g_return_if_fail (self != NULL);
	g_return_if_fail (pkg != NULL);
	g_return_if_fail (result != NULL);

	switch (pk_backend_get_role (self)) {
		case PK_ROLE_ENUM_INSTALL_PACKAGES:
			output = g_strdup_printf ("%s: was not ignored\n",
						  alpm_pkg_get_name (pkg));
			pk_backend_output (self, output);
			g_free (output);

		case PK_ROLE_ENUM_DOWNLOAD_PACKAGES:
		case PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES:
			*result = 1;
			break;

		default:
			*result = 0;
			break;
	}
}

static void
pk_backend_select_provider (PkBackend *self, pmdepend_t *dep,
			    const alpm_list_t *providers)
{
	gchar *output;

	g_return_if_fail (self != NULL);
	g_return_if_fail (dep != NULL);
	g_return_if_fail (providers != NULL);

	output = g_strdup_printf ("provider package was selected "
				  "(%s provides %s)\n",
				  alpm_pkg_get_name (providers->data),
				  alpm_dep_get_name (dep));
	pk_backend_output (self, output);
	g_free (output);
}

static void
pk_backend_transaction_conv_cb (pmtransconv_t question, gpointer data1,
				gpointer data2, gpointer data3, gint *result)
{
	g_return_if_fail (result != NULL);
	g_return_if_fail (backend != NULL);

	switch (question) {
		case ALPM_QUESTION_INSTALL_IGNOREPKG:
			pk_backend_install_ignorepkg (backend, data1, result);
			break;

		case ALPM_QUESTION_REPLACE_PKG:
		case ALPM_QUESTION_CONFLICT_PKG:
		case ALPM_QUESTION_CORRUPTED_PKG:
		case ALPM_QUESTION_LOCAL_NEWER:
			/* these actions are mostly harmless */
			g_debug ("safe question %d", question);
			*result = 1;
			break;

		case ALPM_QUESTION_REMOVE_PKGS:
			g_debug ("unsafe question %d", question);
			*result = 0;
			break;

		case ALPM_QUESTION_SELECT_PROVIDER:
			pk_backend_select_provider (backend, data1, data2);
			*result = 0;
			break;

		default:
			g_warning ("unknown question %d", question);
			break;
	}
}

static void
pk_backend_output_end (PkBackend *self)
{
	g_return_if_fail (self != NULL);

	tpkg = NULL;

	if (toutput != NULL) {
		pk_backend_output (self, toutput->str);
		g_string_free (toutput, TRUE);
		toutput = NULL;
	}
}

static void
pk_backend_output_start (PkBackend *self, pmpkg_t *pkg)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (pkg != NULL);

	if (tpkg != NULL) {
		pk_backend_output_end (self);
	}

	tpkg = pkg;
}

void
pk_backend_output (PkBackend *self, const gchar *output)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (output != NULL);

	if (tpkg != NULL) {
		if (toutput == NULL) {
			toutput = g_string_new ("<b>");
			g_string_append (toutput, alpm_pkg_get_name (tpkg));
			g_string_append (toutput, "</b>\n");
		}

		g_string_append (toutput, output);
	} else {
		PkMessageEnum type = PK_MESSAGE_ENUM_UNKNOWN;
		pk_backend_message (self, type, "%s", output);
	}
}

static void
pk_backend_transaction_dep_resolve (PkBackend *self)
{
	g_return_if_fail (self != NULL);

	pk_backend_set_status (self, PK_STATUS_ENUM_DEP_RESOLVE);
}

static void
pk_backend_transaction_test_commit (PkBackend *self)
{
	g_return_if_fail (self != NULL);

	pk_backend_set_status (self, PK_STATUS_ENUM_TEST_COMMIT);
}

static void
pk_backend_transaction_add_start (PkBackend *self, pmpkg_t *pkg)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (pkg != NULL);

	pk_backend_set_status (self, PK_STATUS_ENUM_INSTALL);
	pk_backend_pkg (self, pkg, PK_INFO_ENUM_INSTALLING);
	pk_backend_output_start (self, pkg);
}

static void
pk_backend_transaction_add_done (PkBackend *self, pmpkg_t *pkg)
{
	const gchar *name, *version;
	const alpm_list_t *i, *optdepends;

	g_return_if_fail (self != NULL);
	g_return_if_fail (pkg != NULL);
	g_return_if_fail (alpm != NULL);

	name = alpm_pkg_get_name (pkg);
	version = alpm_pkg_get_version (pkg);

	alpm_logaction (alpm, "installed %s (%s)\n", name, version);
	pk_backend_pkg (self, pkg, PK_INFO_ENUM_FINISHED);

	optdepends = alpm_pkg_get_optdepends (pkg);
	if (optdepends != NULL) {
		pk_backend_output (self, "Optional dependencies:\n");

		for (i = optdepends; i != NULL; i = i->next) {
			const gchar *depend = i->data;
			gchar *output = g_strdup_printf ("%s\n", depend);
			pk_backend_output (self, output);
			g_free (output);
		}
	}
	pk_backend_output_end (self);
}

static void
pk_backend_transaction_remove_start (PkBackend *self, pmpkg_t *pkg)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (pkg != NULL);

	pk_backend_set_status (self, PK_STATUS_ENUM_REMOVE);
	pk_backend_pkg (self, pkg, PK_INFO_ENUM_REMOVING);
	pk_backend_output_start (self, pkg);
}

static void
pk_backend_transaction_remove_done (PkBackend *self, pmpkg_t *pkg)
{
	const gchar *name, *version;

	g_return_if_fail (self != NULL);
	g_return_if_fail (pkg != NULL);
	g_return_if_fail (alpm != NULL);

	name = alpm_pkg_get_name (pkg);
	version = alpm_pkg_get_version (pkg);

	alpm_logaction (alpm, "removed %s (%s)\n", name, version);
	pk_backend_pkg (self, pkg, PK_INFO_ENUM_FINISHED);
	pk_backend_output_end (self);
}

static void
pk_backend_transaction_upgrade_start (PkBackend *self, pmpkg_t *pkg,
				      pmpkg_t *old)
{
	PkRoleEnum role;
	PkStatusEnum state;
	PkInfoEnum info;

	g_return_if_fail (self != NULL);
	g_return_if_fail (pkg != NULL);

	role = pk_backend_get_role (self);
	if (role == PK_ROLE_ENUM_INSTALL_FILES ||
	    role == PK_ROLE_ENUM_SIMULATE_INSTALL_FILES) {
		state = PK_STATUS_ENUM_INSTALL;
		info = PK_INFO_ENUM_INSTALLING;
	} else {
		state = PK_STATUS_ENUM_UPDATE;
		info = PK_INFO_ENUM_UPDATING;
	}

	pk_backend_set_status (self, state);
	pk_backend_pkg (self, pkg, info);
	pk_backend_output_start (self, pkg);
}

static void
pk_backend_transaction_upgrade_done (PkBackend *self, pmpkg_t *pkg,
				     pmpkg_t *old)
{
	const gchar *name, *pre, *post;
	const alpm_list_t *i;
	alpm_list_t *optdepends;

	g_return_if_fail (self != NULL);
	g_return_if_fail (pkg != NULL);
	g_return_if_fail (old != NULL);
	g_return_if_fail (alpm != NULL);

	name = alpm_pkg_get_name (pkg);
	pre = alpm_pkg_get_version (old);
	post = alpm_pkg_get_version (pkg);

	alpm_logaction (alpm, "upgraded %s (%s -> %s)\n", name, pre, post);
	pk_backend_pkg (self, pkg, PK_INFO_ENUM_FINISHED);

	optdepends = alpm_list_diff (alpm_pkg_get_optdepends (pkg),
				     alpm_pkg_get_optdepends (old),
				     (alpm_list_fn_cmp) g_strcmp0);
	if (optdepends != NULL) {
		pk_backend_output (self, "New optional dependencies:\n");

		for (i = optdepends; i != NULL; i = i->next) {
			const gchar *depend = i->data;
			gchar *output = g_strdup_printf ("%s\n", depend);
			pk_backend_output (self, output);
			g_free (output);
		}

		alpm_list_free (optdepends);
	}
	pk_backend_output_end (self);
}

static void
pk_backend_transaction_event_cb (pmtransevt_t event, gpointer data,
				 gpointer old)
{
	g_return_if_fail (backend != NULL);

	/* figure out the backend status and package info */
	switch (event) {
		case ALPM_EVENT_CHECKDEPS_START:
		case ALPM_EVENT_RESOLVEDEPS_START:
			pk_backend_transaction_dep_resolve (backend);
			break;

		case ALPM_EVENT_FILECONFLICTS_START:
		case ALPM_EVENT_INTERCONFLICTS_START:
		case ALPM_EVENT_INTEGRITY_START:
		case ALPM_EVENT_DELTA_INTEGRITY_START:
		case ALPM_EVENT_DISKSPACE_START:
			pk_backend_transaction_test_commit (backend);
			break;

		case ALPM_EVENT_ADD_START:
			pk_backend_transaction_add_start (backend, data);
			break;

		case ALPM_EVENT_ADD_DONE:
			pk_backend_transaction_add_done (backend, data);
			break;

		case ALPM_EVENT_REMOVE_START:
			pk_backend_transaction_remove_start (backend, data);
			break;

		case ALPM_EVENT_REMOVE_DONE:
			pk_backend_transaction_remove_done (backend, data);
			break;

		case ALPM_EVENT_UPGRADE_START:
			pk_backend_transaction_upgrade_start (backend, data,
							      old);
			break;

		case ALPM_EVENT_UPGRADE_DONE:
			pk_backend_transaction_upgrade_done (backend, data,
							     old);
			break;

		case ALPM_EVENT_SCRIPTLET_INFO:
			pk_backend_output (backend, data);
			break;

		default:
			g_debug ("unhandled event %d", event);
			break;
	}
}

static void
transaction_cancelled_cb (GCancellable *object, gpointer data)
{
	g_return_if_fail (data != NULL);
	g_return_if_fail (alpm != NULL);

	alpm_trans_interrupt (alpm);
}

gboolean
pk_backend_transaction_initialize (PkBackend *self, pmtransflag_t flags,
				   GError **error)
{
	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);
	g_return_val_if_fail (cancellable != NULL, FALSE);

	if (alpm_trans_init (alpm, flags) < 0) {
		enum _alpm_errno_t errno = alpm_errno (alpm);
		g_set_error_literal (error, ALPM_ERROR, errno,
				     alpm_strerror (errno));
		return FALSE;
	}

	alpm_option_set_eventcb (alpm, pk_backend_transaction_event_cb);
	alpm_option_set_questioncb (alpm, pk_backend_transaction_conv_cb);
	alpm_option_set_progresscb (alpm, pk_backend_transaction_progress_cb);

	alpm_option_set_dlcb (alpm, pk_backend_transaction_dlcb);
	alpm_option_set_totaldlcb (alpm, pk_backend_transaction_totaldlcb);

	g_cancellable_connect (cancellable,
			       G_CALLBACK (transaction_cancelled_cb),
			       self, NULL);

	return TRUE;
}

static gchar *
alpm_pkg_build_list (const alpm_list_t *i)
{
	GString *list;

	if (i == NULL) {
		return NULL;
	} else {
		list = g_string_new ("");
	}

	for (; i != NULL; i = i->next) {
		g_string_append_printf (list, "%s, ",
					alpm_pkg_get_name (i->data));
	}

	g_string_truncate (list, list->len - 2);
	return g_string_free (list, FALSE);
}

static gchar *
alpm_miss_build_list (const alpm_list_t *i)
{
	GString *list;

	if (i == NULL) {
		return NULL;
	} else {
		list = g_string_new ("");
	}

	for (; i != NULL; i = i->next) {
		pmdepend_t *dep = alpm_miss_get_dep (i->data);
		gchar *depend = alpm_dep_compute_string (dep);
		g_string_append_printf (list, "%s <- %s, ", depend,
					alpm_miss_get_target (i->data));
		free (depend);
	}

	g_string_truncate (list, list->len - 2);
	return g_string_free (list, FALSE);
}

static void
alpm_dep_free (gpointer dep)
{
	/* TODO: remove when implemented in libalpm */
	free ((gpointer) alpm_dep_get_name (dep));
	free ((gpointer) alpm_dep_get_version (dep));
	free (dep);
}

static void
alpm_miss_free (gpointer miss)
{
	/* TODO: remove when implemented in libalpm */
	const gchar *temp = alpm_miss_get_causingpkg (miss);
	if (temp != NULL) {
		free ((gpointer) temp);
	}

	free ((gpointer) alpm_miss_get_target (miss));
	alpm_dep_free (alpm_miss_get_dep (miss));
	free (miss);
}

static gchar *
alpm_conflict_build_list (const alpm_list_t *i)
{
	GString *list;

	if (i == NULL) {
		return NULL;
	} else {
		list = g_string_new ("");
	}

	for (; i != NULL; i = i->next) {
		const gchar *first = alpm_conflict_get_package1 (i->data);
		const gchar *second = alpm_conflict_get_package2 (i->data);
		const gchar *reason = alpm_conflict_get_reason (i->data);

		if (g_strcmp0 (first, reason) == 0 ||
		    g_strcmp0 (second, reason) == 0) {
			g_string_append_printf (list, "%s <-> %s, ", first,
						second);
		} else {
			g_string_append_printf (list, "%s <-> %s (%s), ", first,
						second, reason);
		}
	}

	g_string_truncate (list, list->len - 2);
	return g_string_free (list, FALSE);
}

static void
alpm_conflict_free (gpointer conflict)
{
	/* TODO: remove when implemented in libalpm */
	free ((gpointer) alpm_conflict_get_package1 (conflict));
	free ((gpointer) alpm_conflict_get_package2 (conflict));
	free ((gpointer) alpm_conflict_get_reason (conflict));
	free (conflict);
}

static gchar *
alpm_fileconflict_build_list (const alpm_list_t *i)
{
	GString *list;

	if (i == NULL) {
		return NULL;
	} else {
		list = g_string_new ("");
	}

	for (; i != NULL; i = i->next) {
		const gchar *target = alpm_fileconflict_get_target (i->data);
		const gchar *file = alpm_fileconflict_get_file (i->data);
		const gchar *ctarget = alpm_fileconflict_get_ctarget (i->data);
		if (*ctarget != '\0') {
			g_string_append_printf (list, "%s <-> %s (%s), ",
						target, ctarget, file);
		} else {
			g_string_append_printf (list, "%s (%s), ", target,
						file);
		}
	}

	g_string_truncate (list, list->len - 2);
	return g_string_free (list, FALSE);
}

static void
alpm_fileconflict_free (gpointer conflict)
{
	/* TODO: remove when implemented in libalpm */
	const gchar *temp = alpm_fileconflict_get_ctarget (conflict);
	if (*temp != '\0') {
		free ((gpointer) temp);
	}

	free ((gpointer) alpm_fileconflict_get_target (conflict));
	free ((gpointer) alpm_fileconflict_get_file (conflict));
	free (conflict);
}

gboolean
pk_backend_transaction_simulate (PkBackend *self, GError **error)
{
	alpm_list_t *data = NULL;
	gchar *prefix;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);

	if (alpm_trans_prepare (alpm, &data) >= 0) {
		return TRUE;
	}

	switch (alpm_errno (alpm)) {
		case ALPM_ERR_PKG_INVALID_ARCH:
			prefix = alpm_pkg_build_list (data);
			alpm_list_free (data);
			break;

		case ALPM_ERR_UNSATISFIED_DEPS:
			prefix = alpm_miss_build_list (data);
			alpm_list_free_inner (data, alpm_miss_free);
			alpm_list_free (data);
			break;

		case ALPM_ERR_CONFLICTING_DEPS:
			prefix = alpm_conflict_build_list (data);
			alpm_list_free_inner (data, alpm_conflict_free);
			alpm_list_free (data);
			break;

		case ALPM_ERR_FILE_CONFLICTS:
			prefix = alpm_fileconflict_build_list (data);
			alpm_list_free_inner (data, alpm_fileconflict_free);
			alpm_list_free (data);
			break;

		default:
			prefix = NULL;
			if (data != NULL) {
				g_warning ("unhandled error %d",
					   alpm_errno (alpm));
			}
			break;
	}

	if (prefix != NULL) {
		enum _alpm_errno_t errno = alpm_errno (alpm);
		g_set_error (error, ALPM_ERROR, errno, "%s: %s", prefix,
			     alpm_strerror (errno));
		g_free (prefix);
	} else {
		enum _alpm_errno_t errno = alpm_errno (alpm);
		g_set_error_literal (error, ALPM_ERROR, errno,
				     alpm_strerror (errno));
	}

	return FALSE;
}

void
pk_backend_transaction_packages (PkBackend *self)
{
	const alpm_list_t *i;
	PkInfoEnum info;

	g_return_if_fail (self != NULL);
	g_return_if_fail (alpm != NULL);
	g_return_if_fail (localdb != NULL);

	/* emit packages that would have been installed */
	for (i = alpm_trans_get_add (alpm); i != NULL; i = i->next) {
		if (pk_backend_cancelled (self)) {
			break;
		} else {
			const gchar *name = alpm_pkg_get_name (i->data);

			if (alpm_db_get_pkg (localdb, name) != NULL) {
				info = PK_INFO_ENUM_UPDATING;
			} else {
				info = PK_INFO_ENUM_INSTALLING;
			}

			pk_backend_pkg (self, i->data, info);
		}
	}

	switch (pk_backend_get_role (self)) {
		case PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES:
			info = PK_INFO_ENUM_OBSOLETING;
			break;

		default:
			info = PK_INFO_ENUM_REMOVING;
			break;
	}

	/* emit packages that would have been removed */
	for (i = alpm_trans_get_remove (alpm); i != NULL; i = i->next) {
		if (pk_backend_cancelled (self)) {
			break;
		} else {
			pk_backend_pkg (self, i->data, info);
		}
	}
}

static gchar *
alpm_string_build_list (const alpm_list_t *i)
{
	GString *list;

	if (i == NULL) {
		return NULL;
	} else {
		list = g_string_new ("");
	}

	for (; i != NULL; i = i->next) {
		g_string_append_printf (list, "%s, ", (const gchar *) i->data);
	}

	g_string_truncate (list, list->len - 2);
	return g_string_free (list, FALSE);
}

gboolean
pk_backend_transaction_commit (PkBackend *self, GError **error)
{
	alpm_list_t *data = NULL;
	gchar *prefix;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);

	if (pk_backend_cancelled (self)) {
		return TRUE;
	}

	pk_backend_set_allow_cancel (self, FALSE);
	pk_backend_set_status (self, PK_STATUS_ENUM_RUNNING);

	if (alpm_trans_commit (alpm, &data) >= 0) {
		return TRUE;
	}

	switch (alpm_errno (alpm)) {
		case ALPM_ERR_FILE_CONFLICTS:
			prefix = alpm_fileconflict_build_list (data);
			alpm_list_free_inner (data, alpm_fileconflict_free);
			alpm_list_free (data);
			break;

		case ALPM_ERR_PKG_INVALID:
		case ALPM_ERR_DLT_INVALID:
			prefix = alpm_string_build_list (data);
			alpm_list_free (data);
			break;

		default:
			prefix = NULL;
			if (data != NULL) {
				g_warning ("unhandled error %d",
					   alpm_errno (alpm));
			}
			break;
	}

	if (prefix != NULL) {
		enum _alpm_errno_t errno = alpm_errno (alpm);
		g_set_error (error, ALPM_ERROR, errno, "%s: %s", prefix,
			     alpm_strerror (errno));
		g_free (prefix);
	} else {
		enum _alpm_errno_t errno = alpm_errno (alpm);
		g_set_error_literal (error, ALPM_ERROR, errno,
				     alpm_strerror (errno));
	}

	return FALSE;
}

gboolean
pk_backend_transaction_end (PkBackend *self, GError **error)
{
	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);

	alpm_option_set_eventcb (alpm, NULL);
	alpm_option_set_questioncb (alpm, NULL);
	alpm_option_set_progresscb (alpm, NULL);

	alpm_option_set_dlcb (alpm, NULL);
	alpm_option_set_totaldlcb (alpm, NULL);

	if (dpkg != NULL) {
		pk_backend_transaction_download_end (self);
	}
	if (tpkg != NULL) {
		pk_backend_output_end (self);
	}

	if (alpm_trans_release (alpm) < 0) {
		enum _alpm_errno_t errno = alpm_errno (alpm);
		g_set_error_literal (error, ALPM_ERROR, errno,
				     alpm_strerror (errno));
		return FALSE;
	}

	return TRUE;
}

gboolean
pk_backend_transaction_finish (PkBackend *self, GError *error)
{
	g_return_val_if_fail (self != NULL, FALSE);

	pk_backend_transaction_end (self, (error == NULL) ? &error : NULL);

	return pk_backend_finish (self, error);
}
