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
#include "pk-alpm-error.h"
#include "pk-alpm-packages.h"
#include "pk-alpm-transaction.h"

static off_t dcomplete = 0;
static off_t dtotal = 0;

static alpm_pkg_t *dpkg = NULL;
static GString *dfiles = NULL;

static alpm_pkg_t *tpkg = NULL;
static GString *toutput = NULL;

static PkBackendJob* pkalpm_current_job = NULL;
const gchar *pkalpm_dirname = NULL;

static gchar *
pk_alpm_resolve_path (PkBackendJob *job, const gchar *basename)
{
	g_return_val_if_fail (job != NULL, NULL);
	g_return_val_if_fail (basename != NULL, NULL);
	g_return_val_if_fail (pkalpm_dirname != NULL, NULL);

	return g_build_filename (pkalpm_dirname, basename, NULL);
}

static gboolean
pk_alpm_pkg_has_basename (PkBackend *backend, alpm_pkg_t *pkg, const gchar *basename)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	const alpm_list_t *i;

	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (basename != NULL, FALSE);

	if (g_strcmp0 (alpm_pkg_get_filename (pkg), basename) == 0)
		return TRUE;

	if (alpm_option_get_deltaratio (priv->alpm) == 0.0)
		return FALSE;

	for (i = alpm_pkg_get_deltas (pkg); i != NULL; i = i->next) {
		alpm_delta_t *delta = (alpm_delta_t *) i->data;
		if (g_strcmp0 (delta->delta, basename) == 0)
			return TRUE;
	}

	return FALSE;
}

static void
pk_alpm_transaction_download_end (PkBackendJob *job)
{
	g_return_if_fail (dpkg != NULL);

	pk_alpm_pkg_emit (job, dpkg, PK_INFO_ENUM_FINISHED);

	/* tell DownloadPackages what files were downloaded */
	if (dfiles != NULL) {
		g_autofree gchar *package_id = pk_alpm_pkg_build_id (dpkg);
		pk_backend_job_files (job, package_id, &dfiles->str);
		g_string_free (dfiles, TRUE);
	}

	dpkg = NULL;
	dfiles = NULL;
}

static void
pk_alpm_transaction_download_start (PkBackendJob *job, const gchar *basename)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	const alpm_list_t *i;

	g_return_if_fail (basename != NULL);

	/* continue or finish downloading the current package */
	if (dpkg != NULL) {
		if (pk_alpm_pkg_has_basename (backend, dpkg, basename)) {
			if (dfiles != NULL) {
				g_autofree gchar *path = NULL;
				path = pk_alpm_resolve_path (job, basename);
				g_string_append_printf (dfiles, ";%s", path);
			}
			return;
		}
		pk_alpm_transaction_download_end (job);
		dpkg = NULL;
	}

	/* figure out what the next package is */
	for (i = alpm_trans_get_add (priv->alpm); i != NULL; i = i->next) {
		alpm_pkg_t *pkg = (alpm_pkg_t *) i->data;

		if (pk_alpm_pkg_has_basename (backend, pkg, basename)) {
			dpkg = pkg;
			break;
		}
	}

	if (dpkg == NULL)
		return;

	pk_alpm_pkg_emit (job, dpkg, PK_INFO_ENUM_DOWNLOADING);

	/* start collecting files for the new package */
	if (pk_backend_job_get_role (job) == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		g_autofree gchar *path = NULL;
		path = pk_alpm_resolve_path (job, basename);
		dfiles = g_string_new (path);
	}
}

static void
pk_alpm_transaction_totaldlcb (off_t total)
{
	PkBackendJob* job;
	g_assert (pkalpm_current_job);
	job = pkalpm_current_job;

	if (dtotal > 0 && dpkg != NULL)
		pk_alpm_transaction_download_end (job);

	dcomplete = 0;
	dtotal = total;
}

