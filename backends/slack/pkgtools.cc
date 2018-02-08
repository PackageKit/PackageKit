#include <curl/curl.h>
#include <sqlite3.h>
#include "pkgtools.h"
#include "slack-utils.h"

G_DEFINE_INTERFACE(SlackPkgtools, slack_pkgtools, G_TYPE_OBJECT)

static void
slack_pkgtools_default_init(SlackPkgtoolsInterface *iface)
{
	GParamSpec *param;
	param = g_param_spec_string("name",
	                            "Name",
	                            "Repository name",
	                            NULL,
	                            static_cast<GParamFlags> (G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
	g_object_interface_install_property(iface, param);

	param = g_param_spec_string("mirror",
	                            "Mirror",
	                            "Repository mirror",
	                            NULL,
	                            static_cast<GParamFlags> (G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
	g_object_interface_install_property(iface, param);

	param = g_param_spec_uint("order",
	                          "Order",
	                          "Repository order",
	                          0,
	                          G_MAXUINT8,
	                          0,
	                          static_cast<GParamFlags> (G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
	g_object_interface_install_property(iface, param);
}

/**
 * slack_pkgtools_download:
 * @pkgtools: This class instance.
 * @job: A #PkBackendJob.
 * @dest_dir_name: Destination directory.
 * @pkg_name: Package name.
 *
 * Download a package.
 *
 * Returns: %TRUE on success, %FALSE otherwise.
 **/
gboolean
slack_pkgtools_download(SlackPkgtools *pkgtools,
                        PkBackendJob *job,
                        gchar *dest_dir_name,
                        gchar *pkg_name)
{
	gchar *dest_filename, *source_url;
	gboolean ret = FALSE;
	sqlite3_stmt *statement = NULL;
	CURL *curl = NULL;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));
	GValue order = G_VALUE_INIT;
	GValue mirror = G_VALUE_INIT;

	g_value_init(&order, G_TYPE_UINT);
	g_object_get_property(G_OBJECT(pkgtools), "order", &order);
	g_value_init(&mirror, G_TYPE_STRING);
	g_object_get_property(G_OBJECT(pkgtools), "mirror", &mirror);

	if ((sqlite3_prepare_v2(job_data->db,
							"SELECT location, (full_name || '.' || ext) FROM pkglist "
							"WHERE name LIKE @name AND repo_order = @repo_order",
							-1,
							&statement,
							NULL) != SQLITE_OK))
		return FALSE;

	sqlite3_bind_text(statement, 1, pkg_name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(statement, 2, g_value_get_uint(&order));

	if (sqlite3_step(statement) == SQLITE_ROW)
	{
		dest_filename = g_build_filename(dest_dir_name, sqlite3_column_text(statement, 1), NULL);
		source_url = g_strconcat(g_value_get_string(&mirror),
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
 * slack_pkgtools_install:
 * @pkgtools: This class instance.
 * @job: A #PkBackendJob.
 * @pkg_name: Package name.
 *
 * Install a package.
 **/
void
slack_pkgtools_install(SlackPkgtools *pkgtools,
                       PkBackendJob *job,
                       gchar *pkg_name)
{
	gchar *pkg_filename, *cmd_line;
	sqlite3_stmt *statement = NULL;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));
	GValue order = G_VALUE_INIT;

	g_value_init(&order, G_TYPE_UINT);
	g_object_get_property(G_OBJECT(pkgtools), "order", &order);

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
	sqlite3_bind_int(statement, 2, g_value_get_uint(&order));

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

/**
 * slack_pkgtools_collect_cache_info:
 * @pkgtools: This class instance.
 * @tmpl: temporary directory for downloading the files.
 *
 * Download files needed to get the information like the list of packages
 * in available repositories, updates, package descriptions and so on.
 *
 * Returns: List of files needed for building the cache.
 **/
GSList *
slack_pkgtools_collect_cache_info(SlackPkgtools *pkgtools, const gchar *tmpl)
{
	SlackPkgtoolsInterface *iface;

	g_return_val_if_fail(SLACK_IS_PKGTOOLS(pkgtools), NULL);
	g_return_val_if_fail(tmpl != NULL, NULL);

	iface = SLACK_PKGTOOLS_GET_IFACE(pkgtools);
	g_return_val_if_fail(iface->collect_cache_info != NULL, NULL);

	return iface->collect_cache_info(pkgtools, tmpl);
}

/**
 * slack_pkgtools_generate_cache:
 * @pkgtools: This class instance.
 * @tmpl: temporary directory for downloading the files.
 *
 * Download files needed to get the information like the list of packages
 * in available repositories, updates, package descriptions and so on.
 *
 * Returns: List of files needed for building the cache.
 **/
void
slack_pkgtools_generate_cache(SlackPkgtools *pkgtools,
                              PkBackendJob *job,
                              const gchar *tmpl)
{
	SlackPkgtoolsInterface *iface;

	g_return_if_fail(SLACK_IS_PKGTOOLS(pkgtools));
	g_return_if_fail(job != NULL);
	g_return_if_fail(tmpl != NULL);

	iface = SLACK_PKGTOOLS_GET_IFACE(pkgtools);
	g_return_if_fail(iface->generate_cache != NULL);

	iface->generate_cache(pkgtools, job, tmpl);
}

/**
 * SlackPkgtools::get_name:
 *
 * Retrieves the repository name.
 *
 * Returns: Repository name.
 **/
const gchar *
SlackPkgtools::get_name () noexcept
{
	GValue name = G_VALUE_INIT;

	g_value_init (&name, G_TYPE_STRING);
	g_object_get_property (G_OBJECT (this), "name", &name);

	return g_value_get_string (&name);
}

/**
 * SlackPkgtools::get_mirror:
 *
 * Retrieves the repository mirror.
 *
 * Returns: Repository mirror.
 **/
const gchar *
SlackPkgtools::get_mirror () noexcept
{
	GValue mirror = G_VALUE_INIT;

	g_value_init (&mirror, G_TYPE_STRING);
	g_object_get_property (G_OBJECT (this), "mirror", &mirror);

	return g_value_get_string(&mirror);
}
