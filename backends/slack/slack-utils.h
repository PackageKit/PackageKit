#ifndef __SLACK_UTILS_H
#define __SLACK_UTILS_H

#include <curl/curl.h>
#include <pk-backend.h>
#include <pk-backend-job.h>

typedef struct
{
	GObjectClass parent_class;

	sqlite3 *db;
	CURL *curl;
} JobData;

CURLcode slack_get_file(CURL **curl, gchar *source_url, gchar *dest);

gchar **slack_split_package_name(const gchar *pkg_filename);

PkInfoEnum slack_is_installed(const gchar *pkg_fullname);

gint slack_cmp_repo(gconstpointer a, gconstpointer b);

#endif /* __SLACK_UTILS_H */
