#include <dirent.h>
#include <glib/gstdio.h>
#include <packagekit-glib2/pk-debug.h>
#include <stdlib.h>
#include <stdio.h>
#include <zlib.h>
#include <curl/curl.h>
#include <pk-backend.h>
#include <sqlite3.h>
#include "job.h"
#include "dl.h"
#include "pkgtools.h"
#include "slackpkg.h"
#include "slack-utils.h"

static GSList *repos = NULL;

void pk_backend_initialize(GKeyFile *conf, PkBackend *backend)
{
	gchar *path, **groups;
	gint ret;
	gushort i;
	gsize groups_len;
	GFile *conf_file;
	GFileInfo *file_info;
	GKeyFile *key_conf;
	GError *err = NULL;
	gpointer repo = NULL;
	sqlite3 *db;
	sqlite3_stmt *stmt;

	g_debug("backend: initialize");
	curl_global_init(CURL_GLOBAL_DEFAULT);

	/* Open the database. We will need it to save the time the configuration file was last modified. */
	path = g_build_filename(LOCALSTATEDIR, "cache", "PackageKit", "metadata", "metadata.db", NULL);
	if (sqlite3_open(path, &db) != SQLITE_OK)
	{
		g_error("%s: %s", path, sqlite3_errmsg(db));
	}
	g_free(path);

	/* Read the configuration file */
	key_conf = g_key_file_new();
	path = g_build_filename(SYSCONFDIR, "PackageKit", "Slackware.conf", NULL);
	g_key_file_load_from_file(key_conf, path, G_KEY_FILE_NONE, &err);
	if (err)
	{
		g_error("%s: %s", path, err->message);
		g_error_free(err);
	}

	conf_file = g_file_new_for_path(path);
	if (!(file_info = g_file_query_info(conf_file,
	                                    "time::modified-usec",
	                                    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
	                                    NULL,
	                                    &err)))
	{
		g_error("%s", err->message);
		g_error_free(err);
	}

	if ((ret = sqlite3_prepare_v2(db,
					"UPDATE cache_info SET value = ? WHERE key LIKE 'last_modification'",
					-1,
					&stmt,
					NULL)) == SQLITE_OK) {
		ret = sqlite3_bind_int(stmt, 1, g_file_info_get_attribute_uint32(file_info, "time::modified-usec"));
		if (ret == SQLITE_OK)
		{
			ret = sqlite3_step(stmt);
		}
		sqlite3_finalize(stmt);
	}
	if ((ret != SQLITE_OK) && (ret != SQLITE_DONE))
	{
		g_error("%s: %s", path, sqlite3_errstr(ret));
	}
	else if (!sqlite3_changes(db))
	{
		g_error("Failed to update database: %s", path);
	}

	g_object_unref(file_info);
	g_object_unref(conf_file);
	sqlite3_close_v2(db);
	g_free(path);

	/* Initialize an object for each well-formed repository */
	groups = g_key_file_get_groups(key_conf, &groups_len);
	for (i = 0; i < groups_len; i++)
	{
		gchar *blacklist = g_key_file_get_string(key_conf, groups[i], "Blacklist", NULL);
		gchar *mirror = g_key_file_get_string(key_conf, groups[i], "Mirror", NULL);

		if (g_key_file_has_key(key_conf, groups[i], "Priority", NULL))
		{
			repo = slack_slackpkg_new(groups[i],
			                          mirror,
			                          i + 1,
			                          blacklist,
			                          g_key_file_get_string_list(key_conf, groups[i], "Priority", NULL, NULL));
		}
		else if (g_key_file_has_key(key_conf, groups[i], "IndexFile", NULL))
		{
			repo = slack_dl_new(groups[i],
			                    mirror,
			                    i + 1,
			                    blacklist,
			                    g_key_file_get_string(key_conf, groups[i], "IndexFile", NULL));
		}

		if (repo)
		{
			repos = g_slist_append(repos, repo);
		}
		else
		{
			g_free(groups[i]);
		}
		g_free(mirror);
		g_free(blacklist);
	}
	g_free(groups);

	g_key_file_free(key_conf);
}

void
pk_backend_destroy(PkBackend *backend)
{
	g_debug("backend: destroy");

	g_slist_free_full(repos, g_object_unref);
	curl_global_cleanup();
}

gchar **
pk_backend_get_mime_types(PkBackend *backend)
{
	const gchar *mime_types[] = {
		"application/x-xz-compressed-tar",
		"application/x-compressed-tar",
		"application/x-bzip-compressed-tar",
		"application/x-lzma-compressed-tar",
		NULL
	};

	return g_strdupv((gchar **) mime_types);
}

