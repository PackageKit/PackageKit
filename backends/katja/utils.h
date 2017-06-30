#ifndef __KATJA_UTILS_H
#define __KATJA_UTILS_H

#include <string.h>
#include <curl/curl.h>
#include <glib/gstdio.h>
#include <string>
#include <pk-backend.h>
#include <pk-backend-job.h>
#include "binary.h"

CURLcode katja_get_file(CURL **curl, gchar *source_url, gchar *dest);
gchar **katja_cut_pkg(const gchar *pkg_filename);
PkInfoEnum katja_pkg_is_installed(gchar *pkg_full_name);

namespace katja
{

/**
 * Compare two repositories by the name.
 *
 * Returns: false if the names are equal, true otherwise.
 **/
struct CompareRepo final
{
public:
	CompareRepo(const gchar* name) noexcept;

	bool operator()(const Pkgtools* repo) const noexcept;

private:
	const gchar* name_;
};

}

#endif /* __KATJA_UTILS_H */