static void
pk_alpm_transaction_dlcb (const gchar *basename, off_t complete, off_t total)
{
	guint percentage = 100, sub_percentage = 100;

	PkBackendJob* job;
	g_assert (pkalpm_current_job);
	job = pkalpm_current_job;

	g_return_if_fail (basename != NULL);
	g_return_if_fail (complete <= total);

	if (total > 0)
		sub_percentage = complete * 100 / total;

	if (dtotal > 0) {
		percentage = (dcomplete + complete) * 100 / dtotal;
	} else if (dtotal < 0) {
		/* database files */
		percentage = (dcomplete * 100 + sub_percentage) / -dtotal;

		if (complete == total)
			complete = total = 1;
		else
			complete = total + 1;
	}

	if (complete == 0) {
		g_debug ("downloading file %s", basename);
		pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
		pk_alpm_transaction_download_start (job, basename);
	} else if (complete == total) {
		dcomplete += complete;
	}
	pk_backend_job_set_percentage (job, percentage);
}

static void
pk_alpm_transaction_progress_cb (alpm_progress_t type, const gchar *target,
					gint percent, gsize targets, gsize current)
{
	static gint recent = 101;
	gsize overall = percent + (current - 1) * 100;

	PkBackendJob* job;
	g_assert (pkalpm_current_job);
	job = pkalpm_current_job;

	/* TODO: remove block if/when this is made consistent upstream */
	if (type == ALPM_PROGRESS_CONFLICTS_START ||
	    type == ALPM_PROGRESS_DISKSPACE_START ||
	    type == ALPM_PROGRESS_INTEGRITY_START ||
	    type == ALPM_PROGRESS_LOAD_START ||
	    type == ALPM_PROGRESS_KEYRING_START) {
		if (current < targets) {
			++current;
			overall += 100;
		}
	}

	if (current < 1 || targets < current)
		g_warning ("TODO: CURRENT/TARGETS FAILED for %d", type);

	g_return_if_fail (target != NULL);
	g_return_if_fail (0 <= percent && percent <= 100);
	g_return_if_fail (1 <= current && current <= targets);

	/* update transaction progress */
	switch (type) {
	case ALPM_PROGRESS_ADD_START:
	case ALPM_PROGRESS_UPGRADE_START:
	case ALPM_PROGRESS_DOWNGRADE_START:
	case ALPM_PROGRESS_REINSTALL_START:
	case ALPM_PROGRESS_REMOVE_START:
	case ALPM_PROGRESS_CONFLICTS_START:
	case ALPM_PROGRESS_DISKSPACE_START:
	case ALPM_PROGRESS_INTEGRITY_START:
	case ALPM_PROGRESS_LOAD_START:
	case ALPM_PROGRESS_KEYRING_START:
		if (percent == recent)
			break;

		pk_backend_job_set_item_progress (job, target, PK_ROLE_ENUM_UNKNOWN, percent);
		pk_backend_job_set_percentage (job, overall / targets);
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
pk_alpm_install_ignorepkg (PkBackendJob *job, alpm_question_install_ignorepkg_t *q)
{
	g_autofree gchar *output = NULL;

	g_return_if_fail (q != NULL);
	g_return_if_fail (q->pkg != NULL);

	switch (pk_backend_job_get_role (job)) {
	case PK_ROLE_ENUM_INSTALL_PACKAGES:
		output = g_strdup_printf ("%s: was not ignored\n",
					  alpm_pkg_get_name (q->pkg));
		pk_alpm_transaction_output (output);

	case PK_ROLE_ENUM_DOWNLOAD_PACKAGES:
		q->install = 1;
		break;

	default:
		q->install = 0;
		break;
	}
}

static void
pk_alpm_select_provider (const alpm_list_t *providers,
			    alpm_depend_t *depend)
{
	g_autofree gchar *output = NULL;

	g_return_if_fail (depend != NULL);
	g_return_if_fail (providers != NULL);

	output = g_strdup_printf ("provider package was selected "
				  "(%s provides %s)\n",
				  alpm_pkg_get_name (providers->data),
				  depend->name);
	pk_alpm_transaction_output (output);
}

static void
pk_alpm_transaction_conv_cb (alpm_question_t *question)
{
	PkBackendJob* job;
	g_assert (pkalpm_current_job);
	job = pkalpm_current_job;

	g_return_if_fail (question != NULL);

	switch (question->type) {
	case ALPM_QUESTION_INSTALL_IGNOREPKG:
		{
			alpm_question_install_ignorepkg_t *q = &question->install_ignorepkg;
			pk_alpm_install_ignorepkg (job, q);
		}
		break;
	case ALPM_QUESTION_REPLACE_PKG:
		{
			alpm_question_replace_t *q = &question->replace;
			g_debug ("safe question %d", question->type);
			q->replace = 1;
		}
		break;
	case ALPM_QUESTION_CONFLICT_PKG:
	case ALPM_QUESTION_CORRUPTED_PKG:
		{
			alpm_question_conflict_t *q = &question->conflict;
			g_debug ("safe question %d", question->type);
			q->remove = 1;
		}
		break;
//	case ALPM_QUESTION_LOCAL_NEWER:
	case ALPM_QUESTION_REMOVE_PKGS:
		{
			alpm_question_remove_pkgs_t *q = &question->remove_pkgs;
			g_debug ("unsafe question %d", question->type);
			q->skip = 0;
		}
		break;
	/* TODO: handle keys better */
	case ALPM_QUESTION_IMPORT_KEY:
		{
			alpm_question_import_key_t *q = &question->import_key;
			g_debug ("unsafe question %d", question->type);
			q->import = 0;
		}
		break;
	case ALPM_QUESTION_SELECT_PROVIDER:
		{
			alpm_question_select_provider_t *q = &question->select_provider;
			pk_alpm_select_provider (q->providers, q->depend);
			q->use_index = 0;
		}
		break;

	default:
		g_warning ("unknown question %d", question->type);
		break;
	}
}

static void
pk_alpm_transaction_output_end ()
{
	tpkg = NULL;

	if (toutput != NULL) {
		pk_alpm_transaction_output (toutput->str);
		g_string_free (toutput, TRUE);
		toutput = NULL;
	}
}

static void
pk_alpm_transaction_output_start (alpm_pkg_t *pkg)
{
	g_return_if_fail (pkg != NULL);

	if (tpkg != NULL)
		pk_alpm_transaction_output_end ();

	tpkg = pkg;
}

void
pk_alpm_transaction_output (const gchar *output)
{
	g_return_if_fail (output != NULL);

	if (tpkg != NULL) {
		if (toutput == NULL) {
			toutput = g_string_new ("<b>");
			g_string_append (toutput, alpm_pkg_get_name (tpkg));
			g_string_append (toutput, "</b>\n");
		}
		g_string_append (toutput, output);
	}
}

static void
pk_alpm_transaction_dep_resolve (PkBackendJob *job)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);
}

