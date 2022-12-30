/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#include <gmodule.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <string.h>
#include <appstream-glib.h>
#include <unistd.h>
#include <stdlib.h>

#include <pk-backend.h>
#include <packagekit-glib2/pk-common-private.h>
#include <packagekit-glib2/pk-debug.h>

#include <libdnf/libdnf.h>
#include <libdnf/dnf-advisory.h>
#include <libdnf/dnf-advisoryref.h>
#include <libdnf/dnf-db.h>
#include <libdnf/hy-packageset.h>
#include <libdnf/hy-query.h>
#include <libdnf/dnf-version.h>
#include <libdnf/dnf-sack.h>
#include <libdnf/hy-util.h>
#include <librepo/librepo.h>
#include <rpm/rpmlib.h>

#include "dnf-backend-vendor.h"
#include "dnf-backend.h"
#include "pk-backend-dnf-common.h"

static gboolean
pk_backend_refresh_repo (guint max_cache_age,
                         DnfRepo *repo,
                         DnfState *state,
                         GError **error)
{
	gboolean ret;
	gboolean repo_okay;
	DnfState *state_local;
	GError *error_local = NULL;

	/* set state */
	ret = dnf_state_set_steps (state, error,
				   2, /* check */
				   98, /* download */
				   -1);
	if (!ret)
		return FALSE;

	/* is the repo up to date? */
	state_local = dnf_state_get_child (state);
	repo_okay = dnf_repo_check (repo,
	                            max_cache_age,
	                            state_local,
	                            &error_local);
	if (!repo_okay) {
		g_debug ("repo %s not okay [%s], refreshing",
			 dnf_repo_get_id (repo), error_local->message);
		g_clear_error (&error_local);
		if (!dnf_state_finished (state_local, error))
			return FALSE;
	}

	/* done */
	if (!dnf_state_done (state, error))
		return FALSE;

	/* update repo, TODO: if we have network access */
	if (!repo_okay) {
		state_local = dnf_state_get_child (state);
		ret = dnf_repo_update (repo,
		                       DNF_REPO_UPDATE_FLAG_IMPORT_PUBKEY,
		                       state_local,
		                       &error_local);
		if (!ret) {
			if (g_error_matches (error_local,
					     DNF_ERROR,
					     DNF_ERROR_CANNOT_FETCH_SOURCE)) {
				g_warning ("Skipping refresh of %s: %s",
					   dnf_repo_get_id (repo),
					   error_local->message);
				g_clear_error (&error_local);
				if (!dnf_state_finished (state_local, error))
					return FALSE;
			} else {
				g_propagate_error (error, error_local);
				return FALSE;
			}
		}
	}

	/* copy the appstream files somewhere that the GUI will pick them up */
	if (!dnf_utils_refresh_repo_appstream (repo, error))
		return FALSE;

	/* done */
	return dnf_state_done (state, error);
}

int
main (int argc, char *argv[])
{
	guint max_cache_age, i, ret;
	g_autofree gchar *conf_filename = NULL;
	g_autoptr(GKeyFile) conf = NULL;
	g_autoptr(DnfState) state = NULL;
	g_autoptr(DnfContext) context = NULL;
	g_autoptr(GPtrArray) repos = NULL;
	g_autoptr(GError) error = NULL;

	if (argc != 4) {
		printf("Use: packagekit-dnf-refresh-repo <age> <repo-id> <release-ver>\n");
		return 1;
	}

	conf = g_key_file_new ();
	conf_filename = pk_util_get_config_filename ();
	if (conf_filename == NULL) {
		g_printerr ("%s\n", _("Config file was not found."));
		return 1;
	}
	ret = g_key_file_load_from_file (conf, conf_filename,
					 G_KEY_FILE_NONE, &error);
	if (!ret) {
		/* TRANSLATORS: The placeholder is an error message */
		g_autofree gchar *message = g_strdup_printf (_("Failed to load config file: %s"), error->message);
		g_printerr ("%s\n", message);
		return 1;
	}

	max_cache_age = atoi(argv[1]);
	context = dnf_context_new ();
	if (!pk_backend_setup_dnf_context (context, conf, argv[3], &error))
		return 1;
	repos = dnf_repo_loader_get_repos (dnf_context_get_repo_loader (context), &error);
	if (repos == NULL)
		return 1;
	for (i = 0; i < repos->len; i++) {
                DnfRepo *repo = g_ptr_array_index (repos, i);
		if (strcmp(dnf_repo_get_id (repo), argv[2]) == 0) {
			state = dnf_state_new ();
			pk_backend_refresh_repo (max_cache_age,
						 repo,
						 state,
						 &error);
		}
        }
}
