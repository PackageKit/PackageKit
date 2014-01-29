#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <zlib.h>
#include <curl/curl.h>
#include <pk-backend.h>
#include "katja-slackpkg.h"
#include "katja-dl.h"

static GSList *repos = NULL;


void pk_backend_initialize(PkBackend *backend) {
	gchar *katja_conf_filename, *val, *mirror, **groups;
	guint i;
	gsize groups_len;
	GKeyFile *katja_conf;
	GError *err = NULL;
	gpointer repo = NULL;

	g_debug("backend: initialize");
	curl_global_init(CURL_GLOBAL_DEFAULT);

	/* Read the configuration file */
	katja_conf = g_key_file_new();
	katja_conf_filename = g_build_filename(SYSCONFDIR, "PackageKit", "Katja.conf", NULL);
	g_key_file_load_from_file(katja_conf, katja_conf_filename, G_KEY_FILE_NONE, &err);
	g_free(katja_conf_filename);
	if (err) {
		g_error("%s: %s", katja_conf_filename, err->message);
		g_error_free(err);
	}

	/* Initialize an object for each well-formed repository */
	groups = g_key_file_get_groups(katja_conf, &groups_len);
	for (i = 0; i < groups_len; i++) {
		if (g_key_file_has_key(katja_conf, groups[i], "Priority", NULL)) {
			mirror = g_key_file_get_string(katja_conf, groups[i], "Mirror", NULL);
			repo = katja_slackpkg_new(groups[i], mirror, i + 1, g_key_file_get_string_list(katja_conf,
																						   groups[i],
																						   "Priority",
																						   NULL,
																						   NULL));
			if (repo)
				repos = g_slist_append(repos, repo);
			g_free(mirror);
		} else if (g_key_file_has_key(katja_conf, groups[i], "IndexFile", NULL)) {
			mirror = g_key_file_get_string(katja_conf, groups[i], "Mirror", NULL);
			val = g_key_file_get_string(katja_conf, groups[i], "IndexFile", NULL);
			repo = katja_dl_new(groups[i], mirror, i + 1, val);
			g_free(val);
			g_free(mirror);

			if (repo)
				repos = g_slist_append(repos, repo);
		}

		/* Blacklist if set */
		val = g_key_file_get_string(katja_conf, groups[i], "Blacklist", NULL);
		if (repo && val)
			KATJA_PKGTOOLS(repo)->blacklist = g_regex_new(val, G_REGEX_OPTIMIZE, 0, NULL);
		g_free(val);
	}
	g_strfreev(groups);

	g_key_file_free(katja_conf);
}

void pk_backend_destroy(PkBackend *backend) {
	g_debug("backend: destroy");

	g_slist_free_full(repos, g_object_unref);
	curl_global_cleanup();
}

gchar **pk_backend_get_mime_types(PkBackend *backend) {
	const gchar *mime_types[] = {
		"application/x-xz-compressed-tar",
		"application/x-compressed-tar",
		"application/x-bzip-compressed-tar",
		"application/x-lzma-compressed-tar",
		NULL
	};

	return g_strdupv((gchar **) mime_types);
}

gboolean pk_backend_supports_parallelization(PkBackend *backend) {
	return FALSE;
}

const gchar *pk_backend_get_description(PkBackend *backend) {
	return "Katja";
}

const gchar *pk_backend_get_author(PkBackend *backend) {
	return "Eugene Wissner <belka.ew@gmail.com>";
}

PkBitfield pk_backend_get_groups(PkBackend *backend) {
	return pk_bitfield_from_enums(PK_GROUP_ENUM_COLLECTIONS,
								  PK_GROUP_ENUM_SYSTEM,
								  PK_GROUP_ENUM_ADMIN_TOOLS,
								  PK_GROUP_ENUM_PROGRAMMING,
								  PK_GROUP_ENUM_PUBLISHING,
								  PK_GROUP_ENUM_DOCUMENTATION,
								  PK_GROUP_ENUM_DESKTOP_KDE,
								  PK_GROUP_ENUM_LOCALIZATION,
								  PK_GROUP_ENUM_NETWORK,
								  PK_GROUP_ENUM_DESKTOP_OTHER,
								  PK_GROUP_ENUM_ACCESSORIES,
								  PK_GROUP_ENUM_DESKTOP_XFCE,
								  PK_GROUP_ENUM_GAMES,
								  PK_GROUP_ENUM_OTHER,
								  PK_GROUP_ENUM_UNKNOWN,
								  -1);
}

