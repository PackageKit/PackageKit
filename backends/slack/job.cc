#include "job.h"

namespace slack {

/**
 * Returns true if the package isn't filtered out by the filters, false
 * otherwise.
 */
bool
filter_package (PkBitfield filters, bool is_installed)
{
	if ((is_installed && !pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED))
			|| (!is_installed && !pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)))
	{
		return true;
	}
	return false;
}

}

void
pk_backend_search_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	auto job_data = reinterpret_cast<JobData *> (pk_backend_job_get_user_data (job));

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	gchar **vals;
	PkBitfield filters;
	g_variant_get (params, "(t^a&s)", &filters, &vals);
	gchar *search = g_strjoinv ("%", vals);

	gchar *query = sqlite3_mprintf (
			"SELECT (p1.name || ';' || p1.ver || ';' || p1.arch || ';' || r.repo), p1.summary, "
			"p1.full_name FROM pkglist AS p1 NATURAL JOIN repos AS r "
			"WHERE p1.%s LIKE '%%%q%%' AND p1.ext NOT LIKE 'obsolete' AND p1.repo_order = "
			"(SELECT MIN(p2.repo_order) FROM pkglist AS p2 WHERE p2.name = p1.name GROUP BY p2.name)",
			user_data, search);

	sqlite3_stmt *stmt;
	if ((sqlite3_prepare_v2 (job_data->db, query, -1, &stmt, NULL) == SQLITE_OK))
	{
		/* Now we're ready to output all packages */
		while (sqlite3_step (stmt) == SQLITE_ROW)
		{
			PkInfoEnum info = slack_is_installed (
					reinterpret_cast<const gchar *> (sqlite3_column_text (stmt, 2)));

			if ((info == PK_INFO_ENUM_INSTALLED || info == PK_INFO_ENUM_UPDATING)
					&& slack::filter_package (filters, true))
			{
				pk_backend_job_package (job, PK_INFO_ENUM_INSTALLED,
						reinterpret_cast<const gchar *> (sqlite3_column_text (stmt, 0)),
				        reinterpret_cast<const gchar *> (sqlite3_column_text (stmt, 1)));
			}
			else if (info == PK_INFO_ENUM_INSTALLING && slack::filter_package (filters, false))
			{
				pk_backend_job_package(job, PK_INFO_ENUM_AVAILABLE,
						reinterpret_cast<const gchar *> (sqlite3_column_text (stmt, 0)),
						reinterpret_cast<const gchar *> (sqlite3_column_text (stmt, 1)));
			}
		}
		sqlite3_finalize (stmt);
	}
	else
	{
		pk_backend_job_error_code (job, PK_ERROR_ENUM_CANNOT_GET_FILELIST,
				"%s", sqlite3_errmsg (job_data->db));
	}

	sqlite3_free (query);
	g_free (search);

	pk_backend_job_set_percentage (job, 100);
}
