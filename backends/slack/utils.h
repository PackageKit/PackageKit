#ifndef __SLACK_UTILS_H
#define __SLACK_UTILS_H

#include <curl/curl.h>
#include <pk-backend.h>
#include <pk-backend-job.h>

namespace slack {

struct JobData
{
	GObjectClass parent_class;

	sqlite3 *db;
	CURL *curl;
};

CURLcode get_file (CURL **curl, gchar *source_url, gchar *dest);

gchar **split_package_name (const gchar *pkg_filename);

PkInfoEnum is_installed (const gchar *pkg_fullname);

extern "C" {

gint cmp_repo (gconstpointer a, gconstpointer b);

}

}

#endif /* __SLACK_UTILS_H */