void pk_backend_start_job(PkBackend *backend, PkBackendJob *job) {
	gchar *db_filename = NULL;

	pk_backend_job_set_allow_cancel(job, TRUE);
	pk_backend_job_set_allow_cancel(job, FALSE);

	db_filename = g_build_filename(LOCALSTATEDIR, "cache", "PackageKit", "metadata", "metadata.db", NULL);
	if (sqlite3_open(db_filename, &katja_pkgtools_db) == SQLITE_OK) /* Some SQLite settings */
		sqlite3_exec(katja_pkgtools_db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);
	else
		pk_backend_job_error_code(job, PK_ERROR_ENUM_NO_CACHE,
								  "%s: %s",
								  db_filename,
								  sqlite3_errmsg(katja_pkgtools_db));
	g_free(db_filename);
}

void pk_backend_stop_job(PkBackend *backend, PkBackendJob *job) {
	sqlite3_close(katja_pkgtools_db);
}

static void pk_backend_search_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar **vals, *search;
	GString *query;
	sqlite3_stmt *statement;
	PkInfoEnum ret;

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage(job, 0);

	g_variant_get(params, "(t^a&s)", NULL, &vals);
	search = g_strjoinv("%", vals);

	query = g_string_new("SELECT (p.name || ';' || p.ver || ';' || p.arch || ';' || r.repo), p.summary, p.full_name "
						 "FROM pkglist AS p NATURAL JOIN repos AS r ");
	g_string_append_printf(query, "WHERE %s LIKE '%%%s%%'", (gchar *) user_data, search);
	g_free(search);

	if ((sqlite3_prepare_v2(katja_pkgtools_db, query->str, -1, &statement, NULL) == SQLITE_OK)) {
		/* Now we're ready to output all packages */
		while (sqlite3_step(statement) == SQLITE_ROW) {
			ret = katja_pkgtools_is_installed((gchar *) sqlite3_column_text(statement, 2));
			if ((ret == PK_INFO_ENUM_INSTALLED) || (ret == PK_INFO_ENUM_UPDATING)) {
				pk_backend_job_package(job, PK_INFO_ENUM_INSTALLED,
										(gchar *) sqlite3_column_text(statement, 0),
										(gchar *) sqlite3_column_text(statement, 1));
			} else if (ret == PK_INFO_ENUM_INSTALLING) {
				pk_backend_job_package(job, PK_INFO_ENUM_AVAILABLE,
										(gchar *) sqlite3_column_text(statement, 0),
										(gchar *) sqlite3_column_text(statement, 1));
			}
		}
		sqlite3_finalize(statement);
	} else {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(katja_pkgtools_db));
	}
	g_string_free(query, TRUE);

	pk_backend_job_set_percentage(job, 100);
	pk_backend_job_finished(job);
}

void pk_backend_search_names(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values) {
	pk_backend_job_thread_create(job, pk_backend_search_thread, (gpointer) "p.name", NULL);
}

void pk_backend_search_details(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values) {
	pk_backend_job_thread_create(job, pk_backend_search_thread, (gpointer) "p.desc", NULL);
}

void pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values) {
	pk_backend_job_thread_create(job, pk_backend_search_thread, (gpointer) "p.cat", NULL);
}