gboolean
pk_backend_supports_parallelization(PkBackend *backend)
{
	return FALSE;
}

const gchar *
pk_backend_get_description(PkBackend* backend)
{
	return "Slackware";
}

const gchar *
pk_backend_get_author(PkBackend* backend)
{
	return "Eugene Wissner <belka@caraus.de>";
}

PkBitfield
pk_backend_get_groups(PkBackend *backend)
{
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

void
pk_backend_start_job(PkBackend *backend, PkBackendJob *job)
{
	gchar *db_filename = NULL;
	JobData *job_data = g_new0(JobData, 1);

	pk_backend_job_set_allow_cancel(job, TRUE);
	pk_backend_job_set_allow_cancel(job, FALSE);

	db_filename = g_build_filename(LOCALSTATEDIR, "cache", "PackageKit", "metadata", "metadata.db", NULL);
	if (sqlite3_open(db_filename, &job_data->db) == SQLITE_OK) { /* Some SQLite settings */
		sqlite3_exec(job_data->db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);
	}
	else
	{
		pk_backend_job_error_code(job, PK_ERROR_ENUM_NO_CACHE,
								  "%s: %s",
								  db_filename,
								  sqlite3_errmsg(job_data->db));
		goto out;
	}

	pk_backend_job_set_user_data(job, job_data);
	pk_backend_job_set_status(job, PK_STATUS_ENUM_RUNNING);

out:
	g_free(db_filename);
}

void
pk_backend_stop_job(PkBackend *backend, PkBackendJob *job)
{
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	if (job_data->curl)
	{
		curl_easy_cleanup(job_data->curl);
	}

	sqlite3_close(job_data->db);
	g_free(job_data);
	pk_backend_job_set_user_data(job, NULL);
}

void
pk_backend_search_names(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create(job, pk_backend_search_thread, (gpointer) "name", NULL);
}

void
pk_backend_search_details(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create(job, pk_backend_search_thread, (gpointer) "desc", NULL);
}

void
pk_backend_search_groups(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create(job, pk_backend_search_thread, (gpointer) "cat", NULL);
}

static void
pk_backend_search_files_thread(PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar **vals, *search;
	gchar *query;
	sqlite3_stmt *stmt;
	PkInfoEnum ret;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage(job, 0);

	g_variant_get(params, "(t^a&s)", NULL, &vals);
	search = g_strjoinv("%", vals);

	query = sqlite3_mprintf("SELECT (p.name || ';' || p.ver || ';' || p.arch || ';' || r.repo), p.summary, "
							"p.full_name FROM filelist AS f NATURAL JOIN pkglist AS p NATURAL JOIN repos AS r "
							"WHERE f.filename LIKE '%%%q%%' GROUP BY f.full_name", search);

	if ((sqlite3_prepare_v2(job_data->db, query, -1, &stmt, NULL) == SQLITE_OK))
	{
		/* Now we're ready to output all packages */
		while (sqlite3_step(stmt) == SQLITE_ROW)
		{
			ret = slack_is_installed((gchar*) sqlite3_column_text(stmt, 2));
			if ((ret == PK_INFO_ENUM_INSTALLED) || (ret == PK_INFO_ENUM_UPDATING))
			{
				pk_backend_job_package(job, PK_INFO_ENUM_INSTALLED,
				                       (gchar*) sqlite3_column_text(stmt, 0),
				                       (gchar*) sqlite3_column_text(stmt, 1));
			}
			else if (ret == PK_INFO_ENUM_INSTALLING)
			{
				pk_backend_job_package(job, PK_INFO_ENUM_AVAILABLE,
				                       (gchar*) sqlite3_column_text(stmt, 0),
				                       (gchar*) sqlite3_column_text(stmt, 1));
			}
		}
		sqlite3_finalize(stmt);
	}
	else
	{
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(job_data->db));
	}
	sqlite3_free(query);
	g_free(search);

	pk_backend_job_set_percentage(job, 100);
}

void
pk_backend_search_files(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create(job, pk_backend_search_files_thread, NULL, NULL);
}

