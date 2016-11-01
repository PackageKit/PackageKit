#include "katja-slackpkg.h"

G_DEFINE_TYPE(KatjaSlackpkg, katja_slackpkg, KATJA_TYPE_PKGTOOLS);

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
	repo_tmp_dir = g_file_get_child(tmp_dir, katja_pkgtools_get_name(KATJA_PKGTOOLS(pkgtools)));
	g_file_make_directory(repo_tmp_dir, NULL, NULL);

	/* Download PACKAGES.TXT. These files are most important, break if some of them couldn't be found */
	for (cur_priority = KATJA_SLACKPKG(pkgtools)->priority; *cur_priority; cur_priority++) {
		source_dest = g_malloc_n(3, sizeof(gchar *));
		source_dest[0] = g_strconcat(katja_pkgtools_get_mirror(KATJA_PKGTOOLS(pkgtools)),
									 *cur_priority,
									 "/PACKAGES.TXT",
									 NULL);
		source_dest[1] = g_build_filename(tmpl, katja_pkgtools_get_name(KATJA_PKGTOOLS(pkgtools)), "PACKAGES.TXT", NULL);
		source_dest[2] = NULL;

		if (katja_get_file(&curl, source_dest[0], NULL) == CURLE_OK) {
			file_list = g_slist_prepend(file_list, source_dest);
		} else {
			g_strfreev(source_dest);
			g_slist_free_full(file_list, (GDestroyNotify)g_strfreev);
			goto out;
		}

		/* Download file lists if available */
		source_dest = g_malloc_n(3, sizeof(gchar *));
		source_dest[0] = g_strconcat(katja_pkgtools_get_mirror(KATJA_PKGTOOLS(pkgtools)),
									 *cur_priority,
									 "/MANIFEST.bz2",
									 NULL);
		source_dest[1] = g_strconcat(tmpl,
									 "/", katja_pkgtools_get_name(KATJA_PKGTOOLS(pkgtools)),
									 "/", *cur_priority, "-MANIFEST.bz2",
									 NULL);
		source_dest[2] = NULL;
		if (katja_get_file(&curl, source_dest[0], NULL) == CURLE_OK)
			file_list = g_slist_prepend(file_list, source_dest);
		else
			g_strfreev(source_dest);
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
void katja_slackpkg_real_generate_cache(KatjaPkgtools *pkgtools, PkBackendJob *job, const gchar *tmpl) {
	gchar **pkg_tokens = NULL, **cur_priority;
	gchar *query = NULL, *filename = NULL, *location = NULL, *cat, *summary = NULL, *line, *packages_txt;
	guint pkg_compressed = 0, pkg_uncompressed = 0;
	gushort pkg_name_len;
	GString *desc;
	GFile *list_file;
	GFileInputStream *fin;
	GDataInputStream *data_in;
	sqlite3_stmt *insert_statement = NULL, *update_statement = NULL, *insert_default_statement = NULL, *statement;
	PkBackendKatjaJobData *job_data = pk_backend_job_get_user_data(job);

	/* Check if the temporary directory for this repository exists, then the file metadata have to be generated */
	packages_txt = g_build_filename(tmpl, katja_pkgtools_get_name(KATJA_PKGTOOLS(pkgtools)), "PACKAGES.TXT", NULL);
	list_file = g_file_new_for_path(packages_txt);
	fin = g_file_read(list_file, NULL, NULL);
	g_object_unref(list_file);
	g_free(packages_txt);
	if (!fin)
		goto out;

	/* Remove the old entries from this repository */
	if (sqlite3_prepare_v2(job_data->db,
						   "DELETE FROM repos WHERE repo LIKE @repo",
						   -1,
						   &statement,
						   NULL) == SQLITE_OK) {
		sqlite3_bind_text(statement, 1, katja_pkgtools_get_name(KATJA_PKGTOOLS(pkgtools)), -1, SQLITE_TRANSIENT);
		sqlite3_step(statement);
		sqlite3_finalize(statement);
	}

	if (sqlite3_prepare_v2(job_data->db,
						   "INSERT INTO repos (repo_order, repo) VALUES (@repo_order, @repo)",
						   -1,
						   &statement,
						   NULL) != SQLITE_OK)
		goto out;
	sqlite3_bind_int(statement, 1, katja_pkgtools_get_order(KATJA_PKGTOOLS(pkgtools)));
	sqlite3_bind_text(statement, 2, katja_pkgtools_get_name(KATJA_PKGTOOLS(pkgtools)), -1, SQLITE_TRANSIENT);
	sqlite3_step(statement);
	sqlite3_finalize(statement);

	/* Insert new records */
	if ((sqlite3_prepare_v2(job_data->db,
						"INSERT OR REPLACE INTO pkglist (full_name, ver, arch, ext, location, "
						"summary, desc, compressed, uncompressed, name, repo_order, cat) "
						"VALUES (@full_name, @ver, @arch, @ext, @location, @summary, "
						"@desc, @compressed, @uncompressed, @name, @repo_order, @cat)",
						-1,
						&insert_statement,
						NULL) != SQLITE_OK) ||
	(sqlite3_prepare_v2(job_data->db,
						"INSERT OR REPLACE INTO pkglist (full_name, ver, arch, ext, location, "
						"summary, desc, compressed, uncompressed, name, repo_order) "
						"VALUES (@full_name, @ver, @arch, @ext, @location, @summary, "
						"@desc, @compressed, @uncompressed, @name, @repo_order)",
						-1,
						&insert_default_statement,
						NULL) != SQLITE_OK)) 
		goto out;

	query = sqlite3_mprintf("UPDATE pkglist SET full_name = @full_name, ver = @ver, arch = @arch, "
							"ext = @ext, location = @location, summary = @summary, "
							"desc = @desc, compressed = @compressed, uncompressed = @uncompressed "
							"WHERE name LIKE @name AND repo_order = %u",
							katja_pkgtools_get_order(KATJA_PKGTOOLS(pkgtools)));
	if (sqlite3_prepare_v2(job_data->db, query, -1, &update_statement, NULL) != SQLITE_OK)
		goto out;

	data_in = g_data_input_stream_new(G_INPUT_STREAM(fin));
	desc = g_string_new("");

	sqlite3_exec(job_data->db, "BEGIN TRANSACTION", NULL, NULL, NULL);

	while ((line = g_data_input_stream_read_line(data_in, NULL, NULL, NULL))) {
		if (!strncmp(line, "PACKAGE NAME:  ", 15)) {
			filename = g_strdup(line + 15);
			if (katja_pkgtools_get_blacklist(KATJA_PKGTOOLS(pkgtools)) &&
				g_regex_match(katja_pkgtools_get_blacklist(KATJA_PKGTOOLS(pkgtools)), filename, 0, NULL)) {

				g_free(filename);
				filename = NULL;
			}
		} else if (filename && !strncmp(line, "PACKAGE LOCATION:  ", 19)) {
			location = g_strdup(line + 21); /* Exclude ./ at the path beginning */
		} else if (filename && !strncmp(line, "PACKAGE SIZE (compressed):  ", 28)) {
			/* Remove the unit (kilobytes) */
			pkg_compressed = atoi(g_strndup(line + 28, strlen(line + 28) - 2)) * 1024;
		} else if (filename && !strncmp(line, "PACKAGE SIZE (uncompressed):  ", 30)) {
			/* Remove the unit (kilobytes) */
			pkg_uncompressed = atoi(g_strndup(line + 30, strlen(line + 30) - 2)) * 1024;
		} else if (filename && !g_strcmp0(line, "PACKAGE DESCRIPTION:")) {
			g_free(line);
			line = g_data_input_stream_read_line(data_in, NULL, NULL, NULL); /* Short description */

			summary = g_strstr_len(line, -1, "(");
			if (summary) /* Else summary = NULL */
				summary = g_strndup(summary + 1, strlen(summary) - 2); /* Without ( ) */

			pkg_tokens = katja_cut_pkg(filename);
			pkg_name_len = strlen(pkg_tokens[0]); /* Description begins with pkg_name: */
		} else if (filename && !strncmp(line, pkg_tokens[0], pkg_name_len)) {
			g_string_append(desc, line + pkg_name_len + 1);
		} else if (filename && !g_strcmp0(line, "")) {
			if (g_strcmp0(location, "patches/packages")) { /* Insert a new package */
				/* Get the package group based on its location */
				cat = g_strrstr(location, "/");
				if (cat) /* Else cat = NULL */
					cat = g_hash_table_lookup(katja_slackpkg_cat_map, cat + 1);

				if (cat) {
					statement = insert_statement;
					sqlite3_bind_text(insert_statement, 12, cat, -1, SQLITE_TRANSIENT);
				} else {
					statement = insert_default_statement;
				}

				sqlite3_bind_int(statement, 11, katja_pkgtools_get_order(KATJA_PKGTOOLS(pkgtools)));
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
			g_free(filename);
			g_free(location);
			g_free(summary);
			filename = location = summary = NULL;
			g_string_assign(desc, "");
			pkg_compressed = pkg_uncompressed = 0;
		}
		g_free(line);
	}

	sqlite3_exec(job_data->db, "END TRANSACTION", NULL, NULL, NULL);

	g_string_free(desc, TRUE);

	g_object_unref(data_in);
	g_object_unref(fin);

	/* Parse MANIFEST.bz2 */
	for (cur_priority = KATJA_SLACKPKG(pkgtools)->priority; *cur_priority; cur_priority++) {
		filename = g_strconcat(*cur_priority, "-MANIFEST.bz2", NULL);
		katja_pkgtools_manifest(pkgtools, job, tmpl, filename);
		g_free(filename);
	}

out:
	sqlite3_finalize(update_statement);
	sqlite3_free(query);
	sqlite3_finalize(insert_default_statement);
	sqlite3_finalize(insert_statement);

	if (fin)
		g_object_unref(fin);
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
KatjaSlackpkg *katja_slackpkg_new(gchar *name, gchar *mirror, gushort order, gchar *blacklist, gchar **priority) {
	KatjaSlackpkg *slackpkg;

	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(mirror != NULL, NULL);
	g_return_val_if_fail(priority != NULL, NULL);

	slackpkg = g_object_new(KATJA_TYPE_SLACKPKG, NULL);
	KATJA_PKGTOOLS(slackpkg)->name = name;
	KATJA_PKGTOOLS(slackpkg)->mirror = mirror;
	KATJA_PKGTOOLS(slackpkg)->order = order;

	if (blacklist) /* Blacklist if set */
		KATJA_PKGTOOLS(slackpkg)->blacklist = g_regex_new(blacklist, G_REGEX_OPTIMIZE, 0, NULL);

	slackpkg->priority = priority;

	return KATJA_SLACKPKG(slackpkg);
}
