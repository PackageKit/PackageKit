#include "pk-backend.h"
#include <pk-backend-job.h>

/* Define symbols used by libpk_backend_apt,
 * otherwise we can't link it.
 */

const gchar *
pk_backend_job_get_locale (PkBackendJob *job)
{
    return NULL;
}

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
pk_backend_job_repo_detail (PkBackendJob *job,
                            const gchar *repo_id,
                            const gchar *description,
                            gboolean enabled)
{
}

void
pk_backend_job_update_details (PkBackendJob *job,
                               GPtrArray *update_details)
{
}

void
pk_backend_job_packages (PkBackendJob   *job,
                         GPtrArray  *packages)
{
}

void
pk_backend_job_set_download_size_remaining (PkBackendJob *job,
                                            guint64 download_size_remaining)
{
}

void
pk_backend_job_error_code (PkBackendJob *job,
                           PkErrorEnum error_code,
                           const gchar *format, ...)
{
}

void
pk_backend_job_files (PkBackendJob *job,
                      const gchar *package_id,
                      gchar **files)
{
}

void
pk_backend_job_require_restart (PkBackendJob *job,
                             PkRestartEnum restart,
                             const gchar *package_id)
{
}

void
pk_backend_job_set_percentage (PkBackendJob *job,
                               guint percentage)
{
}

void
pk_backend_job_set_speed (PkBackendJob *job,
                          guint speed)
{
}

PkBitfield
pk_backend_job_get_transaction_flags (PkBackendJob *job)
{
    return 0;
}

void
pk_backend_job_package (PkBackendJob *job,
                        PkInfoEnum info,
                        const gchar *package_id,
                        const gchar *summary)
{
}

void
pk_backend_job_media_change_required (PkBackendJob *job,
                                      PkMediaTypeEnum media_type,
                                      const gchar *media_id,
                                      const gchar *media_text)
{
}

void
pk_backend_job_set_allow_cancel (PkBackendJob *job,
                                 gboolean allow_cancel)
{
}

gboolean
pk_backend_job_get_interactive (PkBackendJob *job)
{
        return FALSE;
}

void
pk_backend_job_set_status (PkBackendJob *job,
                           PkStatusEnum status)
{
}

PkRoleEnum
pk_backend_job_get_role (PkBackendJob *job)
{
    return PK_ROLE_ENUM_UNKNOWN;
}

const gchar *
pk_backend_job_get_proxy_ftp (PkBackendJob *job)
{
    return NULL;
}

void
pk_backend_job_set_item_progress (PkBackendJob *job,
                                  const gchar *package_id,
                                  PkStatusEnum status,
                                  guint percentage)
{

}

gpointer
pk_backend_job_get_backend (PkBackendJob *job)
{
    return NULL;
}

const gchar *
pk_backend_job_get_frontend_socket (PkBackendJob *job)
{
    return NULL;
}

gboolean
pk_backend_job_thread_create (PkBackendJob *job,
                              PkBackendJobThreadFunc func,
                              gpointer user_data,
                              GDestroyNotify destroy_func)
{
    return TRUE;
}

const gchar *
pk_backend_job_get_proxy_http (PkBackendJob *job)
{
    return NULL;
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

guint
pk_backend_job_get_uid (PkBackendJob *job)
{
    return 0;
}

gchar *
pk_backend_convert_uri (const gchar *proxy)
{
    return NULL;
}

GType
pk_backend_get_type ()
{
    return G_TYPE_RESERVED_USER_FIRST;
}

gboolean
pk_backend_is_online (PkBackend *backend)
{
    return TRUE;
}