/*static void pk_backend_search_files_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar ***packages_tokens = NULL, **values, **line_tokens, *cur_pkg_data[3] = {NULL, NULL, NULL};
	gchar *cur_pkg = NULL, *gz_name, *file_list, *values_line, *package_id, *cache_path, buf[PKGTOOLS_SPAWN_MAX_LINE_LEN];
	gint was_read;
	guint n_allocated = PKGTOOLS_SPAWN_MAX_LINE_LEN, i = 0, j, packages_count = 0;
	gushort ext;
	gzFile zhl;
	GRegex *expr;
	GError *err = NULL;
	gboolean found = FALSE;
	FILE *pkglist;
	DIR *dirp;
	struct dirent *package_entry;
	GSList *cur_priority;

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
    g_variant_get(params, "(t^a&s)", NULL, &values);

	// Open the slackpkg package cache file
	cache_path = g_strjoin ("/", g_environ_getenv (slackpkg->env, "WORKDIR"), "pkglist", NULL);
	pkglist = fopen (cache_path, "r");
	g_free (cache_path);
	if (pkglist == NULL) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_CACHE, "the package list is missing: %s", strerror (errno));

		return;
	}

	// Create a regular expression searching for for one of the given words
	values_line = g_strjoinv ("|", values);
	expr = g_regex_new (values_line,
			G_REGEX_OPTIMIZE | G_REGEX_DUPNAMES,
			0,
			&err);
	if (err != NULL) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_UNKNOWN, "%s", err->message);
		g_error_free (err);
		goto out;
	}

	file_list = g_malloc_n (sizeof (gchar), n_allocated);

	for (cur_priority = slackpkg->priority; cur_priority; cur_priority = g_slist_next (cur_priority)) {
		gz_name = g_strconcat (g_environ_getenv (slackpkg->env, "WORKDIR"), "/", cur_priority->data, "-filelist.gz", NULL);
		if ((zhl = gzopen (gz_name, "rb")) == NULL) {
			g_free (gz_name);
			continue;
		}

		// The file list can be quite long, read character for character 
		while ((was_read = gzgetc (zhl)) != -1) {
			if (i > n_allocated) {
				n_allocated += PKGTOOLS_SPAWN_MAX_LINE_LEN;
				file_list = g_realloc_n (file_list, sizeof (gchar), n_allocated);
			}
			if (was_read == 10) { // Newline
				// Free memory from the last package name
				if ((cur_pkg != NULL) && !found) {
					g_free (cur_pkg);
					cur_pkg = NULL;
				}
				found = FALSE;
			} else if ((was_read == 32) && !found) { // Space
				file_list[i] = '\0';
				i = 0;
				// Some files aren't interesting for us
				if (((file_list[0] == '.') && (file_list[1] == '/') && (file_list[2] == '\0'))
						|| g_strstr_len (file_list, 8, "install/")) continue;
				// Check if it is a package or a file name
				if (file_list[0] == '.') {
					// Remember the package
					cur_pkg = g_strdup (file_list);

					// Get the package name, version and architecture
					ext = 0;
					for (j = strlen (cur_pkg) - 1; j > 0; j--) {
						if (cur_pkg[j] == '-') {
							if (ext == 0) cur_pkg[j] = '\0';
							else if (ext == 1) {
								cur_pkg_data[0] = &cur_pkg[j + 1];
								cur_pkg[j] = '\0';
							} else if (ext == 2) {
								cur_pkg_data[1] = &cur_pkg[j + 1];
								cur_pkg[j] = '\0';
							}
							ext++;
						} else if (cur_pkg[j] == '/') {
								cur_pkg_data[2] = &cur_pkg[j + 1];
								break;
						}
					}
				} else {
					if (g_regex_match (expr, file_list, 0, NULL)) {

						// Already found one with a higher priority?
						for (j = 0; j < packages_count; j++)
							if (!g_strcmp0 (cur_pkg_data[2], packages_tokens[j][1])) {
								found = TRUE;
								break;
							}

						if (!found) {
							found = TRUE;
							packages_count++;
							packages_tokens = g_realloc_n (packages_tokens, sizeof (gchar **), packages_count);
							packages_tokens[packages_count - 1] = g_malloc_n (sizeof (gchar *), 7);
							// Save package's name and priority
							packages_tokens[packages_count - 1][0] = cur_priority->data;
							packages_tokens[packages_count - 1][1] = cur_pkg_data[2];
							packages_tokens[packages_count - 1][2] = cur_pkg_data[1];
							packages_tokens[packages_count - 1][3] = cur_pkg_data[0];
							packages_tokens[packages_count - 1][4] = cur_pkg;
							packages_tokens[packages_count - 1][5] = packages_tokens[packages_count - 1][6] = NULL;
							// Find the package description
							while (!feof (pkglist)) {
								fscanf (pkglist, "%*s %s", buf);
								if (g_strcmp0 (buf, cur_pkg_data[2])) {
									fgets (buf, PKGTOOLS_SPAWN_MAX_LINE_LEN, pkglist);
								} else {
									fgetc (pkglist);
									fgets (buf, PKGTOOLS_SPAWN_MAX_LINE_LEN, pkglist);
									line_tokens = g_strsplit (buf, " ", 7);
									if (line_tokens[6] != NULL) line_tokens[6][strlen (line_tokens[6]) - 1] = '\0';
									packages_tokens[packages_count - 1][5] = g_strdup (line_tokens[3]);
									packages_tokens[packages_count - 1][6] = g_strdup (line_tokens[6]);
									g_strfreev (line_tokens);
									break;
								}
							}
							rewind (pkglist);

							// The package from *-filelist.gz wasn't found in pkglist
							if (packages_tokens[packages_count - 1][5] == NULL) {
								g_free (packages_tokens[packages_count - 1][4]);
								g_free (packages_tokens[packages_count - 1]);
								packages_count--;
								continue;
							}
						}
					}
				}
			} else if (!found) {
				file_list[i] = was_read;
				i++;
			}
		}

		gzclose_r (zhl);
		g_free (gz_name);
	}

	g_regex_unref (expr);
	g_free (file_list);

	// Now we're ready to output all packages...
	// ... but before, let us find out whether it is installed or not
	if ((dirp = opendir ("/var/log/packages")) == NULL) {
		pk_backend_job_error_code (job,
				PK_ERROR_ENUM_NO_CACHE,
				"/var/log/packages: %s", strerror (errno));

		for (i = 0; i < packages_count; i++) {
			g_free (packages_tokens[i][4]);
			g_free (packages_tokens[i][5]);
			g_free (packages_tokens[i][6]);
			g_free (packages_tokens[i]);
		}
		g_free (packages_tokens);
		goto out;
	}

	// Output
	for (i = 0; i < packages_count; i++) {
		package_id = NULL;
		while ((package_entry = readdir (dirp))) {
			if (!g_strcmp0 (package_entry->d_name, packages_tokens[i][5])) {
				package_id = g_strconcat (packages_tokens[i][1], ";", packages_tokens[i][2], ";", packages_tokens[i][3], ";installed", NULL);
				pk_backend_job_package (job, PK_INFO_ENUM_INSTALLED, package_id, packages_tokens[i][6]);
				break;
			}
		}
		if (package_id == NULL) {
			package_id = g_strconcat (packages_tokens[i][1], ";", packages_tokens[i][2], ";", packages_tokens[i][3], ";available", NULL);
			pk_backend_job_package (job, PK_INFO_ENUM_AVAILABLE, package_id, packages_tokens[i][6]);
		}
		g_free (package_id);

		g_free (packages_tokens[i][4]);
		g_free (packages_tokens[i][5]);
		g_free (packages_tokens[i][6]);
		g_free (packages_tokens[i]);

		rewinddir (dirp);
	}
	closedir (dirp);
	g_free (packages_tokens);

out:
	// Close files, free the allocated memory
	g_free (values_line);
	fclose (pkglist);

	pk_backend_job_finished (job);
}

void pk_backend_search_files(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values) {
	pk_backend_job_thread_create(job, pk_backend_search_files_thread, NULL, NULL);
}*/

