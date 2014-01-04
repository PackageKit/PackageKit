#include "katja-binary.h"

G_DEFINE_TYPE(KatjaBinary, katja_binary, KATJA_TYPE_PKGTOOLS);

/**
 * katja_binary_real_download:
 **/
gboolean katja_binary_real_download(KatjaPkgtools *pkgtools, gchar *dest_dir_name, gchar *pkg_name) {
	gchar *dest_filename, *source_url;
	gboolean ret = FALSE;
	sqlite3_stmt *statement = NULL;
	CURL *curl = NULL;

	if ((sqlite3_prepare_v2(katja_pkgtools_db,
							"SELECT location, (full_name || '.' || ext) FROM pkglist "
							"WHERE name LIKE @name AND repo_order = @repo_order",
							-1,
							&statement,
							NULL) != SQLITE_OK))
		return FALSE;

	sqlite3_bind_text(statement, 1, pkg_name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(statement, 2, pkgtools->order);

	if (sqlite3_step(statement) == SQLITE_ROW) {
		dest_filename = g_build_filename(dest_dir_name, sqlite3_column_text(statement, 1), NULL);
		source_url = g_strconcat(pkgtools->mirror->str,
								 sqlite3_column_text(statement, 0),
								 "/",
								 sqlite3_column_text(statement, 1),
								 NULL);

		if (!g_file_test(dest_filename, G_FILE_TEST_EXISTS)) {
			if (katja_pkgtools_get_file(&curl, source_url, dest_filename) == CURLE_OK) {
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
 * katja_binary_real_install:
 **/
void katja_binary_real_install(KatjaPkgtools *pkgtools, gchar *pkg_name) {
	gchar *pkg_filename, *cmd_line;
	sqlite3_stmt *statement = NULL;

	if ((sqlite3_prepare_v2(katja_pkgtools_db,
							"SELECT (full_name || '.' || ext) FROM pkglist "
							"WHERE name LIKE @name AND repo_order = @repo_order",
							-1,
							&statement,
							NULL) != SQLITE_OK))
		return;

	sqlite3_bind_text(statement, 1, pkg_name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(statement, 2, pkgtools->order);

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
 * katja_binary_finalize:
 **/
static void katja_binary_finalize(GObject *object) {
	G_OBJECT_CLASS(katja_binary_parent_class)->finalize(object);
}

/**
 * katja_binary_class_init:
 **/
static void katja_binary_class_init(KatjaBinaryClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	KatjaPkgtoolsClass *pkgtools_class = KATJA_PKGTOOLS_CLASS(klass);

	object_class->finalize = katja_binary_finalize;

	pkgtools_class->download = katja_binary_real_download;
	pkgtools_class->install = katja_binary_real_install;
}

/**
 * katja_binary_init:
 **/
static void katja_binary_init(KatjaBinary *binary) {
}