static void
pk_alpm_transaction_test_commit (PkBackendJob *job)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_TEST_COMMIT);
}

static void
pk_alpm_transaction_add_start (PkBackendJob *job, alpm_pkg_t *pkg)
{
	g_return_if_fail (pkg != NULL);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);
	pk_alpm_pkg_emit (job, pkg, PK_INFO_ENUM_INSTALLING);
	pk_alpm_transaction_output_start (pkg);
}

static void
pk_alpm_transaction_add_done (PkBackendJob *job, alpm_pkg_t *pkg)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	const gchar *name, *version;
	const alpm_list_t *i, *optdepends;

	g_return_if_fail (pkg != NULL);

	name = alpm_pkg_get_name (pkg);
	version = alpm_pkg_get_version (pkg);

	alpm_logaction (priv->alpm, PK_LOG_PREFIX, "installed %s (%s)\n", name,
			version);
	pk_alpm_pkg_emit (job, pkg, PK_INFO_ENUM_FINISHED);

	optdepends = alpm_pkg_get_optdepends (pkg);
	if (optdepends != NULL) {
		pk_alpm_transaction_output ("Optional dependencies:\n");

		for (i = optdepends; i != NULL; i = i->next) {
			char *depend = alpm_dep_compute_string (i->data);
			g_autofree gchar *output = g_strdup_printf ("%s\n", depend);
			free (depend);
			pk_alpm_transaction_output (output);
		}
	}
	pk_alpm_transaction_output_end ();
}

static void
pk_alpm_transaction_remove_start (PkBackendJob *job, alpm_pkg_t *pkg)
{
	g_return_if_fail (pkg != NULL);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);
	pk_alpm_pkg_emit (job, pkg, PK_INFO_ENUM_REMOVING);
	pk_alpm_transaction_output_start (pkg);
}