static void
pk_backend_get_details_thread(PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar **pkg_ids, *homepage = NULL;
	gchar** tokens;
	gsize i;
	GString *desc;
	GRegex *expr;
	GMatchInfo *match_info;
	GError *err = NULL;
	sqlite3_stmt *stmt;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

	g_variant_get(params, "(^a&s)", &pkg_ids);

	if ((sqlite3_prepare_v2(job_data->db,
							"SELECT p.desc, p.cat, p.uncompressed FROM pkglist AS p NATURAL JOIN repos AS r "
							"WHERE name LIKE @name AND r.repo LIKE @repo AND ext NOT LIKE 'obsolete'",
							-1,
							&stmt,
							NULL) != SQLITE_OK)) {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(job_data->db));
		goto out;
	}

	tokens = pk_package_id_split(pkg_ids[0]);
	sqlite3_bind_text(stmt, 1, tokens[PK_PACKAGE_ID_NAME], -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, tokens[PK_PACKAGE_ID_DATA], -1, SQLITE_TRANSIENT);
	g_strfreev(tokens);

	if (sqlite3_step(stmt) != SQLITE_ROW)
		goto out;

	desc = g_string_new((gchar *) sqlite3_column_text(stmt, 0));

	/* Regular expression for searching a homepage */
	expr = g_regex_new("(?:http|ftp):\\/\\/[[:word:]\\/\\-\\.]+[[:word:]\\/](?=\\.?$)",
			(GRegexCompileFlags)(G_REGEX_OPTIMIZE | G_REGEX_DUPNAMES),
			(GRegexMatchFlags)(0),
			&err);
	if (err)
	{
		pk_backend_job_error_code(job, PK_ERROR_ENUM_UNKNOWN, "%s", err->message);
		g_error_free(err);
		goto out;
	}
	if (g_regex_match(expr, desc->str, (GRegexMatchFlags)0, &match_info))
	{
		homepage = g_match_info_fetch(match_info, 0); /* URL */
		/* Remove the last sentence with the copied URL */
		for (i = desc->len - 1; i > 0; i--)
		{
			if ((desc->str[i - 1] == '.') && (desc->str[i] == ' '))
			{
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
						   NULL,
						   pk_group_enum_from_string((gchar *) sqlite3_column_text(stmt, 1)),
						   desc->str,
						   homepage,
						   sqlite3_column_int(stmt, 2));

	g_free(homepage);
	if (desc)
	{
		g_string_free(desc, TRUE);
	}

out:
	sqlite3_finalize(stmt);
}

void
pk_backend_get_details(PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_thread_create(job, pk_backend_get_details_thread, NULL, NULL);
}

static void
pk_backend_resolve_thread(PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar **vals, **val;
	sqlite3_stmt *stmt;
	PkInfoEnum ret;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage(job, 0);

	g_variant_get(params, "(t^a&s)", NULL, &vals);

	if ((sqlite3_prepare_v2(job_data->db,
							"SELECT (p1.name || ';' || p1.ver || ';' || p1.arch || ';' || r.repo), p1.summary, "
						   	"p1.full_name FROM pkglist AS p1 NATURAL JOIN repos AS r "
							"WHERE p1.name LIKE @search AND p1.repo_order = "
							"(SELECT MIN(p2.repo_order) FROM pkglist AS p2 WHERE p2.name = p1.name GROUP BY p2.name)",
							-1,
							&stmt,
							NULL) == SQLITE_OK)) {
		/* Output packages matching each pattern */
		for (val = vals; *val; val++)
		{
			sqlite3_bind_text(stmt, 1, *val, -1, SQLITE_TRANSIENT);

			while (sqlite3_step(stmt) == SQLITE_ROW)
			{
				ret = slack_is_installed((gchar*) sqlite3_column_text(stmt, 2));
				if ((ret == PK_INFO_ENUM_INSTALLED) || (ret == PK_INFO_ENUM_UPDATING))
				{
					pk_backend_job_package(job, PK_INFO_ENUM_INSTALLED,
					                       (gchar*) sqlite3_column_text(stmt, 0),
					                       (gchar*) sqlite3_column_text(stmt, 1));
				}
				else if (ret == PK_INFO_ENUM_INSTALLING)
				{
					pk_backend_job_package(job, PK_INFO_ENUM_AVAILABLE,
					                       (gchar*) sqlite3_column_text(stmt, 0),
					                       (gchar*) sqlite3_column_text(stmt, 1));
				}
			}

			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
		}
		sqlite3_finalize(stmt);
	} else {
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(job_data->db));
	}

	pk_backend_job_set_percentage(job, 100);
}

void
pk_backend_resolve(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **packages)
{
	pk_backend_job_thread_create(job, pk_backend_resolve_thread, NULL, NULL);
}

