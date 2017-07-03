#include "utils.h"
#include <glibmm.h>

namespace katja
{

CURLcode
getFile(CURL** curl, gchar* source_url, gchar* dest)
{
	gchar *dest_dir_name;
	FILE *fout = NULL;
	CURLcode ret;
	glong response_code;

	if ((*curl == NULL) && (!(*curl = curl_easy_init())))
	{
		return CURLE_BAD_FUNCTION_ARGUMENT;
	}

	curl_easy_setopt(*curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(*curl, CURLOPT_URL, source_url);

	if (dest == NULL)
	{
		curl_easy_setopt(*curl, CURLOPT_NOBODY, 1L);
		curl_easy_setopt(*curl, CURLOPT_HEADER, 1L);
		ret = curl_easy_perform(*curl);
		curl_easy_getinfo(*curl, CURLINFO_RESPONSE_CODE, &response_code);

		if (response_code != 200)
		{
			ret = CURLE_REMOTE_FILE_NOT_FOUND;
		}
	}
	else
	{
		if (g_file_test(dest, G_FILE_TEST_IS_DIR))
		{
			dest_dir_name = dest;
			dest = g_strconcat(dest_dir_name, g_strrstr(source_url, "/"), NULL);
			g_free(dest_dir_name);
		}
		if ((fout = fopen(dest, "ab")) == NULL)
		{
			return CURLE_WRITE_ERROR;
		}
		curl_easy_setopt(*curl, CURLOPT_WRITEDATA, fout);
		ret = curl_easy_perform(*curl);
	}
	curl_easy_reset(*curl);
	if (fout != NULL)
	{
		fclose(fout);
	}
	return ret;
}

gchar**
splitPackageName(const gchar* pkg_filename)
{
	gchar *pkg_full_name, **pkg_tokens, **reversed_tokens;
	gint len;

	len = strlen(pkg_filename);
	if (pkg_filename[len - 4] == '.')
	{
		pkg_tokens = static_cast<gchar **>(g_malloc_n(6, sizeof(gchar *)));

		/* Full name without extension */
		len -= 4;
		pkg_full_name = g_strndup(pkg_filename, len);
		pkg_tokens[3] = g_strdup(pkg_full_name);

		/* The last 3 characters should be the file extension */
		pkg_tokens[4] = g_strdup(pkg_filename + len + 1);
		pkg_tokens[5] = NULL;
	}
	else
	{
		pkg_tokens = static_cast<gchar **>(g_malloc_n(4, sizeof(gchar *)));
		pkg_full_name = g_strdup(pkg_filename);
		pkg_tokens[3] = NULL;
	}

	/* Reverse all of the bytes in the package filename to get the name, version and the architecture */
	g_strreverse(pkg_full_name);
	reversed_tokens = g_strsplit(pkg_full_name, "-", 4);
	pkg_tokens[0] = g_strreverse(reversed_tokens[3]); /* Name */
	pkg_tokens[1] = g_strreverse(reversed_tokens[2]); /* Version */
	pkg_tokens[2] = g_strreverse(reversed_tokens[1]); /* Architecture */

	g_free(reversed_tokens[0]); /* Build number */
	g_free(reversed_tokens);
	g_free(pkg_full_name);

	return pkg_tokens;
}

PkInfoEnum
isInstalled(const std::string& pkgFullname)
{
	PkInfoEnum ret = PK_INFO_ENUM_INSTALLING;

    // We want to find the package name without version for the package we're
    // looking for.
    g_debug("Looking if %s is installed", pkgFullname.c_str());

    std::string::const_iterator it;
    unsigned short dashes = 0;

    for (it = pkgFullname.end(); it != pkgFullname.begin(); --it)
    {
        if (*it == '-')
        {
            if (dashes == 2)
            {
                break;
            }
            ++dashes;
        }
    }
	if (dashes < 2)
	{
		return PK_INFO_ENUM_UNKNOWN;
	}
    const std::string pkgName(pkgFullname.begin(), it);

	// Read the package metadata directory and comprare all installed packages
    // with ones in the cache.
	Glib::Dir metadataDir("/var/log/packages");
	for (const auto&& dir : metadataDir)
	{
        dashes = 0;
        if (dir == pkgFullname)
        {
            ret = PK_INFO_ENUM_INSTALLED;
            break;
        }

        for (it = dir.end(); it != dir.begin(); --it)
        {
            if (*it == '-')
            {
                if (dashes == 2)
                {
                    break;
                }
                ++dashes;
            }
        }
        if (pkgName == std::string(dir.begin(), it))
        {
            ret = PK_INFO_ENUM_UPDATING;
            break;
        }
	}

	return ret;
}

CompareRepo::CompareRepo(const gchar* name) noexcept
	: name_(name)
{
}

bool
CompareRepo::operator()(const Pkgtools* repo) const noexcept
{
		return *repo == name_;
}

}