static void
pk_alpm_transaction_remove_done (PkBackendJob *job, alpm_pkg_t *pkg)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	const gchar *name, *version;

	g_return_if_fail (pkg != NULL);

	name = alpm_pkg_get_name (pkg);
	version = alpm_pkg_get_version (pkg);

	alpm_logaction (priv->alpm, PK_LOG_PREFIX, "removed %s (%s)\n", name, version);
	pk_alpm_pkg_emit (job, pkg, PK_INFO_ENUM_FINISHED);
	pk_alpm_transaction_output_end ();
}

static void
pk_alpm_transaction_upgrade_start (PkBackendJob *job, alpm_pkg_t *pkg,
				      alpm_pkg_t *old)
{
	PkRoleEnum role;
	PkStatusEnum state;
	PkInfoEnum info;

	g_return_if_fail (pkg != NULL);

	role = pk_backend_job_get_role (job);
	if (role == PK_ROLE_ENUM_INSTALL_FILES) {
		state = PK_STATUS_ENUM_INSTALL;
		info = PK_INFO_ENUM_INSTALLING;
	} else {
		state = PK_STATUS_ENUM_UPDATE;
		info = PK_INFO_ENUM_UPDATING;
	}

	pk_backend_job_set_status (job, state);
	pk_alpm_pkg_emit (job, pkg, info);
	pk_alpm_transaction_output_start (pkg);
}

static gint
pk_alpm_depend_compare (gconstpointer a, gconstpointer b)
{
	const alpm_depend_t *first = a;
	const alpm_depend_t *second = b;
	gint result;

	g_return_val_if_fail (first != NULL, 0);
	g_return_val_if_fail (second != NULL, 0);

	result = g_strcmp0 (first->name, second->name);
	if (result == 0) {
		result = first->mod - second->mod;
		if (result == 0) {
			result = g_strcmp0 (first->version, second->version);
			if (result == 0)
				result = g_strcmp0 (first->desc, second->desc);
		}
	}

	return result;
}

static void
pk_alpm_transaction_process_new_optdepends (alpm_pkg_t *pkg, alpm_pkg_t *old)
{
	alpm_list_t *optdepends;
	const alpm_list_t *i;

	g_return_if_fail (pkg != NULL);
	g_return_if_fail (old != NULL);

	optdepends = alpm_list_diff (alpm_pkg_get_optdepends (pkg),
				     alpm_pkg_get_optdepends (old),
				     pk_alpm_depend_compare);
	if (optdepends == NULL)
		return;

	pk_alpm_transaction_output ("New optional dependencies:\n");

	for (i = optdepends; i != NULL; i = i->next) {
		char *depend = alpm_dep_compute_string (i->data);
		g_autofree gchar *output = g_strdup_printf ("%s\n", depend);
		free (depend);
		pk_alpm_transaction_output (output);
	}

	alpm_list_free (optdepends);
}

static void
pk_alpm_transaction_upgrade_done (PkBackendJob *job, alpm_pkg_t *pkg,
				     alpm_pkg_t *old, gint direction)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	const gchar *name, *pre, *post;

	g_return_if_fail (pkg != NULL);
	g_return_if_fail (old != NULL || direction == 0);

	name = alpm_pkg_get_name (pkg);
	if (direction != 0)
		pre = alpm_pkg_get_version (old);
	post = alpm_pkg_get_version (pkg);

	if (direction > 0) {
		alpm_logaction (priv->alpm, PK_LOG_PREFIX, "upgraded %s (%s -> %s)\n",
				name, pre, post);
	} else if (direction < 0) {
		alpm_logaction (priv->alpm, PK_LOG_PREFIX,
				"downgraded %s (%s -> %s)\n", name, pre, post);
	} else {
		alpm_logaction (priv->alpm, PK_LOG_PREFIX, "reinstalled %s (%s)\n",
				name, post);
	}
	pk_alpm_pkg_emit (job, pkg, PK_INFO_ENUM_FINISHED);

	if (direction != 0)
		pk_alpm_transaction_process_new_optdepends (pkg, old);
	pk_alpm_transaction_output_end ();
}

static void
pk_alpm_transaction_sig_check (PkBackendJob *job)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_SIG_CHECK);
}

static void
pk_alpm_transaction_setup (PkBackendJob *job)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_SETUP);
}