static void
pk_backend_download_packages_thread(PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar *dir_path, *path, **pkg_ids, *to_strv[] = {NULL, NULL};
	guint i;
	sqlite3_stmt *stmt;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	g_variant_get(params, "(^a&ss)", &pkg_ids, &dir_path);
	pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);

	if ((sqlite3_prepare_v2(job_data->db,
							"SELECT summary, (full_name || '.' || ext) FROM pkglist NATURAL JOIN repos "
							"WHERE name LIKE @name AND ver LIKE @ver AND arch LIKE @arch AND repo LIKE @repo",
							-1,
							&stmt,
							NULL) != SQLITE_OK))
	{
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(job_data->db));
		goto out;
	}

	for (i = 0; pkg_ids[i]; ++i)
	{
		gchar **tokens = pk_package_id_split(pkg_ids[i]);

		sqlite3_bind_text(stmt, 1, tokens[PK_PACKAGE_ID_NAME], -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 2, tokens[PK_PACKAGE_ID_VERSION], -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 3, tokens[PK_PACKAGE_ID_ARCH], -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 4, tokens[PK_PACKAGE_ID_DATA], -1, SQLITE_TRANSIENT);
		if (sqlite3_step(stmt) == SQLITE_ROW)
		{
			GSList *repo;
			if ((repo = g_slist_find_custom(repos, tokens[PK_PACKAGE_ID_DATA], slack_cmp_repo)))
			{
				pk_backend_job_package(job, PK_INFO_ENUM_DOWNLOADING,
									   pkg_ids[i],
									   (gchar *) sqlite3_column_text(stmt, 0));
				SLACK_PKGTOOLS (repo->data)->download (job,
						dir_path, tokens[PK_PACKAGE_ID_NAME]);
				path = g_build_filename(dir_path, (gchar *) sqlite3_column_text(stmt, 1), NULL);
				to_strv[0] = path;
				pk_backend_job_files(job, NULL, to_strv);
				g_free(path);
			}
		}
		sqlite3_clear_bindings(stmt);
		sqlite3_reset(stmt);
		g_strfreev(tokens);
	}

out:
	sqlite3_finalize(stmt);
}

void
pk_backend_download_packages(PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory)
{
	pk_backend_job_thread_create(job, pk_backend_download_packages_thread, NULL, NULL);
}

