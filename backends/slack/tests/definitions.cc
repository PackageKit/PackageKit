#include "pk-backend.h"
#include <pk-backend-job.h>

gpointer
pk_backend_job_get_user_data (PkBackendJob *job)
{
	return NULL;
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
