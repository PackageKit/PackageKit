#include <sqlite3.h>
#include <string.h>
#include "slack-utils.h"

/**
 * slack_get_file:
 * @curl: curl easy handle.
 * @source_url: source url.
 * @dest: destination.
 *
 * Download the file.
 *
 * Returns: CURLE_OK (zero) on success, non-zero otherwise.
 **/
CURLcode
slack_get_file(CURL **curl, gchar *source_url, gchar *dest)
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

/**
 * slack_split_package_name:
 * Got the name of a package, without version-arch-release data.
 **/
gchar **
slack_split_package_name(const gchar *pkg_filename)
{
	gchar *pkg_full_name, **pkg_tokens, **reversed_tokens;
	gint len;

	g_return_val_if_fail(pkg_filename != NULL, NULL);

	len = strlen(pkg_filename);
	if (len < 4)
	{
		return NULL;
	}

	if (pkg_filename[len - 4] == '.')
	{
		pkg_tokens = g_malloc_n(6, sizeof(gchar *));

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
		pkg_tokens = g_malloc_n(4, sizeof(gchar *));
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

/**
 * slack_is_installed:
 * Checks if a package is already installed in the system.
 *
 * Params:
 * 	pkg_fullname = Package name should be looked for.
 *
 * Returns: PK_INFO_ENUM_INSTALLING if pkg_fullname is already installed,
 *          PK_INFO_ENUM_UPDATING if an elder version of pkg_fullname is
 *          installed, PK_INFO_ENUM_UNKNOWN if pkg_fullname is malformed.
 **/
PkInfoEnum
slack_is_installed(const gchar *pkg_fullname)
{
	GFileEnumerator *pkg_metadata_enumerator;
	GFileInfo *pkg_metadata_file_info;
	GFile *pkg_metadata_dir;
	PkInfoEnum ret = PK_INFO_ENUM_INSTALLING;
    const gchar *it;
    guint8 dashes = 0;
	ptrdiff_t pkg_name;

	g_return_val_if_fail(pkg_fullname != NULL, PK_INFO_ENUM_UNKNOWN);

    // We want to find the package name without version for the package we're
    // looking for.
    g_debug("Looking if %s is installed", pkg_fullname);

    for (it = pkg_fullname + strlen(pkg_fullname); it != pkg_fullname; --it)
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
    pkg_name = it - pkg_fullname;

	// Read the package metadata directory and comprare all installed packages
    // with ones in the cache.
	pkg_metadata_dir = g_file_new_for_path("/var/log/packages");
	if (!(pkg_metadata_enumerator = g_file_enumerate_children(pkg_metadata_dir,
	                                                          "standard::name",
	                                                          G_FILE_QUERY_INFO_NONE,
	                                                          NULL,
	                                                          NULL)))
	{
		g_object_unref(pkg_metadata_dir);
		return PK_INFO_ENUM_UNKNOWN;
	}

	while ((pkg_metadata_file_info = g_file_enumerator_next_file(pkg_metadata_enumerator, NULL, NULL)))
	{
		const gchar *dir = g_file_info_get_name(pkg_metadata_file_info);
        dashes = 0;

        if (strcmp(dir, pkg_fullname) == 0)
        {
            ret = PK_INFO_ENUM_INSTALLED;
        }
		else
		{
			for (it = dir + strlen(dir); it != dir; --it)
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
			if (pkg_name == (it - dir) && strncmp(pkg_fullname, dir, pkg_name) == 0)
			{
				ret = PK_INFO_ENUM_UPDATING;
			}

		}
		g_object_unref(pkg_metadata_file_info);

		if (ret != PK_INFO_ENUM_INSTALLING) /* If installed */
		{
			break;
		}
	}
	g_object_unref(pkg_metadata_enumerator);
	g_object_unref(pkg_metadata_dir);

	return ret;
}

/**
 * slack_cmp_repo:
 **/
gint slack_cmp_repo(gconstpointer a, gconstpointer b)
{
	GValue value = G_VALUE_INIT;

	g_value_init(&value, G_TYPE_STRING);
	g_object_get_property(G_OBJECT(a), "name", &value);

	return g_strcmp0(g_value_get_string(&value), (gchar *) b);
}