static void
pk_backend_install_packages_thread(PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar *dest_dir_name;
	gchar **pkg_ids;
	guint i;
	gdouble percent_step;
	GSList *install_list = NULL, *l;
	sqlite3_stmt *pkglist_stmt = NULL, *collection_stmt = NULL;
    PkBitfield transaction_flags = 0;
	PkInfoEnum ret;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	g_variant_get(params, "(t^a&s)", &transaction_flags, &pkg_ids);
	pk_backend_job_set_status(job, PK_STATUS_ENUM_DEP_RESOLVE);

	if ((sqlite3_prepare_v2(job_data->db,
							"SELECT summary, cat FROM pkglist NATURAL JOIN repos "
							"WHERE name LIKE @name AND ver LIKE @ver AND arch LIKE @arch AND repo LIKE @repo",
							-1,
							&pkglist_stmt,
							NULL) != SQLITE_OK) ||
		(sqlite3_prepare_v2(job_data->db,
						   "SELECT (c.collection_pkg || ';' || p.ver || ';' || p.arch || ';' || r.repo), p.summary, "
						   "p.full_name, p.ext FROM collections AS c "
						   "JOIN pkglist AS p ON c.collection_pkg = p.name "
						   "JOIN repos AS r ON p.repo_order = r.repo_order "
						   "WHERE c.name LIKE @name AND r.repo LIKE @repo",
						   -1,
						   &collection_stmt,
						   NULL) != SQLITE_OK))
	{
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(job_data->db));
		goto out;
	}

	for (i = 0; pkg_ids[i]; i++)
	{
		gchar **tokens = pk_package_id_split(pkg_ids[i]);
		sqlite3_bind_text(pkglist_stmt, 1, tokens[PK_PACKAGE_ID_NAME], -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(pkglist_stmt, 2, tokens[PK_PACKAGE_ID_VERSION], -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(pkglist_stmt, 3, tokens[PK_PACKAGE_ID_ARCH], -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(pkglist_stmt, 4, tokens[PK_PACKAGE_ID_DATA], -1, SQLITE_TRANSIENT);

		if (sqlite3_step(pkglist_stmt) == SQLITE_ROW)
		{
			/* If it isn't a collection */
			if (g_strcmp0((gchar *) sqlite3_column_text(pkglist_stmt, 1), "collections"))
			{
				if (pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE))
				{
					pk_backend_job_package(job, PK_INFO_ENUM_INSTALLING,
										   pkg_ids[i],
										   (gchar *) sqlite3_column_text(pkglist_stmt, 0));
				}
				else
				{
					install_list = g_slist_append(install_list, g_strdup(pkg_ids[i]));
				}
			}
			else
			{
				sqlite3_bind_text(collection_stmt, 1, tokens[PK_PACKAGE_ID_NAME], -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(collection_stmt, 2, tokens[PK_PACKAGE_ID_DATA], -1, SQLITE_TRANSIENT);

				while (sqlite3_step(collection_stmt) == SQLITE_ROW)
				{
					ret = slack_is_installed((gchar*) sqlite3_column_text(collection_stmt, 2));
					if ((ret == PK_INFO_ENUM_INSTALLING) || (ret == PK_INFO_ENUM_UPDATING))
					{
						if ((pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) &&
							!g_strcmp0((gchar *) sqlite3_column_text(collection_stmt, 3), "obsolete"))
						{
							/* TODO: Don't just skip obsolete packages but remove them */
						}
						else if (pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE))
						{
							pk_backend_job_package(job, ret,
							                       (gchar *) sqlite3_column_text(collection_stmt, 0),
							                       (gchar *) sqlite3_column_text(collection_stmt, 1));
						}
						else
						{
							install_list = g_slist_append(install_list,
							                              g_strdup((gchar *) sqlite3_column_text(collection_stmt, 0)));
						}
					}
				}
				sqlite3_clear_bindings(collection_stmt);
				sqlite3_reset(collection_stmt);
			}
		}

		sqlite3_clear_bindings(pkglist_stmt);
		sqlite3_reset(pkglist_stmt);
		g_strfreev(tokens);
	}

	if (install_list && !pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE))
	{
		/* / 2 means total percentage for installing and for downloading */
		percent_step = 100.0 / g_slist_length(install_list) / 2;

		/* Download the packages */
		pk_backend_job_set_status(job, PK_STATUS_ENUM_DOWNLOAD);
		dest_dir_name = g_build_filename(LOCALSTATEDIR, "cache", "PackageKit", "downloads", NULL);
		for (l = install_list, i = 0; l; l = g_slist_next(l), i++)
		{
			gchar **tokens;
			GSList *repo;

			pk_backend_job_set_percentage(job, percent_step * i);
			tokens = pk_package_id_split((gchar *)(l->data));
			repo = g_slist_find_custom(repos, tokens[PK_PACKAGE_ID_DATA], slack_cmp_repo);

			if (repo)
			{
				SLACK_PKGTOOLS (repo->data)->download (job,
						dest_dir_name, tokens[PK_PACKAGE_ID_NAME]);
			}
			g_strfreev(tokens);
		}
		g_free(dest_dir_name);

		/* Install the packages */
		pk_backend_job_set_status(job, PK_STATUS_ENUM_INSTALL);
		for (l = install_list; l; l = g_slist_next(l), i++)
		{
			gchar **tokens;
			GSList *repo;

			pk_backend_job_set_percentage(job, percent_step * i);
			tokens = pk_package_id_split((gchar *)(l->data));
			repo = g_slist_find_custom(repos, tokens[PK_PACKAGE_ID_DATA], slack_cmp_repo);

			if (repo)
			{
				SLACK_PKGTOOLS (repo->data)->install (job, tokens[PK_PACKAGE_ID_NAME]);
			}
			g_strfreev(tokens);
		}
	}
	g_slist_free_full(install_list, g_free);

out:
	sqlite3_finalize(pkglist_stmt);
	sqlite3_finalize(collection_stmt);
}

void
pk_backend_install_packages(PkBackend *backend,
                            PkBackendJob *job,
                            PkBitfield transaction_flags,
                            gchar **package_ids)
{
	pk_backend_job_thread_create(job, pk_backend_install_packages_thread, NULL, NULL);
}

