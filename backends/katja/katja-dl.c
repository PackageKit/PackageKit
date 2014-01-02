#include "katja-dl.h"

G_DEFINE_TYPE(KatjaDl, katja_dl, KATJA_TYPE_BINARY);

/**
 * katja_dl_real_collect_cache_info:
 **/
GSList *katja_dl_real_collect_cache_info(KatjaPkgtools *pkgtools, const gchar *tmpl) {
	CURL *curl = NULL;
	gchar **source_dest;
	GSList *file_list = NULL;
	GFile *tmp_dir, *repo_tmp_dir;

	/* Create the temporary directory for the repository */
	tmp_dir = g_file_new_for_path(tmpl);
	repo_tmp_dir = g_file_get_child(tmp_dir, pkgtools->name->str);
	g_file_make_directory(repo_tmp_dir, NULL, NULL);

	/* There is no ChangeLog yet to check if there are updates or not. Just mark the index file for download */
	source_dest = g_malloc_n(3, sizeof(gchar *));
	source_dest[0] = g_strdup(KATJA_DL(pkgtools)->index_file->str);
	source_dest[1] = g_build_filename(tmpl, pkgtools->name->str, "IndexFile", NULL);
	source_dest[2] = NULL;
	/* Check if the remote file can be found */
	if (katja_pkgtools_get_file(&curl, source_dest[0], NULL))
		g_strfreev(source_dest);
	else
		file_list = g_slist_append(file_list, source_dest);

	g_object_unref(repo_tmp_dir);
	g_object_unref(tmp_dir);

	if (curl)
		curl_easy_cleanup(curl);

	return file_list;
}

/**
 * katja_dl_real_generate_cache:
 **/
