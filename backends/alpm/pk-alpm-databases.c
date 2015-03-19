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
#include "pk-alpm-config.h"
#include "pk-alpm-databases.h"
#include "pk-alpm-error.h"

typedef struct
{
	gchar *name;
	alpm_list_t *servers;
	alpm_siglevel_t level;
} PkBackendRepo;

static GHashTable *
pk_alpm_disabled_repos_new (GError **error)
{
	GHashTable *table;
	GError *e = NULL;
	_cleanup_object_unref_ GDataInputStream *input = NULL;
	_cleanup_object_unref_ GFile *file = NULL;
	_cleanup_object_unref_ GFileInputStream *is = NULL;

	g_debug ("reading disabled repos from %s", PK_BACKEND_REPO_FILE);
	file = g_file_new_for_path (PK_BACKEND_REPO_FILE);
	is = g_file_read (file, NULL, &e);
	if (is == NULL) {
		g_propagate_error (error, e);
		return NULL;
	}

	table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	input = g_data_input_stream_new (G_INPUT_STREAM (is));

	/* read disabled repos line by line, ignoring comments */
	while (TRUE) {
		gchar *line;

		line = g_data_input_stream_read_line (input, NULL, NULL, &e);
		if (line == NULL)
			break;
		g_strstrip (line);
		if (*line == '\0' || *line == '#') {
			g_free (line);
			continue;
		}

		g_hash_table_insert (table, line, GINT_TO_POINTER (1));
	}

	if (e != NULL) {
		g_hash_table_unref (table);
		g_propagate_error (error, e);
		return NULL;
	}
	return table;
}

static void
pk_alpm_disabled_repos_free (GHashTable *table)
{
	GHashTableIter iter;
	_cleanup_object_unref_ GDataOutputStream *output = NULL;
	_cleanup_object_unref_ GFile *file = NULL;
	_cleanup_object_unref_ GFileOutputStream *os = NULL;

	const gchar *line;

	g_return_if_fail (table != NULL);

	g_debug ("storing disabled repos in %s", PK_BACKEND_REPO_FILE);
	file = g_file_new_for_path (PK_BACKEND_REPO_FILE);
	os = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL);
	if (os == NULL) {
		g_hash_table_unref (table);
		return;
	}

	g_hash_table_iter_init (&iter, table);
	output = g_data_output_stream_new (G_OUTPUT_STREAM (os));

	/* write all disabled repos line by line */
	while (g_hash_table_iter_next (&iter, (gpointer *) &line, NULL) &&
	       g_data_output_stream_put_string (output, line, NULL, NULL) &&
	       g_data_output_stream_put_byte (output, '\n', NULL, NULL));

	g_hash_table_unref (table);
}

static gboolean
pk_alpm_disabled_repos_configure (PkBackend *backend, GHashTable *table, gboolean only_trusted, GError **error)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	const alpm_list_t *i;

	g_return_val_if_fail (table != NULL, FALSE);

	if (alpm_unregister_all_syncdbs (priv->alpm) < 0) {
		alpm_errno_t errno = alpm_errno (priv->alpm);
		g_set_error_literal (error, PK_ALPM_ERROR, errno,
				     alpm_strerror (errno));
		return FALSE;
	}

	for (i = priv->configured_repos; i != NULL; i = i->next) {
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

		db = alpm_register_syncdb (priv->alpm, repo->name, level);
		if (db == NULL) {
			alpm_errno_t errno = alpm_errno (priv->alpm);
			g_set_error (error, PK_ALPM_ERROR, errno, "[%s]: %s",
				     repo->name, alpm_strerror (errno));
			return FALSE;
		}

		alpm_db_set_servers (db, alpm_list_strdup (repo->servers));
	}

	return TRUE;
}

void
pk_alpm_add_database (PkBackend *backend, const gchar *name, alpm_list_t *servers,
			 alpm_siglevel_t level)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	PkBackendRepo *repo = g_new (PkBackendRepo, 1);

	g_return_if_fail (name != NULL);

	repo->name = g_strdup (name);
	repo->servers = alpm_list_strdup (servers);
	repo->level = level;

	priv->configured_repos = alpm_list_add (priv->configured_repos, repo);
}

gboolean
pk_alpm_disable_signatures (PkBackend *backend, GError **error)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	return pk_alpm_disabled_repos_configure (backend, priv->disabled_repos, FALSE, error);
}

gboolean
pk_alpm_enable_signatures (PkBackend *backend, GError **error)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	return pk_alpm_disabled_repos_configure (backend, priv->disabled_repos, TRUE, error);
}

gboolean
pk_alpm_initialize_databases (PkBackend *backend, GError **error)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	priv->disabled_repos = pk_alpm_disabled_repos_new (error);
	if (priv->disabled_repos == NULL)
		return FALSE;

	if (!pk_alpm_disabled_repos_configure (backend, priv->disabled_repos, TRUE, error))
		return FALSE;

	return TRUE;
}

void
pk_alpm_destroy_databases (PkBackend *backend)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	alpm_list_t *i;

	if (priv->disabled_repos != NULL)
		pk_alpm_disabled_repos_free (priv->disabled_repos);

	for (i = priv->configured_repos; i != NULL; i = i->next) {
		PkBackendRepo *repo = (PkBackendRepo *) i->data;
		g_free (repo->name);
		FREELIST (repo->servers);
		g_free (repo);
	}
	alpm_list_free (priv->configured_repos);
}

static gboolean
pk_backend_repo_info (PkBackendJob *job, const gchar *repo, gboolean enabled)
{
	_cleanup_free_ gchar *description = NULL;
	description = g_strdup_printf ("[%s]", repo);
	pk_backend_job_repo_detail (job, repo, description, enabled);
	return TRUE;
}

static void
pk_backend_get_repo_list_thread (PkBackendJob *job, GVariant *params, gpointer data)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	const alpm_list_t *i;
	GHashTableIter iter;
	gpointer key, value;

	g_return_if_fail (priv->disabled_repos != NULL);

	/* emit enabled repos */
	for (i = alpm_get_syncdbs (priv->alpm); i != NULL; i = i->next) {
		alpm_db_t *db = (alpm_db_t *) i->data;
		const gchar *repo = alpm_db_get_name (db);
		if (pk_backend_job_is_cancelled (job))
			return;
		pk_backend_repo_info (job, repo, TRUE);
	}

	/* emit disabled repos */
	g_hash_table_iter_init (&iter, priv->disabled_repos);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar *repo = (const gchar *) key;
		if (pk_backend_job_is_cancelled (job))
			return;
		pk_backend_repo_info (job, repo, FALSE);
	}
}

void
pk_backend_get_repo_list (PkBackend *self,
			  PkBackendJob *job,
			  PkBitfield filters)
{
	pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_backend_get_repo_list_thread, NULL);
}