static void
pk_backend_remove_packages_thread(PkBackendJob* job, GVariant* params, gpointer user_data)
{
	gchar **pkg_ids, *cmd_line;
	guint i;
	gdouble percent_step;
    gboolean allow_deps, autoremove;
	GError *err = NULL;
    PkBitfield transaction_flags = 0;

	g_variant_get(params, "(t^a&sbb)", &transaction_flags, &pkg_ids, &allow_deps, &autoremove);
 
	if (pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE))
	{
		pk_backend_job_set_status(job, PK_STATUS_ENUM_DEP_RESOLVE);
	}
	else
	{
		pk_backend_job_set_status(job, PK_STATUS_ENUM_REMOVE);

		/* Add percent_step percents per removed package */
		percent_step = 100.0 / g_strv_length(pkg_ids);
		for (i = 0; pkg_ids[i]; i++)
		{
			gchar **tokens;

			pk_backend_job_set_percentage(job, percent_step * i);
			tokens = pk_package_id_split(pkg_ids[i]);
			cmd_line = g_strconcat("/sbin/removepkg ", tokens[PK_PACKAGE_ID_NAME], NULL);

			/* Pkgtools return always 0 */
			g_spawn_command_line_sync(cmd_line, NULL, NULL, NULL, &err);

			g_free(cmd_line);
			g_strfreev(tokens);

			if (err)
			{
				pk_backend_job_error_code(job, PK_ERROR_ENUM_PACKAGE_FAILED_TO_REMOVE, "%s", err->message);
				g_error_free(err);

				return;
			}

			pk_backend_job_set_percentage(job, 100);
		}
	}
}

void
pk_backend_remove_packages(PkBackend *backend,
                           PkBackendJob *job,
                           PkBitfield transaction_flags,
                           gchar **package_ids,
                           gboolean allow_deps,
                           gboolean autoremove)
{
	pk_backend_job_thread_create(job, pk_backend_remove_packages_thread, NULL, NULL);
}

static void
pk_backend_get_updates_thread(PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar *pkg_id, *full_name, *desc;
	const gchar *pkg_metadata_filename;
	GFile *pkg_metadata_dir;
	GFileEnumerator *pkg_metadata_enumerator;
	GFileInfo *pkg_metadata_file_info;
	GError *err = NULL;
	sqlite3_stmt *stmt;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

	if ((sqlite3_prepare_v2(job_data->db,
							"SELECT p1.full_name, p1.name, p1.ver, p1.arch, r.repo, p1.summary, p1.ext "
							"FROM pkglist AS p1 NATURAL JOIN repos AS r "
							"WHERE p1.name LIKE @name AND p1.repo_order = "
							"(SELECT MIN(p2.repo_order) FROM pkglist AS p2 WHERE p2.name = p1.name GROUP BY p2.name)",
							-1,
							&stmt,
							NULL) != SQLITE_OK))
	{
		pk_backend_job_error_code(job, PK_ERROR_ENUM_CANNOT_GET_FILELIST, "%s", sqlite3_errmsg(job_data->db));
		goto out;
	}

	/* Read the package metadata directory and comprare all installed packages with ones in the cache */
	pkg_metadata_dir = g_file_new_for_path("/var/log/packages");
	pkg_metadata_enumerator = g_file_enumerate_children(pkg_metadata_dir, "standard::name",
														 G_FILE_QUERY_INFO_NONE,
														 NULL,
														 &err);
	g_object_unref(pkg_metadata_dir);
	if (err)
	{
		pk_backend_job_error_code(job, PK_ERROR_ENUM_NO_CACHE, "/var/log/packages: %s", err->message);
		g_error_free(err);
		goto out;
	}

	while ((pkg_metadata_file_info = g_file_enumerator_next_file(pkg_metadata_enumerator, NULL, NULL)))
	{
		gchar **tokens;

		pkg_metadata_filename = g_file_info_get_name(pkg_metadata_file_info);
		tokens = slack_split_package_name(pkg_metadata_filename);

		/* Select the package from the database */
		sqlite3_bind_text(stmt, 1, tokens[0], -1, SQLITE_TRANSIENT);

		/* If there are more packages with the same name, remember the one from the
		 * repository with the lowest order. */
		if ((sqlite3_step(stmt) == SQLITE_ROW)
		 || g_slist_find_custom(repos, ((gchar *) sqlite3_column_text(stmt, 4)), slack_cmp_repo))
		{

			full_name = g_strdup((gchar *) sqlite3_column_text(stmt, 0));

			if (!g_strcmp0((gchar *) sqlite3_column_text(stmt, 6), "obsolete"))
			{ /* Remove if obsolete */
				pkg_id = pk_package_id_build(tokens[PK_PACKAGE_ID_NAME],
											 tokens[PK_PACKAGE_ID_VERSION],
											 tokens[PK_PACKAGE_ID_ARCH],
											 "obsolete");
				/* TODO:
				 * 1: Use the repository name instead of "obsolete" above and check in pk_backend_update_packages()
				      if the package is obsolete or not
				 * 2: Get description from /var/log/packages, not from the database */
				desc = g_strdup((gchar *) sqlite3_column_text(stmt, 5));

				pk_backend_job_package(job, PK_INFO_ENUM_REMOVING, pkg_id, desc);

				g_free(desc);
				g_free(pkg_id);
			}
			else if (g_strcmp0(pkg_metadata_filename, full_name))
			{ /* Update available */
				pkg_id = pk_package_id_build((gchar *) sqlite3_column_text(stmt, 1),
											 (gchar *) sqlite3_column_text(stmt, 2),
											 (gchar *) sqlite3_column_text(stmt, 3),
											 (gchar *) sqlite3_column_text(stmt, 4));
				desc = g_strdup((gchar *) sqlite3_column_text(stmt, 5));

				pk_backend_job_package(job, PK_INFO_ENUM_NORMAL, pkg_id, desc);

				g_free(desc);
				g_free(pkg_id);
			}
			g_free(full_name);
		}

		sqlite3_clear_bindings(stmt);
		sqlite3_reset(stmt);

		g_strfreev(tokens);
		g_object_unref(pkg_metadata_file_info);
	}
	g_object_unref(pkg_metadata_enumerator);

out:
	sqlite3_finalize(stmt);
}

