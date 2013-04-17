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
#include "pk-backend-config.h"
#include "pk-backend-databases.h"
#include "pk-backend-error.h"

typedef struct
{
	gchar *name;
	alpm_list_t *servers;
	alpm_siglevel_t level;
} PkBackendRepo;

static GHashTable *disabled = NULL;
static alpm_list_t *configured = NULL;

static GHashTable *
disabled_repos_new (GError **error)
{
	GHashTable *table;
	GFile *file;

	GFileInputStream *is;
	GDataInputStream *input;

	GError *e = NULL;

	g_debug ("reading disabled repos from %s", PK_BACKEND_REPO_FILE);
	file = g_file_new_for_path (PK_BACKEND_REPO_FILE);
	is = g_file_read (file, NULL, &e);

	if (is == NULL) {
		g_object_unref (file);
		g_propagate_error (error, e);
		return NULL;
	}

	table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	input = g_data_input_stream_new (G_INPUT_STREAM (is));

	/* read disabled repos line by line, ignoring comments */
	while (TRUE) {
		gchar *line;

		line = g_data_input_stream_read_line (input, NULL, NULL, &e);

		if (line != NULL) {
			g_strstrip (line);
		} else {
			break;
		}

		if (*line == '\0' || *line == '#') {
			g_free (line);
			continue;
		}

		g_hash_table_insert (table, line, GINT_TO_POINTER (1));
	}

	g_object_unref (input);
	g_object_unref (is);
	g_object_unref (file);

	if (e != NULL) {
		g_hash_table_unref (table);
		g_propagate_error (error, e);
		return NULL;
	} else {
		return table;
	}
}

static void
disabled_repos_free (GHashTable *table)
{
	GHashTableIter iter;
	GFile *file;

	GFileOutputStream *os;
	GDataOutputStream *output;

	const gchar *line;

	g_return_if_fail (table != NULL);

	g_debug ("storing disabled repos in %s", PK_BACKEND_REPO_FILE);
	file = g_file_new_for_path (PK_BACKEND_REPO_FILE);
	os = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL);

	if (os == NULL) {
		g_object_unref (file);
		g_hash_table_unref (table);
		return;
	}

	g_hash_table_iter_init (&iter, table);
	output = g_data_output_stream_new (G_OUTPUT_STREAM (os));

	/* write all disabled repos line by line */
	while (g_hash_table_iter_next (&iter, (gpointer *) &line, NULL) &&
	       g_data_output_stream_put_string (output, line, NULL, NULL) &&
	       g_data_output_stream_put_byte (output, '\n', NULL, NULL));

	g_object_unref (output);
	g_object_unref (os);
	g_object_unref (file);

	g_hash_table_unref (table);
}

static gboolean
disabled_repos_configure (GHashTable *table, gboolean only_trusted,
			  GError **error)
{
	const alpm_list_t *i;

	g_return_val_if_fail (table != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);

	if (alpm_unregister_all_syncdbs (alpm) < 0) {
		alpm_errno_t errno = alpm_errno (alpm);
		g_set_error_literal (error, ALPM_ERROR, errno,
				     alpm_strerror (errno));
		return FALSE;
	}

	for (i = configured; i != NULL; i = i->next) {
		PkBackendRepo *repo = (PkBackendRepo *) i->data;
		alpm_siglevel_t level = repo->level;
		alpm_db_t *db;

		if (g_hash_table_lookup (table, repo->name) != NULL) {
			/* repo is disabled */
			continue;
		} else if (!only_trusted) {
			level &= ~ALPM_SIG_PACKAGE;
			level &= ~ALPM_SIG_DATABASE;
			level &= ~ALPM_SIG_USE_DEFAULT;
		}

		db = alpm_register_syncdb (alpm, repo->name, level);
		if (db == NULL) {
			alpm_errno_t errno = alpm_errno (alpm);
			g_set_error (error, ALPM_ERROR, errno, "[%s]: %s",
				     repo->name, alpm_strerror (errno));
			return FALSE;
		}

		alpm_db_set_servers (db, alpm_list_strdup (repo->servers));
	}

	return TRUE;
}

void
pk_backend_add_database (const gchar *name, alpm_list_t *servers,
			 alpm_siglevel_t level)
{
	PkBackendRepo *repo = g_new (PkBackendRepo, 1);

	g_return_if_fail (name != NULL);

	repo->name = g_strdup (name);
	repo->servers = alpm_list_strdup (servers);
	repo->level = level;

	configured = alpm_list_add (configured, repo);
}

gboolean
pk_backend_disable_signatures (PkBackend *self, GError **error)
{
	g_return_val_if_fail (self != NULL, FALSE);

	return disabled_repos_configure (disabled, FALSE, error);
}

gboolean
pk_backend_enable_signatures (PkBackend *self, GError **error)
{
	g_return_val_if_fail (self != NULL, FALSE);

	return disabled_repos_configure (disabled, TRUE, error);
}

