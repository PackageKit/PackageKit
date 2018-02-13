#include <curl/curl.h>
#include <sqlite3.h>
#include "pkgtools.h"
#include "utils.h"

/**
 * SlackPkgtools::download:
 * @job: A #PkBackendJob.
 * @dest_dir_name: Destination directory.
 * @pkg_name: Package name.
 *
 * Download a package.
 *
 * Returns: %TRUE on success, %FALSE otherwise.
 **/
gboolean
SlackPkgtools::download (PkBackendJob *job,
		gchar *dest_dir_name, gchar *pkg_name) noexcept
{
	gchar *dest_filename, *source_url;
	gboolean ret = FALSE;
	sqlite3_stmt *statement = NULL;
	CURL *curl = NULL;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	if ((sqlite3_prepare_v2(job_data->db,
							"SELECT location, (full_name || '.' || ext) FROM pkglist "
							"WHERE name LIKE @name AND repo_order = @repo_order",
							-1,
							&statement,
							NULL) != SQLITE_OK))
		return FALSE;

	sqlite3_bind_text(statement, 1, pkg_name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(statement, 2, this->get_order ());

	if (sqlite3_step(statement) == SQLITE_ROW)
	{
		dest_filename = g_build_filename(dest_dir_name, sqlite3_column_text(statement, 1), NULL);
		source_url = g_strconcat(this->get_mirror (),
								 sqlite3_column_text(statement, 0),
								 "/",
								 sqlite3_column_text(statement, 1),
								 NULL);

		if (!g_file_test(dest_filename, G_FILE_TEST_EXISTS))
		{
			if (slack_get_file(&curl, source_url, dest_filename) == CURLE_OK)
			{
				ret = TRUE;
			}
		}
		else
		{
			ret = TRUE;
		}

		if (curl)
		{
			curl_easy_cleanup(curl);
		}
		g_free(source_url);
		g_free(dest_filename);
	}
	sqlite3_finalize(statement);

	return ret;
}

/**
 * SlackPkgtools::install:
 * @job: A #PkBackendJob.
 * @pkg_name: Package name.
 *
 * Install a package.
 **/
void
SlackPkgtools::install (PkBackendJob *job, gchar *pkg_name) noexcept
{
	gchar *pkg_filename, *cmd_line;
	sqlite3_stmt *statement = NULL;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	if ((sqlite3_prepare_v2(job_data->db,
							"SELECT (full_name || '.' || ext) FROM pkglist "
							"WHERE name LIKE @name AND repo_order = @repo_order",
							-1,
							&statement,
							NULL) != SQLITE_OK))
	{
		return;
	}

	sqlite3_bind_text(statement, 1, pkg_name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(statement, 2, this->get_order ());

	if (sqlite3_step(statement) == SQLITE_ROW)
	{
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

SlackPkgtools::~SlackPkgtools () noexcept
{
}

/**
 * SlackPkgtools::get_name:
 *
 * Retrieves the repository name.
 *
 * Returns: Repository name.
 **/
const gchar *
SlackPkgtools::get_name () const noexcept
{
	return this->name;
}

/**
 * SlackPkgtools::get_mirror:
 *
 * Retrieves the repository mirror.
 *
 * Returns: Repository mirror.
 **/
const gchar *
SlackPkgtools::get_mirror () const noexcept
{
	return this->mirror;
}

/**
 * SlackPkgtools::get_order:
 *
 * Retrieves the repository order.
 *
 * Returns: Repository order.
 **/
guint8
SlackPkgtools::get_order () const noexcept
{
	return this->order;
}

/**
 * SlackPkgtools:is_blacklisted:
 * @pkg: Package name to check for.
 *
 * Checks whether a package is blacklisted.
 *
 * Returns: %TRUE if the package is blacklisted, %FALSE otherwise.
 **/
gboolean
SlackPkgtools::is_blacklisted (const gchar *pkg) const noexcept
{
	return this->blacklist
		&& g_regex_match (this->blacklist,
				pkg, static_cast<GRegexMatchFlags> (0), NULL);
}