void
pk_backend_get_updates(PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	pk_backend_job_thread_create(job, pk_backend_get_updates_thread, NULL, NULL);
}

static void
pk_backend_update_packages_thread(PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar *dest_dir_name, *cmd_line, **pkg_ids;
	guint i;
    PkBitfield transaction_flags = 0;

	g_variant_get(params, "(t^a&s)", &transaction_flags, &pkg_ids);

	if (!pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		pk_backend_job_set_status(job, PK_STATUS_ENUM_DOWNLOAD);

		/* Download the packages */
		dest_dir_name = g_build_filename(LOCALSTATEDIR, "cache", "PackageKit", "downloads", NULL);
		for (i = 0; pkg_ids[i]; i++)
		{
			gchar **tokens = pk_package_id_split(pkg_ids[i]);

			if (g_strcmp0(tokens[PK_PACKAGE_ID_DATA], "obsolete"))
			{
				GSList *repo = g_slist_find_custom(repos, tokens[PK_PACKAGE_ID_DATA], slack_cmp_repo);

				if (repo)
				{
					SLACK_PKGTOOLS (repo->data)->download (job,
							dest_dir_name, tokens[PK_PACKAGE_ID_NAME]);
				}
			}

			g_strfreev(tokens);
		}
		g_free(dest_dir_name);

		/* Install the packages */
		pk_backend_job_set_status(job, PK_STATUS_ENUM_UPDATE);
		for (i = 0; pkg_ids[i]; i++)
		{
			gchar **tokens = pk_package_id_split(pkg_ids[i]);

			if (g_strcmp0(tokens[PK_PACKAGE_ID_DATA], "obsolete"))
			{
				GSList *repo = g_slist_find_custom(repos, tokens[PK_PACKAGE_ID_DATA], slack_cmp_repo);

				if (repo)
				{
					SLACK_PKGTOOLS (repo->data)->download (job,
							dest_dir_name, tokens[PK_PACKAGE_ID_NAME]);
				}
			}
			else
			{
				/* Remove obsolete package
				 * TODO: Removing should be an independent operation (not during installing updates) */
				cmd_line = g_strconcat("/sbin/removepkg ", tokens[PK_PACKAGE_ID_NAME], NULL);
				g_spawn_command_line_sync(cmd_line, NULL, NULL, NULL, NULL);
				g_free(cmd_line);
			}
			g_strfreev(tokens);
		}
	}
}

void
pk_backend_update_packages(PkBackend *backend,
                           PkBackendJob *job,
                           PkBitfield transaction_flags,
                           gchar **package_ids)
{
	pk_backend_job_thread_create(job, pk_backend_update_packages_thread, NULL, NULL);
}

