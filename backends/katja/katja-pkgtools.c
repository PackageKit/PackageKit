#include <stdlib.h>
#include <bzlib.h>
#include "katja-pkgtools.h"

G_DEFINE_TYPE(KatjaPkgtools, katja_pkgtools, G_TYPE_OBJECT);

/**
 * katja_pkgtools_get_name:
 **/
gchar *katja_pkgtools_get_name(KatjaPkgtools *pkgtools) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), NULL);

	return KATJA_PKGTOOLS_GET_CLASS(pkgtools)->get_name(pkgtools);
}

/**
 * katja_pkgtools_get_mirror:
 **/
gchar *katja_pkgtools_get_mirror(KatjaPkgtools *pkgtools) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), NULL);

	return KATJA_PKGTOOLS_GET_CLASS(pkgtools)->get_mirror(pkgtools);
}

/**
 * katja_pkgtools_get_order:
 **/
gushort katja_pkgtools_get_order(KatjaPkgtools *pkgtools) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), 0);

	return KATJA_PKGTOOLS_GET_CLASS(pkgtools)->get_order(pkgtools);
}

/**
 * katja_pkgtools_get_blacklist:
 **/
GRegex *katja_pkgtools_get_blacklist(KatjaPkgtools *pkgtools) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), NULL);

	return KATJA_PKGTOOLS_GET_CLASS(pkgtools)->get_blacklist(pkgtools);
}

/**
 * katja_pkgtools_collect_cache_info:
 **/
GSList *katja_pkgtools_collect_cache_info(KatjaPkgtools *pkgtools, const gchar *tmpl) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), NULL);
	g_return_val_if_fail(KATJA_PKGTOOLS_GET_CLASS(pkgtools)->collect_cache_info != NULL, NULL);

	return KATJA_PKGTOOLS_GET_CLASS(pkgtools)->collect_cache_info(pkgtools, tmpl);
}

/**
 * katja_pkgtools_generate_cache:
 **/
void katja_pkgtools_generate_cache(KatjaPkgtools *pkgtools, PkBackendJob *job, const gchar *tmpl) {
	g_return_if_fail(KATJA_IS_PKGTOOLS(pkgtools));
	g_return_if_fail(KATJA_PKGTOOLS_GET_CLASS(pkgtools)->generate_cache != NULL);

	KATJA_PKGTOOLS_GET_CLASS(pkgtools)->generate_cache(pkgtools, job, tmpl);
}

/**
 * katja_pkgtools_download:
 **/
gboolean katja_pkgtools_download(KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *dest_dir_name, gchar *pkg_name) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), FALSE);
	g_return_val_if_fail(KATJA_PKGTOOLS_GET_CLASS(pkgtools)->download != NULL, FALSE);

	return KATJA_PKGTOOLS_GET_CLASS(pkgtools)->download(pkgtools, job, dest_dir_name, pkg_name);
}

/**
 * katja_pkgtools_install:
 **/
void katja_pkgtools_install(KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *pkg_name) {
	g_return_if_fail(KATJA_IS_PKGTOOLS(pkgtools));
	g_return_if_fail(KATJA_PKGTOOLS_GET_CLASS(pkgtools)->install != NULL);

	KATJA_PKGTOOLS_GET_CLASS(pkgtools)->install(pkgtools, job, pkg_name);
}

/**
 * katja_pkgtools_real_get_name:
 **/
gchar *katja_pkgtools_real_get_name(KatjaPkgtools *pkgtools) {
	return KATJA_PKGTOOLS(pkgtools)->name;
}

/**
 * katja_pkgtools_real_get_mirror:
 **/
gchar *katja_pkgtools_real_get_mirror(KatjaPkgtools *pkgtools) {
	return KATJA_PKGTOOLS(pkgtools)->mirror;
}

/**
 * katja_pkgtools_real_get_order:
 **/
gushort katja_pkgtools_real_get_order(KatjaPkgtools *pkgtools) {
	return KATJA_PKGTOOLS(pkgtools)->order;
}

/**
 * katja_pkgtools_real_get_blacklist:
 **/
GRegex *katja_pkgtools_real_get_blacklist(KatjaPkgtools *pkgtools) {
	return KATJA_PKGTOOLS(pkgtools)->blacklist;
}

