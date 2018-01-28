#ifndef __SLACK_JOB_H
#define __SLACK_JOB_H

#include <pk-backend.h>
#include <sqlite3.h>
#include "slack-utils.h"

#ifdef __cplusplus
namespace slack {

bool filter_package (PkBitfield filters, bool is_installed);

}
#endif

#ifdef __cplusplus
extern "C" {
#endif

void pk_backend_search_thread (PkBackendJob *job, GVariant *params, gpointer user_data);

#ifdef __cplusplus
}
#endif

#endif /* __SLACK_JOB_H */
