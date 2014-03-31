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


void pk_backend_initialize(GKeyFile *conf, PkBackend *backend) {
	gchar *path, *val, *mirror, **groups;
	gint ret;
	guint i;
	gsize groups_len;
	GFile *katja_conf_file;
	GFileInfo *file_info;
	GKeyFile *katja_conf;
	GError *err = NULL;
	gpointer repo = NULL;
	sqlite3_stmt *stmt;

	g_debug("backend: initialize");
	curl_global_init(CURL_GLOBAL_DEFAULT);

	/* Open the database. We will need it to save the time the configuration file was last modified. */
	path = g_build_filename(LOCALSTATEDIR, "cache", "PackageKit", "metadata", "metadata.db", NULL);
	if (sqlite3_open(path, &katja_pkgtools_db) != SQLITE_OK)
		g_error("%s: %s", path, sqlite3_errmsg(katja_pkgtools_db));
	g_free(path);

	/* Read the configuration file */
	katja_conf = g_key_file_new();
	path = g_build_filename(SYSCONFDIR, "PackageKit", "Katja.conf", NULL);
	g_key_file_load_from_file(katja_conf, path, G_KEY_FILE_NONE, &err);
	if (err) {
		g_error("%s: %s", path, err->message);
		g_error_free(err);
	}

	katja_conf_file = g_file_new_for_path(path);
	if (!(file_info = g_file_query_info(katja_conf_file,
										"time::modified-usec",
										G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
										NULL,
										&err))) {
		g_error("%s", err->message);
		g_error_free(err);
	}

	if ((ret = sqlite3_prepare_v2(katja_pkgtools_db,
							"UPDATE cache_info SET value = ? WHERE key LIKE 'last_modification'",
							-1,
							&stmt,
							NULL)) == SQLITE_OK) {
		ret = sqlite3_bind_int(stmt, 1, g_file_info_get_attribute_uint32(file_info, "time::modified-usec"));
		if (ret == SQLITE_OK)
			ret = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}
	if ((ret != SQLITE_OK) && (ret != SQLITE_DONE))
		g_error("%s: %s", path, sqlite3_errstr(ret));
	else if (!sqlite3_changes(katja_pkgtools_db))
		g_error("Failed to update database: %s", path);

	g_object_unref(file_info);
	g_object_unref(katja_conf_file);
	sqlite3_close_v2(katja_pkgtools_db);
	g_free(path);

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
	gchar *query;
	sqlite3_stmt *stmt;
	PkInfoEnum ret;

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage(job, 0);

	g_variant_get(params, "(t^a&s)", NULL, &vals);
	search = g_strjoinv("%", vals);

	query = sqlite3_mprintf("SELECT (p.name || ';' || p.ver || ';' || p.arch || ';' || r.repo), p.summary, "
							"p.full_name FROM pkglist AS p NATURAL JOIN repos AS r "
							"WHERE %s LIKE '%%%q%%' AND ext NOT LIKE 'obsolete'", (gchar *) user_data, search);

	if ((sqlite3_prepare_v2(katja_pkgtools_db, query, -1, &stmt, NULL) == SQLITE_OK)) {
		/* Now we're ready to output all packages */
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			ret = katja_pkgtools_is_installed((gchar *) sqlite3_column_text(stmt, 2));
			if ((ret == PK_INFO_ENUM_INSTALLED) || (ret == PK_INFO_ENUM_UPDATING)) {
				pk_backend_job_package(job, PK_INFO_ENUM_INSTALLED,
										(gchar *) sqlite3_column_text(stmt, 0),
										(gchar *) sqlite3_column_text(stmt, 1));
			} else if (ret == PK_INFO_ENUM_INSTALLING) {
				pk_backend_job_package(job, PK_INFO_ENUM_AVAILABLE,
										(gchar *) sqlite3_column_text(stmt, 0),
										(gchar *) sqlite3_column_text(stmt, 1));
			}
		}
		sqlite3_finalize(stmt);
	} else {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(katja_pkgtools_db));
	}

	sqlite3_free(query);
	g_free(search);

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