static void
pk_alpm_transaction_repackaging (PkBackendJob *job)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_REPACKAGING);
}

static void
pk_alpm_transaction_download (PkBackendJob *job)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
}

static void
pk_alpm_transaction_optdepend_removal (PkBackendJob *job, alpm_pkg_t *pkg,
					   alpm_depend_t *optdepend)
{
	char *depend = NULL;
	g_autofree gchar *output = NULL;

	g_return_if_fail (pkg != NULL);
	g_return_if_fail (optdepend != NULL);

	depend = alpm_dep_compute_string (optdepend);
	output = g_strdup_printf ("optionally requires %s\n", depend);
	free (depend);

// 	pk_backend_job_message (job, pkg, output);
	pk_backend_job_error_code (job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
				   "%s\n%s", alpm_pkg_get_name (pkg), output);
}

static void
pk_alpm_transaction_event_cb (alpm_event_t *event)
{
	PkBackendJob* job;
	job = pkalpm_current_job;
	g_assert (job);

	/* figure out backend status and process package changes */
	switch (event->type) {
	case ALPM_EVENT_CHECKDEPS_START:
	case ALPM_EVENT_RESOLVEDEPS_START:
		pk_alpm_transaction_dep_resolve (job);
		break;
	case ALPM_EVENT_FILECONFLICTS_START:
	case ALPM_EVENT_INTERCONFLICTS_START:
	case ALPM_EVENT_DELTA_INTEGRITY_START:
	case ALPM_EVENT_DISKSPACE_START:
		pk_alpm_transaction_test_commit (job);
		break;
	case ALPM_EVENT_PACKAGE_OPERATION_START:
		{
			alpm_event_package_operation_t *e = (alpm_event_package_operation_t *) event;
			switch(e->operation) {
				case ALPM_PACKAGE_INSTALL:
					pk_alpm_transaction_add_start (job, e->newpkg);
					break;
				case ALPM_PACKAGE_REMOVE:
					pk_alpm_transaction_remove_start (job, e->oldpkg);
					break;
				case ALPM_PACKAGE_UPGRADE:
				case ALPM_PACKAGE_DOWNGRADE:
				case ALPM_PACKAGE_REINSTALL:
					pk_alpm_transaction_upgrade_start (job, e->newpkg, e->oldpkg);
					break;
			}
		}
		break;
	case ALPM_EVENT_PACKAGE_OPERATION_DONE:
		{
			alpm_event_package_operation_t *e = (alpm_event_package_operation_t *) event;
			switch(e->operation) {
				case ALPM_PACKAGE_INSTALL:
					pk_alpm_transaction_add_done (job, e->newpkg);
					break;
				case ALPM_PACKAGE_REMOVE:
					pk_alpm_transaction_remove_done (job, e->oldpkg);
					break;
				case ALPM_PACKAGE_UPGRADE:
					pk_alpm_transaction_upgrade_done (job, e->newpkg, e->oldpkg, 1);
					break;
				case ALPM_PACKAGE_DOWNGRADE:
					pk_alpm_transaction_upgrade_done (job, e->newpkg, e->oldpkg, -1);
					break;
				case ALPM_PACKAGE_REINSTALL:
					pk_alpm_transaction_upgrade_done (job, e->newpkg, e->oldpkg, 0);
					break;
			}
		}
		break;
	case ALPM_EVENT_INTEGRITY_START:
	case ALPM_EVENT_KEYRING_START:
		pk_alpm_transaction_sig_check (job);
		break;
	case ALPM_EVENT_LOAD_START:
		pk_alpm_transaction_setup (job);
		break;
	case ALPM_EVENT_DELTA_PATCHES_START:
	case ALPM_EVENT_DELTA_PATCH_START:
		pk_alpm_transaction_repackaging (job);
		break;
	case ALPM_EVENT_SCRIPTLET_INFO:
		pk_alpm_transaction_output (((alpm_event_scriptlet_info_t *) event)->line);
		break;
	case ALPM_EVENT_RETRIEVE_START:
		pk_alpm_transaction_download (job);
		break;
	case ALPM_EVENT_OPTDEP_REMOVAL:
		/* TODO: remove if this results in notification spam */
		{
			alpm_event_optdep_removal_t *e = (alpm_event_optdep_removal_t *) event;
			pk_alpm_transaction_optdepend_removal (job, e->pkg, e->optdep);
		}
		break;
	case ALPM_EVENT_CHECKDEPS_DONE:
	case ALPM_EVENT_FILECONFLICTS_DONE:
	case ALPM_EVENT_RESOLVEDEPS_DONE:
	case ALPM_EVENT_INTERCONFLICTS_DONE:
	case ALPM_EVENT_INTEGRITY_DONE:
	case ALPM_EVENT_LOAD_DONE:
	case ALPM_EVENT_DELTA_INTEGRITY_DONE:
	case ALPM_EVENT_DELTA_PATCHES_DONE:
	case ALPM_EVENT_DELTA_PATCH_DONE:
	case ALPM_EVENT_DELTA_PATCH_FAILED:
	case ALPM_EVENT_DISKSPACE_DONE:
	case ALPM_EVENT_DATABASE_MISSING:
	case ALPM_EVENT_KEYRING_DONE:
	case ALPM_EVENT_KEY_DOWNLOAD_START:
	case ALPM_EVENT_KEY_DOWNLOAD_DONE:
		/* ignored */
		break;

	default:
		g_debug ("unhandled event %d", event->type);
		break;
	}
}