static void pk_backend_get_details_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar **pkg_ids, **pkg_tokens, *homepage = NULL;
	gsize i;
	GString *desc;
	GRegex *expr;
	GMatchInfo *match_info;
	GError *err = NULL;
	sqlite3_stmt *statement = NULL;

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

	g_variant_get(params, "(^a&s)", &pkg_ids);

	if ((sqlite3_prepare_v2(katja_pkgtools_db,
							"SELECT p.desc, p.cat, p.uncompressed FROM pkglist AS p NATURAL JOIN repos AS r "
							"WHERE name LIKE @name AND p.ver LIKE @ver AND p.arch LIKE @arch AND r.repo LIKE @repo",
							-1,
							&statement,
							NULL) != SQLITE_OK)) {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(katja_pkgtools_db));
		goto out;
	}

	pkg_tokens = pk_package_id_split(pkg_ids[0]);
	sqlite3_bind_text(statement, 1, pkg_tokens[PK_PACKAGE_ID_NAME], -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, 2, pkg_tokens[PK_PACKAGE_ID_VERSION], -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, 3, pkg_tokens[PK_PACKAGE_ID_ARCH], -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(statement, 4, pkg_tokens[PK_PACKAGE_ID_DATA], -1, SQLITE_TRANSIENT);
	g_strfreev(pkg_tokens);

	if (sqlite3_step(statement) != SQLITE_ROW)
		goto out;

	desc = g_string_new((gchar *) sqlite3_column_text(statement, 0));

	/* Regular expression for searching a homepage */
	expr = g_regex_new("(?:http|ftp):\\/\\/[[:word:]\\/\\-\\.]+[[:word:]\\/](?=\\.?$)",
			G_REGEX_OPTIMIZE | G_REGEX_DUPNAMES,
			0,
			&err);
	if (err) {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_UNKNOWN, "%s", err->message);
		g_error_free(err);
		goto out;
	}
	if (g_regex_match(expr, desc->str, 0, &match_info)) {
		homepage = g_match_info_fetch(match_info, 0); /* URL */
		/* Remove the last sentence with the copied URL */
		for (i = desc->len - 1; i > 0; i--) {
			if ((desc->str[i - 1] == '.') && (desc->str[i] == ' ')) {
				g_string_truncate(desc, i);
				break;
			}
		}
		g_match_info_free(match_info);
	}
	g_regex_unref(expr);

	/* Ready */
	pk_backend_job_details(job, pkg_ids[0],
						   NULL,
						   pk_group_enum_from_string((gchar *) sqlite3_column_text(statement, 1)),
						   desc->str,
						   homepage,
						   sqlite3_column_int(statement, 2));

	g_free(homepage);
	if (desc)
		g_string_free(desc, TRUE);

out:
	sqlite3_finalize(statement);

	pk_backend_job_finished(job);
}

