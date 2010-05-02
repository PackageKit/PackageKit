/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008, 2009 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010 Jonathan Conder <j@skurvy.no-ip.org>
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

#include "backend-error.h"
#include "backend-pacman.h"
#include "backend-repos.h"

PacmanDatabase *local_database = NULL;
static GHashTable *disabled_repos = NULL;

static GHashTable *
disabled_repos_new (GError **error)
{
	GHashTable *disabled;
	GFile *file;

	GFileInputStream *file_stream;
	GDataInputStream *data_stream;

	gchar *line;
	GError *e = NULL;

	egg_debug ("pacman: reading disabled repos from %s", PACMAN_REPO_LIST);
	file = g_file_new_for_path (PACMAN_REPO_LIST);
	file_stream = g_file_read (file, NULL, &e);

	if (file_stream == NULL) {
		g_object_unref (file);
		g_propagate_error (error, e);
		return NULL;
	}

	disabled = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	data_stream = g_data_input_stream_new (G_INPUT_STREAM (file_stream));

	/* read disabled repos line by line, ignoring comments */
	while ((line = g_data_input_stream_read_line (data_stream, NULL, NULL, &e)) != NULL) {
		g_strstrip (line);

		if (*line == '\0' || *line == '#') {
			g_free (line);
			continue;
		}

		g_hash_table_insert (disabled, line, GINT_TO_POINTER (1));
	}

	g_object_unref (data_stream);
	g_object_unref (file_stream);
	g_object_unref (file);

	if (e != NULL) {
		g_hash_table_unref (disabled);
		g_propagate_error (error, e);
		return NULL;
	} else {
		return disabled;
	}
}

static gboolean
disabled_repos_configure (GHashTable *disabled, GError **error)
{
	const PacmanList *databases;

	g_return_val_if_fail (pacman != NULL, FALSE);

	egg_debug ("pacman: reading config from %s", PACMAN_CONFIG_FILE);

	/* read configuration from pacman config file */
	if (!pacman_manager_configure (pacman, PACMAN_CONFIG_FILE, error)) {
		return FALSE;
	}

	local_database = pacman_manager_get_local_database (pacman);

	/* disable disabled repos */
	for (databases = pacman_manager_get_sync_databases (pacman); databases != NULL; databases = pacman_list_next (databases)) {
		PacmanDatabase *database = (PacmanDatabase *) pacman_list_get (databases);
		const gchar *repo = pacman_database_get_name (database);

		if (g_hash_table_lookup (disabled, repo) != NULL) {
			if (!pacman_manager_unregister_database (pacman, database, error)) {
				return FALSE;
			}

			/* start again as the list gets invalidated */
			databases = pacman_manager_get_sync_databases (pacman);
		}
	}

	return TRUE;
}

static void
disabled_repos_free (GHashTable *disabled)
{
	GHashTableIter iter;
	GFile *file;

	GFileOutputStream *file_stream;
	GDataOutputStream *data_stream;

	const gchar *line = PACMAN_REPO_LIST_HEADER "\n";

	g_return_if_fail (disabled != NULL);

	egg_debug ("pacman: storing disabled repos in %s", PACMAN_REPO_LIST);
	file = g_file_new_for_path (PACMAN_REPO_LIST);
	file_stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL);

	if (file_stream == NULL) {
		g_object_unref (file);
		g_hash_table_unref (disabled);
		return;
	}

	g_hash_table_iter_init (&iter, disabled);
	data_stream = g_data_output_stream_new (G_OUTPUT_STREAM (file_stream));

	/* write header, then all disabled repos line by line */
	if (g_data_output_stream_put_string (data_stream, line, NULL, NULL)) {
		while (g_hash_table_iter_next (&iter, (gpointer *) &line, NULL) &&
			g_data_output_stream_put_string (data_stream, line, NULL, NULL) &&
			g_data_output_stream_put_string (data_stream, "\n", NULL, NULL));
	}

	g_object_unref (data_stream);
	g_object_unref (file_stream);
	g_object_unref (file);
	g_hash_table_unref (disabled);
}

gboolean
backend_initialize_databases (PkBackend *backend, GError **error)
{
	g_return_val_if_fail (pacman != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	disabled_repos = disabled_repos_new (error);
	if (disabled_repos == NULL) {
		return FALSE;
	}

	if (!disabled_repos_configure (disabled_repos, error)) {
		return FALSE;
	}

	return TRUE;
}

void
backend_destroy_databases (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	if (disabled_repos != NULL) {
		disabled_repos_free (disabled_repos);
	}
}

static gboolean
backend_get_repo_list_thread (PkBackend *backend)
{
	const PacmanList *databases;
	GHashTableIter iter;
	gpointer key, value;

	g_return_val_if_fail (pacman != NULL, FALSE);
	g_return_val_if_fail (disabled_repos != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	/* emit enabled repos */
	for (databases = pacman_manager_get_sync_databases (pacman); databases != NULL; databases = pacman_list_next (databases)) {
		PacmanDatabase *database = (PacmanDatabase *) pacman_list_get (databases);
		const gchar *repo = pacman_database_get_name (database);

		if (backend_cancelled (backend)) {
			break;
		} else {
			pk_backend_repo_detail (backend, repo, repo, TRUE);
		}
	}

	/* emit disabled repos */
	g_hash_table_iter_init (&iter, disabled_repos);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar *repo = (const gchar *) key;

		if (backend_cancelled (backend)) {
			break;
		} else {
			pk_backend_repo_detail (backend, repo, repo, FALSE);
		}
	}

	backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_repo_list:
 **/
void
backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	g_return_if_fail (backend != NULL);

	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_get_repo_list_thread);
}

static gboolean
backend_repo_enable_thread (PkBackend *backend)
{
	GError *error = NULL;

	const gchar *repo;
	gboolean enabled;

	g_return_val_if_fail (pacman != NULL, FALSE);
	g_return_val_if_fail (disabled_repos != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	repo = pk_backend_get_string (backend, "repo_id");
	enabled = pk_backend_get_bool (backend, "enabled");

	g_return_val_if_fail (repo != NULL, FALSE);

	if (enabled) {
		/* check that repo is indeed disabled */
		if (g_hash_table_remove (disabled_repos, repo)) {
			/* reload configuration to preserve the correct order */
			if (disabled_repos_configure (disabled_repos, &error)) {
				pk_backend_repo_list_changed (backend);
			} else {
				backend_error (backend, error);
				pk_backend_thread_finished (backend);
				return FALSE;
			}
		} else {
			pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "Could not find repo [%s]", repo);
			pk_backend_thread_finished (backend);
			return FALSE;
		}
	} else {
		PacmanDatabase *database = pacman_manager_find_sync_database (pacman, repo);

		if (database != NULL) {
			if (pacman_manager_unregister_database (pacman, database, &error)) {
				g_hash_table_insert (disabled_repos, g_strdup (repo), GINT_TO_POINTER (1));
			} else {
				backend_error (backend, error);
				pk_backend_thread_finished (backend);
				return FALSE;
			}
		} else {
			pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "Could not find repo [%s]", repo);
			pk_backend_thread_finished (backend);
			return FALSE;
		}
	}

	pk_backend_thread_finished (backend);
	return TRUE;
}

/**
 * backend_repo_enable:
 **/
void
backend_repo_enable (PkBackend *backend, const gchar *repo, gboolean enabled)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (repo != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_thread_create (backend, backend_repo_enable_thread);
}
