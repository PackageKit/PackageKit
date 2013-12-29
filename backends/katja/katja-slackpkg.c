#include "katja-slackpkg.h"

G_DEFINE_TYPE(KatjaSlackpkg, katja_slackpkg, KATJA_TYPE_BINARY);

/* Static public members */
GHashTable *katja_slackpkg_cat_map = NULL;


/**
 * katja_slackpkg_real_collect_cache_info:
 **/
GSList *katja_slackpkg_real_collect_cache_info(KatjaPkgtools *pkgtools, const gchar *tmpl) {
	CURL *curl = NULL;
	gchar **source_dest, **cur_priority;
	GSList *file_list = NULL;
	GFile *tmp_dir, *repo_tmp_dir;

	/* Create the temporary directory for the repository */
	tmp_dir = g_file_new_for_path(tmpl);
	repo_tmp_dir = g_file_get_child(tmp_dir, pkgtools->name->str);
	g_file_make_directory(repo_tmp_dir, NULL, NULL);

	/* Download PACKAGES.TXT. These files are most important, break if some of them couldn't be found */
	for (cur_priority = KATJA_SLACKPKG(pkgtools)->priority; *cur_priority; cur_priority++) {
		source_dest = g_malloc_n(3, sizeof(gchar *));
		source_dest[0] = g_strconcat(pkgtools->mirror->str, *cur_priority, "/PACKAGES.TXT", NULL);
		source_dest[1] = g_build_filename(tmpl, pkgtools->name->str, "PACKAGES.TXT", NULL);
		source_dest[2] = NULL;

		if (katja_pkgtools_get_file(&curl, source_dest[0], NULL)) {
			g_strfreev(source_dest);
			g_slist_free_full(file_list, (GDestroyNotify)g_strfreev);
			goto out;
		} else
			file_list = g_slist_prepend(file_list, source_dest);
	}

out:
	g_object_unref(repo_tmp_dir);
	g_object_unref(tmp_dir);

	if (curl)
		curl_easy_cleanup(curl);

/*	FILE *fsource = NULL, *fdest = NULL;
	guint i;
	gdouble size, sum_size;
	gchar **conf_keys, *conf_key, *spawn_tmpdir = NULL, *changelog, **source_dest;
	gchar buf[2][PKGTOOLS_SPAWN_MAX_LINE_LEN];
	gboolean changed = FALSE;
	CURL *curl = NULL;

	conf_keys  = g_key_file_get_keys (spawn->conf, "Mirrors", 0, NULL);
	if (!conf_keys) return NULL; // Not tragic, mirrors aren't configured

	// Download ChangeLog.txt from all mirrors
	for (i = 0; i < g_strv_length (conf_keys); i++) {
		spawn_tmpdir = g_build_filename (tmpdir, conf_keys[i], NULL);
		if (mkdir (spawn_tmpdir, 0755)) {
			pk_backend_job_error_code (katja_pkgtools_job_progress.job,
					PK_ERROR_ENUM_CANNOT_GET_FILELIST,
					"%s",
					strerror (errno));
			g_free (spawn_tmpdir);
			break;
		}

		conf_key = g_key_file_get_value (spawn->conf, "Mirrors", conf_keys[i], NULL);

		// Download the actual ChangeLog and compare it with the old one. If force is set, comparison has no sense
		// since cache has to be downloaded in every case
		source_dest = g_malloc_n (3, sizeof (gchar *));
		source_dest[0] = g_strconcat (conf_key, "ChangeLog.txt", NULL);
		source_dest[1] = changelog = g_build_filename (spawn_tmpdir, "ChangeLog.txt", NULL);
		source_dest[2] = NULL;
		if (pkgtools_spawn_get_file (&curl, source_dest[0], source_dest[1], NULL) == CURLE_OK) { // Successful download
			if (!force) {
				changed = FALSE;
				g_free (source_dest[0]);
				source_dest[0] = g_build_filename (LOCALSTATEDIR,
						"cache",
						"PackageKit",
						"metadata",
						conf_keys[i],
						"ChangeLog.txt",
						NULL);
				// Compare
				if (g_file_test (source_dest[0], G_FILE_TEST_EXISTS)) {
					if (((fsource = fopen (source_dest[0], "r")) != NULL)
							&& ((fdest = fopen (source_dest[1], "r")) != NULL)) {
						while (!feof (fsource) && !feof (fdest)) {
							fgets (buf[0], PKGTOOLS_SPAWN_MAX_LINE_LEN, fsource);
							fgets (buf[1], PKGTOOLS_SPAWN_MAX_LINE_LEN, fdest);
							if (g_strcmp0 (buf[0], buf[1])) {
								changed = TRUE;
								break;
							}
						}
					}
					if (fsource != NULL) fclose (fsource);
					if (fdest != NULL) fclose (fdest);
				} else changed = TRUE;
			} else changed = TRUE;
		}
		g_free (source_dest[0]);

		// Get the size of the files we're going to download
		sum_size = size = 0;
		if (changed) {

			source_dest = g_malloc_n (3, sizeof (gchar *));
			source_dest[0] = g_build_filename (conf_key, "MANIFEST.bz2", NULL);
			source_dest[1] = g_build_filename (spawn_tmpdir, "MANIFEST.bz2", NULL);
			source_dest[2] = NULL;
			if (!pkgtools_spawn_get_file (curl, source_dest[0], NULL, &size) && (size != -1)) {
				file_list = g_slist_append (file_list, source_dest);
				sum_size += size;
			} else g_strfreev (source_dest);

		}

		g_free (changelog);
		g_free (conf_key);
		g_free (spawn_tmpdir);
	}
	curl_easy_cleanup (curl);

	g_strfreev (conf_keys);
*/
	return file_list;
}