static void
pk_alpm_transaction_cancelled_cb (GCancellable *object, gpointer data)
{
	PkBackend *backend = pk_backend_job_get_backend (PK_BACKEND_JOB (data));
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	alpm_trans_interrupt (priv->alpm);
}

gboolean
pk_alpm_transaction_initialize (PkBackendJob* job, alpm_transflag_t flags, const gchar* dirname, GError** error)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);

	if (alpm_trans_init (priv->alpm, flags) < 0) {
		alpm_errno_t errno = alpm_errno (priv->alpm);
		g_set_error_literal (error, PK_ALPM_ERROR, errno,
				     alpm_strerror (errno));
		return FALSE;
	}

	g_assert (pkalpm_current_job == NULL);
	pkalpm_current_job = job;
	pkalpm_dirname = dirname;

	alpm_option_set_eventcb (priv->alpm, pk_alpm_transaction_event_cb);
	alpm_option_set_questioncb (priv->alpm, pk_alpm_transaction_conv_cb);
	alpm_option_set_progresscb (priv->alpm, pk_alpm_transaction_progress_cb);

	alpm_option_set_dlcb (priv->alpm, pk_alpm_transaction_dlcb);
	alpm_option_set_totaldlcb (priv->alpm, pk_alpm_transaction_totaldlcb);

	g_cancellable_connect (pk_backend_job_get_cancellable (job),
			       G_CALLBACK (pk_alpm_transaction_cancelled_cb),
			       job, NULL);

	return TRUE;
}

static gchar *
pk_alpm_pkg_build_list (const alpm_list_t *i)
{
	GString *list;

	if (i == NULL)
		return NULL;
	list = g_string_new ("");
	for (; i != NULL; i = i->next) {
		if (i->data == NULL)
			continue;
		g_string_append_printf (list, "%s, ",
					alpm_pkg_get_name (i->data));
	}

	if (list->len > 2)
		g_string_truncate (list, list->len - 2);
	return g_string_free (list, FALSE);
}

static gchar *
pk_alpm_miss_build_list (const alpm_list_t *i)
{
	GString *list;

	if (i == NULL)
		return NULL;
	list = g_string_new ("");
	for (; i != NULL; i = i->next) {
		alpm_depmissing_t *miss = (alpm_depmissing_t *) i->data;
		char *depend = alpm_dep_compute_string (miss->depend);

		g_string_append_printf (list, "%s <- %s, ", depend,
					miss->target);
		free (depend);
	}

	g_string_truncate (list, list->len - 2);
	return g_string_free (list, FALSE);
}

static void
pk_alpm_depend_free (alpm_depend_t *depend)
{
	free (depend->name);
	free (depend->version);
	free (depend->desc);
	free (depend);
}

static void
pk_alpm_depmissing_free (gpointer miss)
{
	alpm_depmissing_t *self = (alpm_depmissing_t *) miss;

	free (self->target);
	pk_alpm_depend_free (self->depend);
	free (self->causingpkg);
	free (miss);
}