static void pk_backend_search_files_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar **vals, *search;
	gchar *query;
	sqlite3_stmt *stmt;
	PkInfoEnum ret;

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage(job, 0);

	g_variant_get(params, "(t^a&s)", NULL, &vals);
	search = g_strjoinv("%", vals);

	query = sqlite3_mprintf("SELECT (p.name || ';' || p.ver || ';' || p.arch || ';' || r.repo), p.summary, "
							"p.full_name FROM filelist AS f NATURAL JOIN pkglist AS p NATURAL JOIN repos AS r "
							"WHERE f.filename LIKE '%%%q%%' GROUP BY f.full_name", search);

	if ((sqlite3_prepare_v2(katja_pkgtools_db, query, -1, &stmt, NULL) == SQLITE_OK)) {
		/* Now we're ready to output all packages */
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			ret = katja_pkgtools_is_installed((gchar *) sqlite3_column_text(stmt, 2));
			if ((ret == PK_INFO_ENUM_INSTALLED) || (ret == PK_INFO_ENUM_UPDATING)) {
				pk_backend_job_package(job, PK_INFO_ENUM_INSTALLED,
										(gchar *) sqlite3_column_text(stmt, 0),
										(gchar *) sqlite3_column_text(stmt, 1));
			} else if (ret == PK_INFO_ENUM_INSTALLING) {
				pk_backend_job_package(job, PK_INFO_ENUM_AVAILABLE,
										(gchar *) sqlite3_column_text(stmt, 0),
										(gchar *) sqlite3_column_text(stmt, 1));
			}
		}
		sqlite3_finalize(stmt);
	} else {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(katja_pkgtools_db));
	}
	sqlite3_free(query);
	g_free(search);

	pk_backend_job_set_percentage(job, 100);
	pk_backend_job_finished (job);
}

void pk_backend_search_files(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values) {
	pk_backend_job_thread_create(job, pk_backend_search_files_thread, NULL, NULL);
}

static void pk_backend_get_details_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar **pkg_ids, **pkg_tokens, *homepage = NULL;
	gsize i;
	GString *desc;
	GRegex *expr;
	GMatchInfo *match_info;
	GError *err = NULL;
	sqlite3_stmt *stmt = NULL;

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

	g_variant_get(params, "(^a&s)", &pkg_ids);

	if ((sqlite3_prepare_v2(katja_pkgtools_db,
							"SELECT p.desc, p.cat, p.uncompressed FROM pkglist AS p NATURAL JOIN repos AS r "
							"WHERE name LIKE @name AND r.repo LIKE @repo AND ext NOT LIKE 'obsolete'",
							-1,
							&stmt,
							NULL) != SQLITE_OK)) {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(katja_pkgtools_db));
		goto out;
	}

	pkg_tokens = pk_package_id_split(pkg_ids[0]);
	sqlite3_bind_text(stmt, 1, pkg_tokens[PK_PACKAGE_ID_NAME], -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, pkg_tokens[PK_PACKAGE_ID_DATA], -1, SQLITE_TRANSIENT);
	g_strfreev(pkg_tokens);

	if (sqlite3_step(stmt) != SQLITE_ROW)
		goto out;

	desc = g_string_new((gchar *) sqlite3_column_text(stmt, 0));

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
	pk_backend_job_details(job, pkg_ids[0], NULL,
						   NULL,
						   pk_group_enum_from_string((gchar *) sqlite3_column_text(stmt, 1)),
						   desc->str,
						   homepage,
						   sqlite3_column_int(stmt, 2));

	g_free(homepage);
	if (desc)
		g_string_free(desc, TRUE);

out:
	sqlite3_finalize(stmt);

	pk_backend_job_finished(job);
}

void pk_backend_get_details(PkBackend *backend, PkBackendJob *job, gchar **package_ids) {
	pk_backend_job_thread_create(job, pk_backend_get_details_thread, NULL, NULL);
}