/**
 * katja_slackpkg_real_generate_cache:
 **/
void katja_slackpkg_real_generate_cache(KatjaPkgtools *pkgtools, const gchar *tmpl) {
	sqlite3_stmt *insert_statement, *update_statement, *insert_default_statement, *statement;
	gchar **pkg_tokens = NULL, *pkg_filename = NULL, *location = NULL, *cat, *summary = NULL, *line;
	guint pkg_compressed = 0, pkg_uncompressed = 0;
	gushort pkg_name_len;
	GString *desc;
	GFile *tmp_dir, *repo_tmp_dir, *index_file;
	GFileInputStream *fin;
	GDataInputStream *data_in;

	/* Check if the temporary directory for this repository exists. If so the file metadata have to be generated */
	tmp_dir = g_file_new_for_path(tmpl);
	repo_tmp_dir = g_file_get_child(tmp_dir, pkgtools->name->str);
	index_file = g_file_get_child(repo_tmp_dir, "PACKAGES.TXT");
	if (!(fin = g_file_read(index_file, NULL, NULL)))
		goto out;

	/* Remove the old entries from this repository */
	sqlite3_exec(katja_pkgtools_db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);
	if (sqlite3_prepare_v2(katja_pkgtools_db,
						   "DELETE FROM repos WHERE repo LIKE @repo",
						   -1,
						   &statement,
						   NULL) == SQLITE_OK) {
		sqlite3_bind_text(statement, 1, pkgtools->name->str, -1, SQLITE_TRANSIENT);
		sqlite3_step(statement);
		sqlite3_finalize(statement);
	}

	if (sqlite3_prepare_v2(katja_pkgtools_db,
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
	if ((sqlite3_prepare_v2(katja_pkgtools_db,
						"INSERT OR REPLACE INTO pkglist (full_name, ver, arch, ext, location, "
						"summary, desc, compressed, uncompressed, name, repo_order, cat) "
						"VALUES (@full_name, @ver, @arch, @ext, @location, @summary, "
						"@desc, @compressed, @uncompressed, @name, @repo_order, @cat)",
						-1,
						&insert_statement,
						NULL) != SQLITE_OK) ||
	(sqlite3_prepare_v2(katja_pkgtools_db,
						"INSERT OR REPLACE INTO pkglist (full_name, ver, arch, ext, location, "
						"summary, desc, compressed, uncompressed, name, repo_order) "
						"VALUES (@full_name, @ver, @arch, @ext, @location, @summary, "
						"@desc, @compressed, @uncompressed, @name, @repo_order)",
						-1,
						&insert_default_statement,
						NULL) != SQLITE_OK) ||
	(sqlite3_prepare_v2(katja_pkgtools_db,
						"UPDATE pkglist SET full_name = @full_name, ver = @ver, arch = @arch, "
						"ext = @ext, location = @location, summary = @summary, "
						"desc = @desc, compressed = @compressed, uncompressed = @uncompressed "
						"WHERE name LIKE @name",
						-1,
						&update_statement,
						NULL) != SQLITE_OK)) {
							sqlite3_close(katja_pkgtools_db);
							goto out;
						}

	data_in = g_data_input_stream_new(G_INPUT_STREAM(fin));
	desc = g_string_new("");

	sqlite3_exec(katja_pkgtools_db, "BEGIN TRANSACTION", NULL, NULL, NULL);

	while ((line = g_data_input_stream_read_line(data_in, NULL, NULL, NULL))) {
		if (!strncmp(line, "PACKAGE NAME:  ", 15)) {
			pkg_filename = g_strdup(line + 15);
			if (pkgtools->blacklist && g_regex_match(pkgtools->blacklist, pkg_filename, 0, NULL)) {
				g_free(pkg_filename);
				pkg_filename = NULL;
			}
		} else if (pkg_filename && !strncmp(line, "PACKAGE LOCATION:  ", 19)) {
			location = g_strdup(line + 21); /* Exclude ./ at the path beginning */
		} else if (pkg_filename && !strncmp(line, "PACKAGE SIZE (compressed):  ", 28)) {
			/* Remove the unit (kilobytes) */
			pkg_compressed = atoi(g_strndup(line + 28, strlen(line + 28) - 2)) * 1024;
		} else if (pkg_filename && !strncmp(line, "PACKAGE SIZE (uncompressed):  ", 30)) {
			/* Remove the unit (kilobytes) */
			pkg_uncompressed = atoi(g_strndup(line + 30, strlen(line + 30) - 2)) * 1024;
		} else if (pkg_filename && !g_strcmp0(line, "PACKAGE DESCRIPTION:")) {
			g_free(line);
			line = g_data_input_stream_read_line(data_in, NULL, NULL, NULL); /* Short description */

			summary = g_strstr_len(line, -1, "(");
			if (summary) /* Else summary = NULL */
				summary = g_strndup(summary + 1, strlen(summary) - 2); /* Without ( ) */

			pkg_tokens = katja_pkgtools_cut_pkg(pkg_filename);
			pkg_name_len = strlen(pkg_tokens[0]); /* Description begins with pkg_name: */
		} else if (pkg_filename && !strncmp(line, pkg_tokens[0], pkg_name_len)) {
			g_string_append(desc, line + pkg_name_len + 1);
		} else if (pkg_filename && !g_strcmp0(line, "")) {
			if (g_strcmp0(location, "patches/packages")) { /* Insert a new package */
				/* Get the package group based on its location */
				cat = g_strrstr(location, "/");
				if (cat) /* Else cat= NULL */
					cat = g_hash_table_lookup(katja_slackpkg_cat_map, cat + 1);

				if (cat) {
					statement = insert_statement;
					sqlite3_bind_text(insert_statement, 12, cat, -1, SQLITE_TRANSIENT);
				} else {
					statement = insert_default_statement;
				}

				sqlite3_bind_int(statement, 11, pkgtools->order);
			} else { /* Update package information if it is a patch */
				statement = update_statement;
			}
			sqlite3_bind_text(statement, 1, pkg_tokens[3], -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 2, pkg_tokens[1], -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 3, pkg_tokens[2], -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 4, pkg_tokens[4], -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 5, location, -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 6, summary, -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 7, desc->str, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(statement, 8, pkg_compressed);
			sqlite3_bind_int(statement, 9, pkg_uncompressed);
			sqlite3_bind_text(statement, 10, pkg_tokens[0], -1, SQLITE_TRANSIENT);

			sqlite3_step(statement);
			sqlite3_clear_bindings(statement);
			sqlite3_reset(statement);

			/* Reset for the next package */
			g_strfreev(pkg_tokens);
			g_free(pkg_filename);
			g_free(location);
			g_free(summary);
			pkg_filename = location = summary = NULL;
			g_string_assign(desc, "");
			pkg_compressed = pkg_uncompressed = 0;
		}
		g_free(line);
	}

	sqlite3_exec(katja_pkgtools_db, "END TRANSACTION", NULL, NULL, NULL);
	sqlite3_finalize(update_statement);
	sqlite3_finalize(insert_statement);
	sqlite3_finalize(insert_default_statement);

	g_string_free(desc, TRUE);

	g_object_unref(data_in);
	g_object_unref(fin);

out:
	g_object_unref(index_file);

	katja_pkgtools_clean_dir(repo_tmp_dir, TRUE);
	g_object_unref(repo_tmp_dir);
	g_object_unref(tmp_dir);
}

/**
 * katja_slackpkg_finalize:
 **/
static void katja_slackpkg_finalize(GObject *object) {
	KatjaSlackpkg *slackpkg;

	g_return_if_fail(KATJA_IS_SLACKPKG(object));

	slackpkg = KATJA_SLACKPKG(object);
	if (slackpkg->priority)
		g_strfreev(slackpkg->priority);

	G_OBJECT_CLASS(katja_slackpkg_parent_class)->finalize(object);
}

/**
 * katja_slackpkg_class_init:
 **/
static void katja_slackpkg_class_init(KatjaSlackpkgClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	KatjaPkgtoolsClass *pkgtools_class = KATJA_PKGTOOLS_CLASS(klass);

	object_class->finalize = katja_slackpkg_finalize;

	pkgtools_class->collect_cache_info = katja_slackpkg_real_collect_cache_info;
	pkgtools_class->generate_cache = katja_slackpkg_real_generate_cache;

	katja_slackpkg_cat_map = g_hash_table_new(g_str_hash, g_str_equal);
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "a", (gpointer) "system");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "ap", (gpointer) "admin-tools");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "d", (gpointer) "programming");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "e", (gpointer) "programming");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "f", (gpointer) "documentation");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "k", (gpointer) "system");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "kde", (gpointer) "desktop-kde");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "kdei", (gpointer) "localization");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "l", (gpointer) "system");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "n", (gpointer) "network");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "t", (gpointer) "publishing");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "tcl", (gpointer) "system");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "x", (gpointer) "desktop-other");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "xap", (gpointer) "accessories");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "xfce", (gpointer) "desktop-xfce");
	g_hash_table_insert(katja_slackpkg_cat_map, (gpointer) "y", (gpointer) "games");
}

/**
 * katja_slackpkg_init:
 **/
static void katja_slackpkg_init(KatjaSlackpkg *slackpkg) {
	slackpkg->priority = NULL;
}

/**
 * katja_slackpkg_new:
 **/
KatjaSlackpkg *katja_slackpkg_new(gchar *name, gchar *mirror, guint order, gchar **priority) {
	KatjaSlackpkg *slackpkg;

	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(mirror != NULL, NULL);
	g_return_val_if_fail(priority != NULL, NULL);

	slackpkg = g_object_new(KATJA_TYPE_SLACKPKG, NULL);
	KATJA_PKGTOOLS(slackpkg)->name = g_string_new(name);
	KATJA_PKGTOOLS(slackpkg)->mirror = g_string_new(mirror);
	KATJA_PKGTOOLS(slackpkg)->order = order;
	slackpkg->priority = priority;

	return KATJA_SLACKPKG(slackpkg);
}