void pk_backend_get_details(PkBackend *backend, PkBackendJob *job, gchar **package_ids) {
	pk_backend_job_thread_create(job, pk_backend_get_details_thread, NULL, NULL);
}

static void pk_backend_resolve_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar **vals, **cur_val;
	sqlite3_stmt *statement;
	PkInfoEnum ret;

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage(job, 0);

	g_variant_get(params, "(t^a&s)", NULL, &vals);

	if ((sqlite3_prepare_v2(katja_pkgtools_db,
							"SELECT (name || ';' || ver || ';' || arch || ';' || repo), summary, full_name "
							"FROM pkglist NATURAL JOIN repos WHERE name LIKE @search",
							-1,
							&statement,
							NULL) == SQLITE_OK)) {
		/* Output packages matching each pattern */
		for (cur_val = vals; *cur_val; cur_val++) {
			sqlite3_bind_text(statement, 1, *cur_val, -1, SQLITE_TRANSIENT);

			while (sqlite3_step(statement) == SQLITE_ROW) {
				ret = katja_pkgtools_is_installed((gchar *) sqlite3_column_text(statement, 2));
				if ((ret == PK_INFO_ENUM_INSTALLED) || (ret == PK_INFO_ENUM_UPDATING)) {
					pk_backend_job_package(job, PK_INFO_ENUM_INSTALLED,
											(gchar *) sqlite3_column_text(statement, 0),
											(gchar *) sqlite3_column_text(statement, 1));
				} else if (ret == PK_INFO_ENUM_INSTALLING) {
					pk_backend_job_package(job, PK_INFO_ENUM_AVAILABLE,
											(gchar *) sqlite3_column_text(statement, 0),
											(gchar *) sqlite3_column_text(statement, 1));
				}
			}

			sqlite3_clear_bindings(statement);
			sqlite3_reset(statement);
		}
		sqlite3_finalize(statement);
	} else {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(katja_pkgtools_db));
	}

	pk_backend_job_set_percentage(job, 100);
	pk_backend_job_finished(job);
}

void pk_backend_resolve(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **packages) {
	pk_backend_job_thread_create(job, pk_backend_resolve_thread, NULL, NULL);
}