/**
 * katja_pkgtools_real_download:
 **/
gboolean katja_pkgtools_real_download(KatjaPkgtools *pkgtools, PkBackendJob *job,
									gchar *dest_dir_name,
									gchar *pkg_name) {
	gchar *dest_filename, *source_url;
	gboolean ret = FALSE;
	sqlite3_stmt *statement = NULL;
	CURL *curl = NULL;
	PkBackendKatjaJobData *job_data = pk_backend_job_get_user_data(job);

	if ((sqlite3_prepare_v2(job_data->db,
							"SELECT location, (full_name || '.' || ext) FROM pkglist "
							"WHERE name LIKE @name AND repo_order = @repo_order",
							-1,
							&statement,
							NULL) != SQLITE_OK))
		return FALSE;

	sqlite3_bind_text(statement, 1, pkg_name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(statement, 2, katja_pkgtools_get_order(pkgtools));

	if (sqlite3_step(statement) == SQLITE_ROW) {
		dest_filename = g_build_filename(dest_dir_name, sqlite3_column_text(statement, 1), NULL);
		source_url = g_strconcat(katja_pkgtools_get_mirror(pkgtools),
								 sqlite3_column_text(statement, 0),
								 "/",
								 sqlite3_column_text(statement, 1),
								 NULL);

		if (!g_file_test(dest_filename, G_FILE_TEST_EXISTS)) {
			if (katja_get_file(&curl, source_url, dest_filename) == CURLE_OK) {
				ret = TRUE;
			}
		} else {
			ret = TRUE;
		}

		if (curl)
			curl_easy_cleanup(curl);

		g_free(source_url);
		g_free(dest_filename);
	}
	sqlite3_finalize(statement);

	return ret;
}

/**
 * katja_pkgtools_real_install:
 **/
void katja_pkgtools_real_install(KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *pkg_name) {
	gchar *pkg_filename, *cmd_line;
	sqlite3_stmt *statement = NULL;
	PkBackendKatjaJobData *job_data = pk_backend_job_get_user_data(job);

	if ((sqlite3_prepare_v2(job_data->db,
							"SELECT (full_name || '.' || ext) FROM pkglist "
							"WHERE name LIKE @name AND repo_order = @repo_order",
							-1,
							&statement,
							NULL) != SQLITE_OK))
		return;

	sqlite3_bind_text(statement, 1, pkg_name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(statement, 2, katja_pkgtools_get_order(pkgtools));

	if (sqlite3_step(statement) == SQLITE_ROW) {
		pkg_filename = g_build_filename(LOCALSTATEDIR,
								   "cache",
								   "PackageKit",
								   "downloads",
								   sqlite3_column_text(statement, 0),
								   NULL);
		cmd_line = g_strconcat("/sbin/upgradepkg --install-new ", pkg_filename, NULL);
		g_spawn_command_line_sync(cmd_line, NULL, NULL, NULL, NULL);
		g_free(cmd_line);

		g_free(pkg_filename);
	}
	sqlite3_finalize(statement);
}

/**
 * katja_pkgtools_manifest:
 **/
void katja_pkgtools_manifest(KatjaPkgtools *pkgtools, PkBackendJob *job, const gchar *tmpl, gchar *filename) {
	FILE *manifest;
	gint err, read_len;
	guint pos;
	gchar buf[KATJA_PKGTOOLS_MAX_BUF_SIZE], *path, *full_name = NULL, *pkg_filename, *rest = NULL, *start;
	gchar **line, **lines;
	BZFILE *manifest_bz2;
	GRegex *pkg_expr = NULL, *file_expr = NULL;
	GMatchInfo *match_info;
	sqlite3_stmt *statement = NULL;
	PkBackendKatjaJobData *job_data = pk_backend_job_get_user_data(job);

	path = g_build_filename(tmpl, pkgtools->name, filename, NULL);
	manifest = fopen(path, "rb");
	g_free(path);

	if (!manifest)
		return;
	if (!(manifest_bz2 = BZ2_bzReadOpen(&err, manifest, 0, 0, NULL, 0)))
		goto out;

	/* Prepare regular expressions */
	if (!(pkg_expr = g_regex_new("^\\|\\|[[:blank:]]+Package:[[:blank:]]+.+\\/(.+)\\.(t[blxg]z$)?",
								 G_REGEX_OPTIMIZE | G_REGEX_DUPNAMES,
								 0,
								 NULL)) ||
		!(file_expr = g_regex_new("^[-bcdlps][-r][-w][-xsS][-r][-w][-xsS][-r][-w][-xtT][[:space:]][^[:space:]]+"
								  "[[:space:]]+[[:digit:]]+[[:space:]][[:digit:]-]+[[:space:]][[:digit:]:]+[[:space:]]"
								  "(?!install\\/|\\.)(.*)",
								 G_REGEX_OPTIMIZE | G_REGEX_DUPNAMES,
								 0,
								 NULL)))
		goto out;

	/* Prepare SQL statements */
	if (sqlite3_prepare_v2(job_data->db,
						   "INSERT INTO filelist (full_name, filename) VALUES (@full_name, @filename)",
						   -1,
						   &statement,
						   NULL) != SQLITE_OK)
		goto out;

	sqlite3_exec(job_data->db, "BEGIN TRANSACTION", NULL, NULL, NULL);
	while ((read_len = BZ2_bzRead(&err, manifest_bz2, buf, KATJA_PKGTOOLS_MAX_BUF_SIZE))) {
		if ((err != BZ_OK) && (err != BZ_STREAM_END))
			break;

		/* Split the read text into lines */
		lines = g_strsplit(buf, "\n", 0);
		if (rest) { /* Add to the first line rest characters from the previous read operation */
			start = lines[0];
			lines[0] = g_strconcat(rest, lines[0], NULL);
			g_free(start);
			g_free(rest);
		}
		if (err != BZ_STREAM_END) { /* The last line can be incomplete */
			pos = g_strv_length(lines) - 1;
			rest = lines[pos];
			lines[pos] = NULL;
		}
		for (line = lines; *line; line++) {
			if (g_regex_match(pkg_expr, *line, 0, &match_info)) {
				if (g_match_info_get_match_count(match_info) > 2) { /* If the extension matches */
					g_free(full_name);
					full_name = g_match_info_fetch(match_info, 1);
				} else {
					full_name = NULL;
				}
			}
			g_match_info_free(match_info);

			match_info = NULL;
			if (full_name && g_regex_match(file_expr, *line, 0, &match_info)) {
				pkg_filename = g_match_info_fetch(match_info, 1);
				sqlite3_bind_text(statement, 1, full_name, -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(statement, 2, pkg_filename, -1, SQLITE_TRANSIENT);
				sqlite3_step(statement);
				sqlite3_clear_bindings(statement);
				sqlite3_reset(statement);
				g_free(pkg_filename);
			}
			g_match_info_free(match_info);
		}
		g_strfreev(lines);
	}

	sqlite3_exec(job_data->db, "END TRANSACTION", NULL, NULL, NULL);
	g_free(full_name);
	BZ2_bzReadClose(&err, manifest_bz2);

out:
	sqlite3_finalize(statement);
	if (file_expr)
		g_regex_unref(file_expr);
	if (pkg_expr)
		g_regex_unref(pkg_expr);

	fclose(manifest);
}

/**
 * katja_pkgtools_init:
 **/
static void katja_pkgtools_init(KatjaPkgtools *pkgtools) {
	pkgtools->name = NULL;
	pkgtools->mirror = NULL;
	pkgtools->blacklist = NULL;
	pkgtools->order = 0;
}

/**
 * katja_pkgtools_class_init:
 **/
static void katja_pkgtools_class_init(KatjaPkgtoolsClass *klass)
{
	klass->get_name = katja_pkgtools_real_get_name;
	klass->get_mirror = katja_pkgtools_real_get_mirror;
	klass->get_order = katja_pkgtools_real_get_order;
	klass->get_blacklist = katja_pkgtools_real_get_blacklist;
	klass->collect_cache_info = (GSList *(*)(KatjaPkgtools *, const gchar *)) katja_pkgtools_collect_cache_info;
	klass->generate_cache = (void (*)(KatjaPkgtools *, PkBackendJob *, const gchar *)) katja_pkgtools_generate_cache;
	klass->download = katja_pkgtools_real_download;
	klass->install = katja_pkgtools_real_install;
}