static void pk_backend_resolve_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar **vals, **val;
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
		for (val = vals; *val; val++) {
			sqlite3_bind_text(statement, 1, *val, -1, SQLITE_TRANSIENT);

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

static void pk_backend_download_packages_thread(PkBackendJob *job, GVariant *params, gpointer user_data) {
	gchar *dir_path, *path, **pkg_ids, **pkg_tokens, *to_strv[] = {NULL, NULL};
	guint i;
	GSList *repo;
	sqlite3_stmt *pkglist_statement = NULL;

	g_variant_get(params, "(^a&ss)", &pkg_ids, &dir_path);
	pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);

	if ((sqlite3_prepare_v2(katja_pkgtools_db,
							"SELECT summary, (full_name || '.' || ext) FROM pkglist NATURAL JOIN repos "
							"WHERE name LIKE @name AND ver LIKE @ver AND arch LIKE @arch AND repo LIKE @repo",
							-1,
							&pkglist_statement,
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
			if ((repo = g_slist_find_custom(repos, pkg_tokens[PK_PACKAGE_ID_DATA], katja_pkgtools_cmp_repo))) {
				pk_backend_job_package(job, PK_INFO_ENUM_DOWNLOADING,
									   pkg_ids[i],
									   (gchar *) sqlite3_column_text(pkglist_statement, 0));
				katja_pkgtools_download(KATJA_PKGTOOLS(repo->data), dir_path, pkg_tokens[PK_PACKAGE_ID_NAME]);
				path = g_build_filename(dir_path, (gchar *) sqlite3_column_text(pkglist_statement, 1), NULL);
				to_strv[0] = path;
				pk_backend_job_files(job, NULL, to_strv);
				g_free(path);
			}
		}
		sqlite3_clear_bindings(pkglist_statement);
		sqlite3_reset(pkglist_statement);
		g_strfreev(pkg_tokens);
	}

out:
	sqlite3_finalize(pkglist_statement);

	pk_backend_job_finished (job);
}

void pk_backend_download_packages(PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory) {
	pk_backend_job_thread_create(job, pk_backend_download_packages_thread, NULL, NULL);
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

			/* If it isn't a collection */
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
	gchar *pkg_id, *full_name, *desc, **pkg_tokens;
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

			full_name = g_strdup((gchar *) sqlite3_column_text(statement, 0));

			if (g_strcmp0(pkg_metadata_filename, full_name)) {
				pkg_id = pk_package_id_build((gchar *) sqlite3_column_text(statement, 1),
											 (gchar *) sqlite3_column_text(statement, 2),
											 (gchar *) sqlite3_column_text(statement, 3),
											 (gchar *) sqlite3_column_text(statement, 4));
				desc = g_strdup((gchar *) sqlite3_column_text(statement, 5));

				pk_backend_job_package(job, PK_INFO_ENUM_NORMAL, pkg_id, desc);

				g_free(desc);
				g_free(pkg_id);
			}
			g_free(full_name);
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
	gchar *tmp_dir_name, *db_err, *path = NULL;
	gint ret;
	gboolean force;
	GSList *file_list = NULL, *l;
	GFile *db_file = NULL;
	GFileInfo *file_info = NULL;
	GError *err = NULL;
	CURL *curl = NULL;
	sqlite3_stmt *stmt = NULL;

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

	/* Force the complete cache refresh if the read configuration file is newer than the metadata cache */
	if (!force) {
		path = g_build_filename(LOCALSTATEDIR, "cache", "PackageKit", "metadata", "metadata.db", NULL);
		db_file = g_file_new_for_path(path);
		file_info = g_file_query_info(db_file, "time::modified-usec", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &err);
		if (err) {
			pk_backend_job_error_code(job, PK_ERROR_ENUM_NO_CACHE, "%s: %s", path, err->message);
			g_error_free(err);
			goto out;
		}
		ret = sqlite3_prepare_v2(katja_pkgtools_db,
								 "SELECT value FROM cache_info WHERE key LIKE 'last_modification'",
								 -1,
								 &stmt,
								 NULL);
		if ((ret != SQLITE_OK) || ((ret = sqlite3_step(stmt)) != SQLITE_ROW)) {
			pk_backend_job_error_code(job, PK_ERROR_ENUM_NO_CACHE, "%s: %s", path, sqlite3_errstr(ret));
			goto out;
		}
		if ((guint32) sqlite3_column_int(stmt, 0) > g_file_info_get_attribute_uint32(file_info, "time::modified-usec"))
			force = TRUE;
	}

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
	sqlite3_finalize(stmt);
	if (file_info)
		g_object_unref(file_info);
	if (db_file)
		g_object_unref(db_file);
	g_free(path);

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