static void pk_backend_install_packages_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar *dest_dir_name, **pkg_tokens, **pkg_ids;
	guint i;
	gdouble percent_step;
	GSList *repo, *install_list = NULL, *l;
	sqlite3_stmt *pkglist_statement = NULL, *collections_statement = NULL;
    PkBitfield transaction_flags = 0;
	PkInfoEnum ret;

	g_variant_get(params, "(t^a&s)", &transaction_flags, &pkg_ids);
	pk_backend_job_set_status(job, PK_STATUS_ENUM_DEP_RESOLVE);

	if ((sqlite3_prepare_v2(katja_pkgtools_db,
							"SELECT summary, cat FROM pkglist NATURAL JOIN repos "
							"WHERE name LIKE @name AND ver LIKE @ver AND arch LIKE @arch AND repo LIKE @repo",
							-1,
							&pkglist_statement,
							NULL) != SQLITE_OK) ||
		(sqlite3_prepare_v2(katja_pkgtools_db,
						   "SELECT (c.collection_pkg || ';' || p.ver || ';' || p.arch || ';' || r.repo), p.summary, "
						   "p.full_name FROM collections AS c "
						   "JOIN pkglist AS p ON c.collection_pkg = p.name "
						   "JOIN repos AS r ON p.repo_order = r.repo_order "
						   "WHERE c.name LIKE @name AND r.repo LIKE @repo",
						   -1,
						   &collections_statement,
						   NULL) != SQLITE_OK)) {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(katja_pkgtools_db));
		goto out;
	}

	for (i = 0; pkg_ids[i]; i++) {
		pkg_tokens = pk_package_id_split(pkg_ids[i]);
		sqlite3_bind_text(pkglist_statement, 1, pkg_tokens[PK_PACKAGE_ID_NAME], -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(pkglist_statement, 2, pkg_tokens[PK_PACKAGE_ID_VERSION], -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(pkglist_statement, 3, pkg_tokens[PK_PACKAGE_ID_ARCH], -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(pkglist_statement, 4, pkg_tokens[PK_PACKAGE_ID_DATA], -1, SQLITE_TRANSIENT);

		if (sqlite3_step(pkglist_statement) == SQLITE_ROW) {

			/* If it is a collection */
			if (g_strcmp0((gchar *) sqlite3_column_text(pkglist_statement, 1), "collections")) {
				if (pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
					pk_backend_job_package(job, PK_INFO_ENUM_INSTALLING,
										   pkg_ids[i],
										   (gchar *) sqlite3_column_text(pkglist_statement, 0));
				} else {
					install_list = g_slist_append(install_list, g_strdup(pkg_ids[i]));
				}
			} else {
				sqlite3_bind_text(collections_statement, 1, pkg_tokens[PK_PACKAGE_ID_NAME], -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(collections_statement, 2, pkg_tokens[PK_PACKAGE_ID_DATA], -1, SQLITE_TRANSIENT);

				while (sqlite3_step(collections_statement) == SQLITE_ROW) {
//				g_file_set_contents("/home/belka/file.txt", (gchar *) sqlite3_column_text(collections_statement, 0), 30, NULL);
					ret = katja_pkgtools_is_installed((gchar *) sqlite3_column_text(collections_statement, 2));
					if ((ret == PK_INFO_ENUM_INSTALLING) || (ret == PK_INFO_ENUM_UPDATING)) {
						if (pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
							pk_backend_job_package(job, ret,
												   (gchar *) sqlite3_column_text(collections_statement, 0),
												   (gchar *) sqlite3_column_text(collections_statement, 1));
						} else {
							install_list = g_slist_append(install_list,
														  g_strdup((gchar *) sqlite3_column_text(collections_statement,
																								 0)));
						}
					}
				}

				sqlite3_clear_bindings(collections_statement);
				sqlite3_reset(collections_statement);
			}
		}

		sqlite3_clear_bindings(pkglist_statement);
		sqlite3_reset(pkglist_statement);
		g_strfreev(pkg_tokens);
	}

	if (install_list && !pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		/* / 2 means total percentage for installing and for downloading */
		percent_step = 100.0 / g_slist_length(install_list) / 2;

		/* Download the packages */
		pk_backend_job_set_status(job, PK_STATUS_ENUM_DOWNLOAD);
		dest_dir_name = g_build_filename(LOCALSTATEDIR, "cache", "PackageKit", "downloads", NULL);
		for (l = install_list, i = 0; l; l = g_slist_next(l), i++) {
			pk_backend_job_set_percentage(job, percent_step * i);
			pkg_tokens = pk_package_id_split(l->data);
			repo = g_slist_find_custom(repos, pkg_tokens[PK_PACKAGE_ID_DATA], katja_pkgtools_cmp_repo);

			if (repo)
				katja_pkgtools_download(KATJA_PKGTOOLS(repo->data), dest_dir_name, pkg_tokens[PK_PACKAGE_ID_NAME]);
			g_strfreev(pkg_tokens);
		}
		g_free(dest_dir_name);

		/* Install the packages */
		pk_backend_job_set_status(job, PK_STATUS_ENUM_INSTALL);
		for (l = install_list; l; l = g_slist_next(l), i++) {
			pk_backend_job_set_percentage(job, percent_step * i);
			pkg_tokens = pk_package_id_split(l->data);
			repo = g_slist_find_custom(repos, pkg_tokens[PK_PACKAGE_ID_DATA], katja_pkgtools_cmp_repo);

			if (repo)
				katja_pkgtools_install(KATJA_PKGTOOLS(repo->data), pkg_tokens[PK_PACKAGE_ID_NAME]);
			g_strfreev(pkg_tokens);
		}
	}
	g_slist_free_full(install_list, g_free);

out:
	sqlite3_finalize(pkglist_statement);
	sqlite3_finalize(collections_statement);

	pk_backend_job_finished (job);
}

void pk_backend_install_packages(PkBackend *backend, PkBackendJob *job,
								 PkBitfield transaction_flags,
								 gchar **package_ids) {
	pk_backend_job_thread_create(job, pk_backend_install_packages_thread, NULL, NULL);
}

static void pk_backend_remove_packages_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar **pkg_tokens, **pkg_ids, *cmd_line;
	guint i;
	gdouble percent_step;
    gboolean allow_deps, autoremove;
	GError *err = NULL;
    PkBitfield transaction_flags = 0;

	g_variant_get(params, "(t^a&sbb)", &transaction_flags, &pkg_ids, &allow_deps, &autoremove);
 
	if (pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		pk_backend_job_set_status(job, PK_STATUS_ENUM_DEP_RESOLVE);
	} else {
		pk_backend_job_set_status(job, PK_STATUS_ENUM_REMOVE);

		/* Add percent_step percents per removed package */
		percent_step = 100.0 / g_strv_length(pkg_ids);
		for (i = 0; pkg_ids[i]; i++) {
			pk_backend_job_set_percentage(job, percent_step * i);
			pkg_tokens = pk_package_id_split(pkg_ids[i]);
			cmd_line = g_strconcat("/sbin/removepkg ", pkg_tokens[PK_PACKAGE_ID_NAME], NULL);

			/* Pkgtools return always 0 */
			g_spawn_command_line_sync(cmd_line, NULL, NULL, NULL, &err);

			g_free(cmd_line);
			g_strfreev(pkg_tokens);

			if (err) {
				pk_backend_job_error_code(job, PK_ERROR_ENUM_PACKAGE_FAILED_TO_REMOVE, "%s", err->message);
				g_error_free(err);
				goto out;
			}

			pk_backend_job_set_percentage(job, 100);
		}
	}

out:
	pk_backend_job_finished(job);
}

void pk_backend_remove_packages(PkBackend *backend, PkBackendJob *job,
								 PkBitfield transaction_flags,
								 gchar **package_ids,
								 gboolean allow_deps,
								 gboolean autoremove) {
	pk_backend_job_thread_create(job, pk_backend_remove_packages_thread, NULL, NULL);
}

static void pk_backend_get_updates_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar *pkg_id, *pkg_full_name, *desc, **pkg_tokens;
	const gchar *pkg_metadata_filename;
	GFile *pkg_metadata_dir;
	GFileEnumerator *pkg_metadata_enumerator;
	GFileInfo *pkg_metadata_file_info;
	GError *err = NULL;
	sqlite3_stmt *statement = NULL;

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

	if ((sqlite3_prepare_v2(katja_pkgtools_db,
							"SELECT full_name, name, ver, arch, repo, summary, MIN(repo_order) "
							"FROM pkglist NATURAL JOIN repos WHERE name LIKE @name GROUP BY name",
							-1,
							&statement,
							NULL) != SQLITE_OK)) {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(katja_pkgtools_db));
		goto out;
	}

	/* Read the package metadata directory and comprare all installed packages with ones in the cache */
	pkg_metadata_dir = g_file_new_for_path("/var/log/packages");
	pkg_metadata_enumerator = g_file_enumerate_children(pkg_metadata_dir, "standard::name",
														 G_FILE_QUERY_INFO_NONE,
														 NULL,
														 &err);
	g_object_unref(pkg_metadata_dir);
	if (err) {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_NO_CACHE, "/var/log/packages: %s", err->message);
		g_error_free(err);
		goto out;
	}

	while ((pkg_metadata_file_info = g_file_enumerator_next_file(pkg_metadata_enumerator, NULL, NULL))) {
		pkg_metadata_filename = g_file_info_get_name(pkg_metadata_file_info);
		pkg_tokens = katja_pkgtools_cut_pkg(pkg_metadata_filename);

		/* Select the package from the database */
		sqlite3_bind_text(statement, 1, pkg_tokens[0], -1, SQLITE_TRANSIENT);

		/* If there are more packages with the same name, remember the one from the repository with the lowest order */
		if ((sqlite3_step(statement) == SQLITE_ROW) ||
			g_slist_find_custom(repos, ((gchar *) sqlite3_column_text(statement, 4)), katja_pkgtools_cmp_repo)) {

			pkg_full_name = g_strdup((gchar *) sqlite3_column_text(statement, 0));

			if (g_strcmp0(pkg_metadata_filename, pkg_full_name)) {
				pkg_id = pk_package_id_build((gchar *) sqlite3_column_text(statement, 1),
											 (gchar *) sqlite3_column_text(statement, 2),
											 (gchar *) sqlite3_column_text(statement, 3),
											 (gchar *) sqlite3_column_text(statement, 4));
				desc = g_strdup((gchar *) sqlite3_column_text(statement, 5));

				pk_backend_job_package(job, PK_INFO_ENUM_NORMAL, pkg_id, desc);

				g_free(desc);
				g_free(pkg_id);
			}
			g_free(pkg_full_name);
		}

		sqlite3_clear_bindings(statement);
		sqlite3_reset(statement);

		g_strfreev(pkg_tokens);
		g_object_unref(pkg_metadata_file_info);
	}
	g_object_unref(pkg_metadata_enumerator);

