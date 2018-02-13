#ifndef __SLACK_JOB_H
#define __SLACK_JOB_H

#include <pk-backend.h>
#include <sqlite3.h>

namespace slack {

bool filter_package (PkBitfield filters, bool is_installed);

}

extern "C" {

void pk_backend_search_thread (PkBackendJob *job, GVariant *params, gpointer user_data);

}

#endif /* __SLACK_JOB_H */
