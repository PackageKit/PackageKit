#include "pk-backend.h"
#include <pk-backend-job.h>

gpointer
pk_backend_job_get_user_data (PkBackendJob *job)
{
	return NULL;
}

void
pk_backend_job_set_user_data (PkBackendJob *job, gpointer user_data)
{
}

void
pk_backend_job_set_allow_cancel (PkBackendJob *job, gboolean allow_cancel)
{
}

void
pk_backend_job_package (PkBackendJob *job,
			PkInfoEnum info,
			const gchar *package_id,
			const gchar *summary)
{
}

void
pk_backend_job_set_status (PkBackendJob *job, PkStatusEnum status)
{
}

void
pk_backend_job_set_percentage (PkBackendJob *job, guint percentage)
{
}

void
pk_backend_job_error_code (PkBackendJob *job,
		PkErrorEnum error_code, const gchar *format, ...)
{
}

void
pk_backend_job_files (PkBackendJob *job,
		const gchar *package_id, gchar **files)
{
}

void
pk_backend_job_details (PkBackendJob *job,
		const gchar *package_id,
		const gchar *summary,
		const gchar *license,
		PkGroupEnum group,
		const gchar *description,
		const gchar *url,
		gulong size)
{
}

void
pk_backend_job_update_detail (PkBackendJob *job,
		const gchar *package_id,
		gchar **updates,
		gchar **obsoletes,
		gchar **vendor_urls,
		gchar **bugzilla_urls,
		gchar **cve_urls,
		PkRestartEnum restart,
		const gchar *update_text,
		const gchar *changelog,
		PkUpdateStateEnum state,
		const gchar *issued,
		const gchar *updated)
{
}

gboolean
pk_backend_job_thread_create (PkBackendJob *job,
		PkBackendJobThreadFunc func,
		gpointer user_data,
		GDestroyNotify destroy_func)
{
	return FALSE;
}

gboolean pk_directory_remove_contents (const gchar *directory)
{
	return TRUE;
}