out:
	sqlite3_finalize(statement);

	pk_backend_job_finished (job);
}

void pk_backend_get_updates(PkBackend *backend, PkBackendJob *job, PkBitfield filters) {
	pk_backend_job_thread_create(job, pk_backend_get_updates_thread, NULL, NULL);
}

static void pk_backend_update_packages_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar *dest_dir_name, **pkg_tokens, **pkg_ids;
	guint i;
	GSList *repo;
    PkBitfield transaction_flags = 0;

	g_variant_get(params, "(t^a&s)", &transaction_flags, &pkg_ids);

	if (!pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		pk_backend_job_set_status(job, PK_STATUS_ENUM_DOWNLOAD);

		/* Download the packages */
		dest_dir_name = g_build_filename(LOCALSTATEDIR, "cache", "PackageKit", "downloads", NULL);
		for (i = 0; pkg_ids[i]; i++) {
			pkg_tokens = pk_package_id_split(pkg_ids[i]);
			repo = g_slist_find_custom(repos, pkg_tokens[PK_PACKAGE_ID_DATA], katja_pkgtools_cmp_repo);

			if (repo)
				katja_pkgtools_download(KATJA_PKGTOOLS(repo->data), dest_dir_name, pkg_tokens[PK_PACKAGE_ID_NAME]);

			g_strfreev(pkg_tokens);
		}
		g_free(dest_dir_name);

		/* Install the packages */
		pk_backend_job_set_status(job, PK_STATUS_ENUM_UPDATE);
		for (i = 0; pkg_ids[i]; i++) {
			pkg_tokens = pk_package_id_split(pkg_ids[i]);
			repo = g_slist_find_custom(repos, pkg_tokens[PK_PACKAGE_ID_DATA], katja_pkgtools_cmp_repo);

			if (repo)
				katja_pkgtools_install(KATJA_PKGTOOLS(repo->data), pkg_tokens[PK_PACKAGE_ID_NAME]);

			g_strfreev(pkg_tokens);
		}
	}

	pk_backend_job_finished(job);
}