static gchar *
pk_alpm_conflict_build_list (const alpm_list_t *i)
{
	GString *list;

	if (i == NULL)
		return NULL;
	list = g_string_new ("");
	for (; i != NULL; i = i->next) {
		alpm_conflict_t *conflict = (alpm_conflict_t *) i->data;
		alpm_depend_t *depend = conflict->reason;

		if (g_strcmp0 (conflict->package1, depend->name) == 0 ||
		    g_strcmp0 (conflict->package2, depend->name) == 0) {
			g_string_append_printf (list, "%s <-> %s, ",
						conflict->package1,
						conflict->package2);
		} else {
			char *reason = alpm_dep_compute_string (depend);
			g_string_append_printf (list, "%s <-> %s (%s), ",
						conflict->package1,
						conflict->package2, reason);
			free (reason);
		}
	}

	g_string_truncate (list, list->len - 2);
	return g_string_free (list, FALSE);
}

static void
pk_alpm_conflict_free (gpointer conflict)
{
	alpm_conflict_t *self = (alpm_conflict_t *) conflict;

	free (self->package1);
	free (self->package2);
	free (conflict);
}

static gchar *
pk_alpm_fileconflict_build_list (const alpm_list_t *i)
{
	GString *list;

	if (i == NULL)
		return NULL;
	list = g_string_new ("");
	for (; i != NULL; i = i->next) {
		alpm_fileconflict_t *conflict = (alpm_fileconflict_t *) i->data;

		if (*conflict->ctarget != '\0') {
			g_string_append_printf (list, "%s <-> %s (%s), ",
						conflict->target,
						conflict->ctarget,
						conflict->file);
		} else {
			g_string_append_printf (list, "%s (%s), ",
						conflict->target,
						conflict->file);
		}
	}

	g_string_truncate (list, list->len - 2);
	return g_string_free (list, FALSE);
}

static void
pk_alpm_fileconflict_free (gpointer conflict)
{
	alpm_fileconflict_t *self = (alpm_fileconflict_t *) conflict;

	free (self->target);
	free (self->file);
	free (self->ctarget);
	free (conflict);
}

gboolean
pk_alpm_transaction_simulate (PkBackendJob *job, GError **error)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	alpm_list_t *data = NULL;
	g_autofree gchar *prefix = NULL;

	if (alpm_trans_prepare (priv->alpm, &data) >= 0)
		return TRUE;

	switch (alpm_errno (priv->alpm)) {
	case ALPM_ERR_PKG_INVALID_ARCH:
		prefix = pk_alpm_pkg_build_list (data);
		alpm_list_free (data);
		break;
	case ALPM_ERR_UNSATISFIED_DEPS:
		prefix = pk_alpm_miss_build_list (data);
		alpm_list_free_inner (data, pk_alpm_depmissing_free);
		alpm_list_free (data);
		break;
	case ALPM_ERR_CONFLICTING_DEPS:
		prefix = pk_alpm_conflict_build_list (data);
		alpm_list_free_inner (data, pk_alpm_conflict_free);
		alpm_list_free (data);
		break;
	case ALPM_ERR_FILE_CONFLICTS:
		prefix = pk_alpm_fileconflict_build_list (data);
		alpm_list_free_inner (data, pk_alpm_fileconflict_free);
		alpm_list_free (data);
		break;
	default:
		if (data != NULL)
			g_warning ("unhandled error %d", alpm_errno (priv->alpm));
		break;
	}

	if (prefix != NULL) {
		alpm_errno_t errno = alpm_errno (priv->alpm);
		g_set_error (error, PK_ALPM_ERROR, errno, "%s: %s", prefix,
			     alpm_strerror (errno));
	} else {
		alpm_errno_t errno = alpm_errno (priv->alpm);
		g_set_error_literal (error, PK_ALPM_ERROR, errno,
				     alpm_strerror (errno));
	}

	return FALSE;
}