void katja_dl_real_generate_cache(KatjaPkgtools *pkgtools, const gchar *tmpl) {
	gchar **line_tokens, **pkg_tokens, *line, *collection_name = NULL;
	GFile *tmp_dir, *repo_tmp_dir, *index_file;
	GFileInputStream *fin;
	GDataInputStream *data_in;
	sqlite3_stmt *statement = NULL, *pkglist_collection_statement = NULL, *pkglist_statement = NULL;

	/* Check if the temporary directory for this repository exists. If so the file metadata have to be generated */
	tmp_dir = g_file_new_for_path(tmpl);
	repo_tmp_dir = g_file_get_child(tmp_dir, pkgtools->name->str);
	index_file = g_file_get_child(repo_tmp_dir, "IndexFile");
	if (!(fin = g_file_read(index_file, NULL, NULL)))
		goto out;

	/* Remove the old entries from this repository */
	sqlite3_exec(katja_pkgtools_sql, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);
	if (sqlite3_prepare_v2(katja_pkgtools_sql,
						   "DELETE FROM repos WHERE repo LIKE @repo",
						   -1,
						   &statement,
						   NULL) == SQLITE_OK) {
		sqlite3_bind_text(statement, 1, pkgtools->name->str, -1, SQLITE_TRANSIENT);
		sqlite3_step(statement);
		sqlite3_finalize(statement);
	}

	if (sqlite3_prepare_v2(katja_pkgtools_sql,
						   "INSERT INTO repos (repo_order, repo) VALUES (@repo_order, @repo)",
						   -1,
						   &statement,
						   NULL) != SQLITE_OK)
		goto out;
	sqlite3_bind_int(statement, 1, pkgtools->order);
	sqlite3_bind_text(statement, 2, pkgtools->name->str, -1, SQLITE_TRANSIENT);
	sqlite3_step(statement);
	sqlite3_finalize(statement);

	/* Insert new records */
	if ((sqlite3_prepare_v2(katja_pkgtools_sql,
							"INSERT INTO pkglist (full_name, name, ver, arch, "
							"summary, desc, compressed, uncompressed, cat, repo_order, ext) "
							"VALUES (@full_name, @name, @ver, @arch, @summary, "
							"@desc, @compressed, @uncompressed, 'desktop-gnome', @repo_order, @ext)",
							-1,
							&pkglist_statement,
							NULL) != SQLITE_OK) ||
		(sqlite3_prepare_v2(katja_pkgtools_sql,
							"INSERT INTO pkglist (full_name, name, ver, arch, "
							"summary, desc, compressed, uncompressed, cat, repo_order) "
							"VALUES (@full_name, @name, @ver, @arch, @summary, "
							"@desc, @compressed, @uncompressed, 'collections', @repo_order)",
							-1,
							&pkglist_collection_statement,
							NULL) != SQLITE_OK))
		goto out;

	data_in = g_data_input_stream_new(G_INPUT_STREAM(fin));
	sqlite3_exec(katja_pkgtools_sql, "BEGIN TRANSACTION", NULL, NULL, NULL);

	while ((line = g_data_input_stream_read_line(data_in, NULL, NULL, NULL))) {
		line_tokens = g_strsplit(line, ":", 0);
		if ((g_strv_length(line_tokens) > 6) &&
			(!pkgtools->blacklist || !g_regex_match(pkgtools->blacklist, line_tokens[0], 0, NULL))) {

			pkg_tokens = katja_pkgtools_cut_pkg(line_tokens[0]);

			/* If the katja_pkgtools_cut_pkg doesn't return a full name and an extension, it is a collection.
			 * We save its name in this case */
			if (!pkg_tokens[3] && !collection_name) {
				collection_name = g_strdup(pkg_tokens[0]);
				statement = pkglist_collection_statement;
				sqlite3_bind_text(statement, 1, line_tokens[0], -1, SQLITE_TRANSIENT);
			} else {
				statement = pkglist_statement;
				sqlite3_bind_text(statement, 1, pkg_tokens[3], -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(statement, 10, pkg_tokens[4], -1, SQLITE_TRANSIENT);
			}

			sqlite3_bind_text(statement, 2, pkg_tokens[0], -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 3, pkg_tokens[1], -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 4, pkg_tokens[2], -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 5, line_tokens[2], -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 6, line_tokens[2], -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(statement, 7, atoi(line_tokens[5]));
			sqlite3_bind_int(statement, 8, atoi(line_tokens[5]));
			sqlite3_bind_int(statement, 9, pkgtools->order);

			sqlite3_step(statement);
			sqlite3_clear_bindings(statement);
			sqlite3_reset(statement);

			g_strfreev(pkg_tokens);
		}
		g_strfreev(line_tokens);
		g_free(line);
	}

	/* Create a collection entry */
	if (collection_name && g_seekable_seek(G_SEEKABLE(data_in), 0, G_SEEK_SET, NULL, NULL) &&
		(sqlite3_prepare_v2(katja_pkgtools_sql,
							"INSERT INTO collections (name, repo_order, collection_pkg) "
							"VALUES (@name, @repo_order, @collection_pkg)",
							-1,
							&statement,
							NULL) == SQLITE_OK)) {

		while ((line = g_data_input_stream_read_line(data_in, NULL, NULL, NULL))) {
			line_tokens = g_strsplit(line, ":", 0);
			if ((g_strv_length(line_tokens) > 6) &&
				(!pkgtools->blacklist || !g_regex_match(pkgtools->blacklist, line_tokens[0], 0, NULL))) {

				pkg_tokens = katja_pkgtools_cut_pkg(line_tokens[0]);

				/* If not a collection itself */
				if (pkg_tokens[3]) { /* Save this package as a part of the collection */
					sqlite3_bind_text(statement, 1, collection_name, -1, SQLITE_TRANSIENT);
					sqlite3_bind_int(statement, 2, pkgtools->order);
					sqlite3_bind_text(statement, 3, pkg_tokens[0], -1, SQLITE_TRANSIENT);
					sqlite3_step(statement);
					sqlite3_clear_bindings(statement);
					sqlite3_reset(statement);
				}

				g_strfreev(pkg_tokens);
			}
			g_strfreev(line_tokens);
			g_free(line);
		}
		sqlite3_finalize(statement);
	}
	g_free(collection_name);

	sqlite3_exec(katja_pkgtools_sql, "END TRANSACTION", NULL, NULL, NULL);
	g_object_unref(data_in);

out:
	sqlite3_finalize(pkglist_statement);
	sqlite3_finalize(pkglist_collection_statement);

	if (fin)
		g_object_unref(fin);

	g_object_unref(index_file);
	katja_pkgtools_clean_dir(repo_tmp_dir, TRUE);
	g_object_unref(repo_tmp_dir);
	g_object_unref(tmp_dir);
}

/**
 * katja_dl_finalize:
 **/
static void katja_dl_finalize(GObject *object) {
	KatjaDl *dl;

	g_return_if_fail(KATJA_IS_DL(object));

	dl = KATJA_DL(object);
	if (dl->index_file)
		g_string_free(dl->index_file, TRUE);

	G_OBJECT_CLASS(katja_dl_parent_class)->finalize(object);
}

/**
 * katja_dl_class_init:
 **/
static void katja_dl_class_init(KatjaDlClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	KatjaPkgtoolsClass *pkgtools_class = KATJA_PKGTOOLS_CLASS(klass);

	object_class->finalize = katja_dl_finalize;

	pkgtools_class->collect_cache_info = katja_dl_real_collect_cache_info;
	pkgtools_class->generate_cache = katja_dl_real_generate_cache;
}

/**
 * katja_dl_init:
 **/
static void katja_dl_init(KatjaDl *dl) {
	dl->index_file = NULL;
}

/**
 * katja_dl_new:
 **/
KatjaDl *katja_dl_new(gchar *name, gchar *mirror, guint order, gchar *index_file) {
	KatjaDl *dl;

	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(mirror != NULL, NULL);
	g_return_val_if_fail(index_file != NULL, NULL);

	dl = g_object_new(KATJA_TYPE_DL, NULL);
	KATJA_PKGTOOLS(dl)->name = g_string_new(name);
	KATJA_PKGTOOLS(dl)->mirror = g_string_new(mirror);
	KATJA_PKGTOOLS(dl)->order = order;
	dl->index_file = g_string_new(index_file);

	return KATJA_DL(dl);
}