void pk_backend_update_packages(PkBackend *backend, PkBackendJob *job,
								PkBitfield transaction_flags,
								gchar **package_ids) {
	pk_backend_job_thread_create(job, pk_backend_update_packages_thread, NULL, NULL);
}

static void pk_backend_refresh_cache_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar *tmp_dir_name, *db_err;
	gboolean force;
	GSList *file_list = NULL, *l;
	GError *err;
	CURL *curl = NULL;

	pk_backend_job_set_status(job, PK_STATUS_ENUM_DOWNLOAD_CHANGELOG);

	/* Create temporary directory */
	tmp_dir_name = g_dir_make_tmp("PackageKit.XXXXXX", &err);
	if (!tmp_dir_name) {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR, "%s", err->message);
		g_error_free(err);
		pk_backend_job_finished(job);
		return;
	}

	g_variant_get(params, "(b)", &force);

	if (force) { /* It should empty all tables */
		if (sqlite3_exec(katja_pkgtools_db, "DELETE FROM repos", NULL, 0, &db_err) != SQLITE_OK) {
			pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR, "%s", db_err);
			sqlite3_free(db_err);
			goto out;
		}
	}

	for (l = repos; l; l = g_slist_next(l))	/* Get list of files that should be downloaded */
		file_list = g_slist_concat(file_list, katja_pkgtools_collect_cache_info(l->data, tmp_dir_name));

	/* Download repository */
	pk_backend_job_set_status(job, PK_STATUS_ENUM_DOWNLOAD_REPOSITORY);

	for (l = file_list; l; l = g_slist_next(l))
		katja_pkgtools_get_file(&curl, ((gchar **)l->data)[0], ((gchar **)l->data)[1]);
	g_slist_free_full(file_list, (GDestroyNotify)g_strfreev);

	if (curl)
		curl_easy_cleanup(curl);

	/* Refresh cache */
	pk_backend_job_set_status(job, PK_STATUS_ENUM_REFRESH_CACHE);

	for (l = repos; l; l = g_slist_next(l))
		katja_pkgtools_generate_cache(l->data, tmp_dir_name);

out:
	pk_directory_remove_contents(tmp_dir_name);
	g_rmdir(tmp_dir_name);
	g_free(tmp_dir_name);

	pk_backend_job_finished(job);
}

void pk_backend_refresh_cache(PkBackend *backend, PkBackendJob *job, gboolean force) {
	pk_backend_job_thread_create(job, pk_backend_refresh_cache_thread, NULL, NULL);
}

static void pk_backend_get_update_detail_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	guint i;
	gchar **pkg_ids;

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

	g_variant_get(params, "(^a&s)", &pkg_ids);

	for (i = 0; pkg_ids[i] != NULL; i++) {
		pk_backend_job_update_detail (job,
				pkg_ids[i],
				NULL,
				NULL,
				NULL,
				NULL,
				NULL,
				PK_RESTART_ENUM_NONE,
				NULL,
				NULL,
				PK_UPDATE_STATE_ENUM_STABLE,
				NULL,
				NULL);
	}

	pk_backend_job_finished (job);
}

void pk_backend_get_update_detail(PkBackend *backend, PkBackendJob *job, gchar **package_ids) {
	pk_backend_job_thread_create(job, pk_backend_get_update_detail_thread, NULL, NULL);
}