void
pk_alpm_transaction_packages (PkBackendJob *job)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	const alpm_list_t *i;
	PkInfoEnum info;

	/* emit packages that would have been installed */
	for (i = alpm_trans_get_add (priv->alpm); i != NULL; i = i->next) {
		const gchar *name;
		if (pk_backend_job_is_cancelled (job))
			break;

		name = alpm_pkg_get_name (i->data);

		if (alpm_db_get_pkg (priv->localdb, name) != NULL) {
			info = PK_INFO_ENUM_UPDATING;
		} else {
			info = PK_INFO_ENUM_INSTALLING;
		}

		pk_alpm_pkg_emit (job, i->data, info);
	}

	switch (pk_backend_job_get_role (job)) {
	case PK_ROLE_ENUM_UPDATE_PACKAGES:
		info = PK_INFO_ENUM_OBSOLETING;
		break;

	default:
		info = PK_INFO_ENUM_REMOVING;
		break;
	}

	/* emit packages that would have been removed */
	for (i = alpm_trans_get_remove (priv->alpm); i != NULL; i = i->next) {
		if (pk_backend_job_is_cancelled (job))
			break;
		pk_alpm_pkg_emit (job, i->data, info);
	}
}

static gchar *
pk_alpm_string_build_list (const alpm_list_t *i)
{
	GString *list;

	if (i == NULL)
		return NULL;
	list = g_string_new ("");
	for (; i != NULL; i = i->next)
		g_string_append_printf (list, "%s, ", (const gchar *) i->data);

	g_string_truncate (list, list->len - 2);
	return g_string_free (list, FALSE);
}

gboolean
pk_alpm_transaction_commit (PkBackendJob *job, GError **error)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	alpm_list_t *data = NULL;
	g_autofree gchar *prefix = NULL;
	gint commit_result;

	if (pk_backend_job_is_cancelled (job))
		return TRUE;

	pk_backend_job_set_allow_cancel (job, FALSE);
	pk_backend_job_set_status (job, PK_STATUS_ENUM_RUNNING);

	pk_backend_transaction_inhibit_start (backend);
	commit_result = alpm_trans_commit (priv->alpm, &data);
	pk_backend_transaction_inhibit_end (backend);
	if (commit_result >= 0)
		return TRUE;

	switch (alpm_errno (priv->alpm)) {
	case ALPM_ERR_FILE_CONFLICTS:
		prefix = pk_alpm_fileconflict_build_list (data);
		alpm_list_free_inner (data, pk_alpm_fileconflict_free);
		alpm_list_free (data);
		break;
	case ALPM_ERR_PKG_INVALID:
	case ALPM_ERR_DLT_INVALID:
		prefix = pk_alpm_string_build_list (data);
		alpm_list_free (data);
		break;
	default:
		if (data != NULL) {
			g_warning ("unhandled error %d",
				   alpm_errno (priv->alpm));
		}
		break;
	}

	if (prefix != NULL) {
		alpm_errno_t errno = alpm_errno (priv->alpm);
		g_set_error (error, PK_ALPM_ERROR, errno, "%s: %s", prefix,
			     alpm_strerror (errno));
	} else {
		alpm_errno_t errno = alpm_errno (priv->alpm);
		g_set_error_literal (error, PK_ALPM_ERROR, errno,
				     alpm_strerror (errno));
	}

	return FALSE;
}

gboolean
pk_alpm_transaction_end (PkBackendJob *job, GError **error)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);

	alpm_option_set_eventcb (priv->alpm, NULL);
	alpm_option_set_questioncb (priv->alpm, NULL);
	alpm_option_set_progresscb (priv->alpm, NULL);

	alpm_option_set_dlcb (priv->alpm, NULL);
	alpm_option_set_totaldlcb (priv->alpm, NULL);

	if (dpkg != NULL)
		pk_alpm_transaction_download_end (job);
	if (tpkg != NULL)
		pk_alpm_transaction_output_end ();

	g_assert (pkalpm_current_job);
	pkalpm_current_job = NULL;

	if (alpm_trans_release (priv->alpm) < 0) {
		alpm_errno_t errno = alpm_errno (priv->alpm);
		g_set_error_literal (error, PK_ALPM_ERROR, errno,
				     alpm_strerror (errno));
		return FALSE;
	}

	return TRUE;
}

gboolean
pk_alpm_transaction_finish (PkBackendJob *job, GError *error)
{

	pk_alpm_transaction_end (job, (error == NULL) ? &error : NULL);

	return pk_alpm_finish (job, error);
}