gboolean
pk_backend_initialize_databases (PkBackend *self, GError **error)
{
	g_return_val_if_fail (self != NULL, FALSE);

	disabled = disabled_repos_new (error);
	if (disabled == NULL) {
		return FALSE;
	}

	if (!disabled_repos_configure (disabled, TRUE, error)) {
		return FALSE;
	}

	return TRUE;
}

void
pk_backend_destroy_databases (PkBackend *self)
{
	alpm_list_t *i;

	g_return_if_fail (self != NULL);

	if (disabled != NULL) {
		disabled_repos_free (disabled);
	}

	for (i = configured; i != NULL; i = i->next) {
		PkBackendRepo *repo = (PkBackendRepo *) i->data;
		g_free (repo->name);
		FREELIST (repo->servers);
		g_free (repo);
	}
	alpm_list_free (configured);
}

static gboolean
pk_backend_repo_info (PkBackend *self, const gchar *repo, gboolean enabled)
{
	gchar *description;
	gboolean result;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (repo != NULL, FALSE);

	description = g_strdup_printf ("[%s]", repo);
	result = pk_backend_repo_detail (self, repo, description, enabled);
	g_free (description);

	return result;
}

static void
pk_backend_get_repo_list_thread (PkBackend *self, gpointer data)
{
	const alpm_list_t *i;
	GHashTableIter iter;
	gpointer key, value;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);
	g_return_val_if_fail (disabled != NULL, FALSE);

	/* emit enabled repos */
	for (i = alpm_get_syncdbs (alpm); i != NULL; i = i->next) {
		alpm_db_t *db = (alpm_db_t *) i->data;
		const gchar *repo = alpm_db_get_name (db);

		if (pk_backend_cancelled (self)) {
			goto out;
		} else {
			pk_backend_repo_info (self, repo, TRUE);
		}
	}

	/* emit disabled repos */
	g_hash_table_iter_init (&iter, disabled);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar *repo = (const gchar *) key;

		if (pk_backend_cancelled (self)) {
			goto out;
		} else {
			pk_backend_repo_info (self, repo, FALSE);
		}
	}

out:
	pk_backend_finish (self, NULL);
}

void
pk_backend_get_repo_list (PkBackend *self, PkBitfield filters)
{
	g_return_if_fail (self != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_QUERY,
			pk_backend_get_repo_list_thread, NULL, NULL);
}

static void
pk_backend_repo_enable_thread (PkBackend *self, gpointer data)
{
	const gchar *repo;

	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (disabled != NULL, FALSE);

	repo = pk_backend_get_string (self, "repo_id");

	g_return_val_if_fail (repo != NULL, FALSE);

	if (g_hash_table_remove (disabled, repo)) {
		/* reload configuration to preserve ordering */
		if (disabled_repos_configure (disabled, TRUE, &error)) {
			pk_backend_repo_list_changed (self);
		}
	} else {
		int code = ALPM_ERR_DB_NOT_NULL;
		g_set_error (&error, ALPM_ERROR, code, "[%s]: %s",
			     repo, alpm_strerror (code));
	}

	if (error != NULL) {
		pk_backend_error (self, error);
		g_error_free (error);
	}

	pk_backend_finished (self);
}

static void
pk_backend_repo_disable_thread (PkBackend *self, gpointer data)
{
	const alpm_list_t *i;
	const gchar *repo;

	GError *error = NULL;

	g_return_if_fail (self != NULL);
	g_return_if_fail (alpm != NULL);
	g_return_if_fail (disabled != NULL);

	repo = pk_backend_get_string (self, "repo_id");

	g_return_if_fail (repo != NULL);

	for (i = alpm_get_syncdbs (alpm); i != NULL; i = i->next) {
		alpm_db_t *db = (alpm_db_t *) i->data;
		const gchar *name = alpm_db_get_name (db);

		if (g_strcmp0 (repo, name) == 0) {
			if (alpm_db_unregister (db) < 0) {
				alpm_errno_t errno = alpm_errno (alpm);
				g_set_error (&error, ALPM_ERROR, errno,
					     "[%s]: %s", repo,
					     alpm_strerror (errno));
			} else {
				g_hash_table_insert (disabled, g_strdup (repo),
						     GINT_TO_POINTER (1));
			}
			break;
		}
	}

	if (i == NULL) {
		int code = ALPM_ERR_DB_NULL;
		g_set_error (&error, ALPM_ERROR, code, "[%s]: %s", repo,
			     alpm_strerror (code));
	}

	if (error != NULL) {
		pk_backend_error (self, error);
		g_error_free (error);
	}

	pk_backend_finished (self);
}

void
pk_backend_repo_enable (PkBackend *self, const gchar *repo_id, gboolean enabled)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (repo_id != NULL);

	pk_backend_set_status (self, PK_STATUS_ENUM_QUERY);

	if (enabled) {
		pk_backend_thread_create (self, pk_backend_repo_enable_thread,
					  NULL, NULL);
	} else {
		pk_backend_thread_create (self, pk_backend_repo_disable_thread,
					  NULL, NULL);
	}
}
