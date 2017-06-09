#ifndef __KATJA_UTILS_H
#define __KATJA_UTILS_H

#include <string.h>
#include <curl/curl.h>
#include <glib/gstdio.h>
#include <pk-backend.h>
#include <pk-backend-job.h>
#include "binary.h"

CURLcode katja_get_file(CURL **curl, gchar *source_url, gchar *dest);
gchar **katja_cut_pkg(const gchar *pkg_filename);
gint katja_cmp_repo(gconstpointer a, gconstpointer b);
PkInfoEnum katja_pkg_is_installed(gchar *pkg_full_name);

#endif /* __KATJA_UTILS_H */