static void
pk_backend_refresh_cache_thread(PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar *tmp_dir_name, *db_err, *path = NULL;
	gint ret;
	gboolean force;
	GSList *file_list = NULL;
	GFile *db_file = NULL;
	GFileInfo *file_info = NULL;
	GError *err = NULL;
	sqlite3_stmt *stmt = NULL;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	pk_backend_job_set_status(job, PK_STATUS_ENUM_DOWNLOAD_CHANGELOG);

	/* Create temporary directory */
	tmp_dir_name = g_dir_make_tmp("PackageKit.XXXXXX", &err);
	if (!tmp_dir_name)
	{
		pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR, "%s", err->message);
		g_error_free(err);
		return;
	}

	g_variant_get(params, "(b)", &force);

	/* Force the complete cache refresh if the read configuration file is newer than the metadata cache */
	if (!force)
	{
		path = g_build_filename(LOCALSTATEDIR, "cache", "PackageKit", "metadata", "metadata.db", NULL);
		db_file = g_file_new_for_path(path);
		file_info = g_file_query_info(db_file, "time::modified-usec", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &err);
		if (err)
		{
			pk_backend_job_error_code(job, PK_ERROR_ENUM_NO_CACHE, "%s: %s", path, err->message);
			g_error_free(err);
			goto out;
		}
		ret = sqlite3_prepare_v2(job_data->db,
								 "SELECT value FROM cache_info WHERE key LIKE 'last_modification'",
								 -1,
								 &stmt,
								 NULL);
		if ((ret != SQLITE_OK) || ((ret = sqlite3_step(stmt)) != SQLITE_ROW))
		{
			pk_backend_job_error_code(job,
			                          PK_ERROR_ENUM_NO_CACHE,
			                          "%s: %s",
			                          path,
			                          sqlite3_errstr(ret));
			goto out;
		}
		if ((guint32) sqlite3_column_int(stmt, 0) > g_file_info_get_attribute_uint32(file_info, "time::modified-usec"))
		{
			force = TRUE;
		}
	}
	if (force) /* It should empty all tables */
	{
		if (sqlite3_exec(job_data->db, "DELETE FROM repos", NULL, 0, &db_err) != SQLITE_OK)
		{
			pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR, "%s", db_err);
			sqlite3_free(db_err);
			goto out;
		}
	}

	// Get list of files that should be downloaded.
	for (GSList *l = repos; l; l = g_slist_next(l))
	{
		file_list = g_slist_concat(file_list,
		                           slack_pkgtools_collect_cache_info(SLACK_PKGTOOLS(l->data), tmp_dir_name));
	}

	/* Download repository */
	pk_backend_job_set_status(job, PK_STATUS_ENUM_DOWNLOAD_REPOSITORY);

	for (GSList *l = file_list; l; l = g_slist_next(l))
	{
		slack_get_file(&job_data->curl,
		               ((gchar **) l->data)[0],
		               ((gchar **)l->data)[1]);
	}
	g_slist_free_full(file_list, (GDestroyNotify)g_strfreev);

	/* Refresh cache */
	pk_backend_job_set_status(job, PK_STATUS_ENUM_REFRESH_CACHE);

	for (GSList *l = repos; l; l = g_slist_next(l))
	{
		slack_pkgtools_generate_cache(SLACK_PKGTOOLS(l->data), job, tmp_dir_name);
	}

out:
	sqlite3_finalize(stmt);
	if (file_info)
	{
		g_object_unref(file_info);
	}
	if (db_file)
	{
		g_object_unref(db_file);
	}
	g_free(path);

	pk_directory_remove_contents(tmp_dir_name);
	g_rmdir(tmp_dir_name);
	g_free(tmp_dir_name);
}

void
pk_backend_refresh_cache(PkBackend *backend, PkBackendJob *job, gboolean force)
{
	pk_backend_job_thread_create(job, pk_backend_refresh_cache_thread, NULL, NULL);
}

static void
pk_backend_get_update_detail_thread(PkBackendJob *job, GVariant *params, gpointer user_data)
{
	guint i;
	gchar **pkg_ids;

	pk_backend_job_set_status(job, PK_STATUS_ENUM_QUERY);

	g_variant_get(params, "(^a&s)", &pkg_ids);

	for (i = 0; pkg_ids[i] != NULL; i++)
	{
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
}

void
pk_backend_get_update_detail(PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_thread_create(job, pk_backend_get_update_detail_thread, NULL, NULL);
}

PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
			PK_FILTER_ENUM_INSTALLED, PK_FILTER_ENUM_NOT_INSTALLED,
			PK_FILTER_ENUM_APPLICATION, PK_FILTER_ENUM_NOT_APPLICATION,
			-1);
}
