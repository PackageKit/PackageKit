#include "katja-pkgtools.h"

G_DEFINE_TYPE(KatjaPkgtools, katja_pkgtools, G_TYPE_OBJECT);

/* Static public members */
/*KatjaPkgtoolsJobProgress katja_pkgtools_job_progress = {NULL, 0, 0, 0};*/


/**
 * katja_pkgtools_collect_cache_info:
 **/
GSList *katja_pkgtools_collect_cache_info(KatjaPkgtools *pkgtools, const gchar *tmpl) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), NULL);
	g_return_val_if_fail(KATJA_PKGTOOLS_GET_CLASS(pkgtools)->collect_cache_info != NULL, NULL);

	return KATJA_PKGTOOLS_GET_CLASS(pkgtools)->collect_cache_info(pkgtools, tmpl);
}

/**
 * katja_pkgtools_generate_cache:
 **/
void katja_pkgtools_generate_cache(KatjaPkgtools *pkgtools, const gchar *tmpl) {
	g_return_if_fail(KATJA_IS_PKGTOOLS(pkgtools));
	g_return_if_fail(KATJA_PKGTOOLS_GET_CLASS(pkgtools)->generate_cache != NULL);

	KATJA_PKGTOOLS_GET_CLASS(pkgtools)->generate_cache(pkgtools, tmpl);
}

/**
 * katja_pkgtools_download:
 **/
gboolean katja_pkgtools_download(KatjaPkgtools *pkgtools, gchar *dest_dir_name, gchar *pkg_name) {
	g_return_if_fail(KATJA_IS_PKGTOOLS(pkgtools));
	g_return_if_fail(KATJA_PKGTOOLS_GET_CLASS(pkgtools)->download != NULL);

	KATJA_PKGTOOLS_GET_CLASS(pkgtools)->download(pkgtools, dest_dir_name, pkg_name);
}

/**
 * katja_pkgtools_install:
 **/
void katja_pkgtools_install(KatjaPkgtools *pkgtools, gchar *pkg_name) {
	g_return_if_fail(KATJA_IS_PKGTOOLS(pkgtools));
	g_return_if_fail(KATJA_PKGTOOLS_GET_CLASS(pkgtools)->install != NULL);

	KATJA_PKGTOOLS_GET_CLASS(pkgtools)->install(pkgtools, pkg_name);
}

/**
 * katja_pkgtools_progress:
 *
static int katja_pkgtools_progress(void *p, double dltotal, double dlnow, double ultotal, double ulnow) {
	guint percentage;

	if (katja_pkgtools_job_progress.all <= 0) // Avoid division by 0
		return 0;

	percentage = (guint)((katja_pkgtools_job_progress.downloaded + dlnow) / (katja_pkgtools_job_progress.all / 100));
	if ((percentage > katja_pkgtools_job_progress.last_percentage) && (percentage <= 100)) {
		pk_backend_job_set_percentage(katja_pkgtools_job_progress.job, percentage);
		katja_pkgtools_job_progress.last_percentage = percentage;
	}

	return 0;
}*/

/**
 * katja_pkgtools_clean_dir:
 **/
void katja_pkgtools_clean_dir(GFile *dir, gboolean delete_parent) {
	GFile *dir_child;
	GFileEnumerator *enumerator;
	GFileInfo *file_info;

	g_return_val_if_fail(dir != NULL, NULL);

	/* Clean the directory if it exists */
	if (g_file_query_exists(dir, NULL)) {
		enumerator = g_file_enumerate_children(dir, "", G_FILE_QUERY_INFO_NONE, NULL, NULL);

		while ((file_info = g_file_enumerator_next_file(enumerator, NULL, NULL))) {
			dir_child = g_file_enumerator_get_child(enumerator, file_info);
			g_file_delete(dir_child, NULL, NULL);

			g_object_unref(dir_child);
			g_object_unref(file_info);
		}
		g_object_unref(enumerator);

		if (delete_parent)
			g_file_delete(dir, NULL, NULL);
	}
}

/**
 * katja_pkgtools_job_progress_init:
 **
void katja_pkgtools_job_progress_init(PkBackendJob *job) {
	katja_pkgtools_job_progress.job = job;
	katja_pkgtools_job_progress.last_percentage = 0;
	katja_pkgtools_job_progress.all = katja_pkgtools_job_progress.downloaded = 0;
}*/

/**
 * katja_pkgtools_get_file:
 **/
CURLcode katja_pkgtools_get_file(CURL **curl, gchar *source_url, gchar *dest) {
	gchar *dest_dir_name;
	FILE *fout = NULL;
	CURLcode ret;
	/*gdouble length_download;*/
	glong response_code;

	if ((*curl == NULL) && (!(*curl = curl_easy_init()))) return CURLE_BAD_FUNCTION_ARGUMENT;

	curl_easy_setopt(*curl, CURLOPT_URL, source_url);
	if (dest == NULL) {
		curl_easy_setopt(*curl, CURLOPT_NOBODY, 1L);
		curl_easy_setopt(*curl, CURLOPT_HEADER, 1L);
		ret = curl_easy_perform(*curl);
		curl_easy_getinfo(*curl, CURLINFO_RESPONSE_CODE, &response_code);
		if (response_code != 200)
			ret = CURLE_REMOTE_FILE_NOT_FOUND;
			/*curl_easy_getinfo(*curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, size);*/
	} else {
		if (g_file_test(dest, G_FILE_TEST_IS_DIR)) {
			dest_dir_name = dest;
			dest = g_strconcat(dest_dir_name, g_strrstr(source_url, "/"), NULL);
			g_free(dest_dir_name);
		}
		if ((fout = fopen(dest, "ab")) == NULL)
			return CURLE_WRITE_ERROR;

		/*curl_easy_setopt(*curl, CURLOPT_PROGRESSFUNCTION, katja_pkgtools_progress);
		curl_easy_setopt(*curl, CURLOPT_NOPROGRESS, 0L);*/
		curl_easy_setopt(*curl, CURLOPT_WRITEDATA, fout);
		ret = curl_easy_perform(*curl);
		/*if (!(ret = curl_easy_perform(*curl))) {
			curl_easy_getinfo(*curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &length_download);
			katja_pkgtools_job_progress.downloaded += length_download;
		}*/
	}

	curl_easy_reset(*curl);
	if (fout != NULL) fclose(fout);

	return ret;
}

/**
 * katja_pkgtools_cut_pkg:
 **/
gchar **katja_pkgtools_cut_pkg(const gchar *pkg_filename) {
	gchar *pkg_full_name, **pkg_tokens, **reversed_tokens;
	gint len;

	len = strlen(pkg_filename);
	if (pkg_filename[len - 4] == '.') {
		pkg_tokens = g_malloc_n(6, sizeof(gchar *));

		/* Full name without extension */
		len -= 4;
		pkg_full_name = g_strndup(pkg_filename, len);
		pkg_tokens[3] = g_strdup(pkg_full_name);

		/* The last 3 characters should be the file extension */
		pkg_tokens[4] = g_strdup(pkg_filename + len + 1);
		pkg_tokens[5] = NULL;
	} else {
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
 * katja_pkgtools_cmp_repo:
 **/
gint katja_pkgtools_cmp_repo(gconstpointer a, gconstpointer b) {
	return g_strcmp0(KATJA_PKGTOOLS(a)->name->str, (gchar *) b);
}

/**
 * katja_pkgtools_finalize:
 **/
static void katja_pkgtools_finalize(GObject *object) {
	KatjaPkgtools *pkgtools;

	g_return_if_fail(KATJA_IS_PKGTOOLS(object));

	pkgtools = KATJA_PKGTOOLS(object);
	if (pkgtools->name)
		g_string_free(pkgtools->name, TRUE);
	if (pkgtools->mirror)
		g_string_free(pkgtools->mirror, TRUE);
	if (pkgtools->blacklist)
		g_object_unref(pkgtools->blacklist);

	G_OBJECT_CLASS(katja_pkgtools_parent_class)->finalize(object);
}

PkInfoEnum katja_pkgtools_is_installed(gchar *pkg_full_name) {
	PkInfoEnum ret = PK_INFO_ENUM_INSTALLING;
	gchar **pkg_tokens, **metadata_pkg_tokens;
	const gchar *pkg_metadata_filename;
	GFile *pkg_metadata_dir;
	GFileEnumerator *pkg_metadata_enumerator;
	GFileInfo *pkg_metadata_file_info;

	g_return_val_if_fail(pkg_full_name != NULL, PK_INFO_ENUM_UNKNOWN);

	/* Read the package metadata directory and comprare all installed packages with ones in the cache */
	pkg_metadata_dir = g_file_new_for_path("/var/log/packages");
	if (!(pkg_metadata_enumerator = g_file_enumerate_children(pkg_metadata_dir, "standard::name",
														G_FILE_QUERY_INFO_NONE,
														NULL,
														NULL))) {
		g_object_unref(pkg_metadata_dir);
		return PK_INFO_ENUM_UNKNOWN;
	}

	pkg_tokens = katja_pkgtools_cut_pkg(pkg_full_name);

	while ((pkg_metadata_file_info = g_file_enumerator_next_file(pkg_metadata_enumerator, NULL, NULL))) {
		pkg_metadata_filename = g_file_info_get_name(pkg_metadata_file_info);
		metadata_pkg_tokens = katja_pkgtools_cut_pkg(pkg_metadata_filename);

		if (!g_strcmp0(pkg_metadata_filename, pkg_full_name))
			ret = PK_INFO_ENUM_INSTALLED;
		else if (!g_strcmp0(metadata_pkg_tokens[0], pkg_tokens[0]))
			ret = PK_INFO_ENUM_UPDATING;

		g_strfreev(metadata_pkg_tokens);
		g_object_unref(pkg_metadata_file_info);

		if (ret != PK_INFO_ENUM_INSTALLING) /* If installed */
			break;
	}

	g_strfreev(pkg_tokens);
	g_object_unref(pkg_metadata_enumerator);
	g_object_unref(pkg_metadata_dir);

	return ret;
}

/**
 * katja_pkgtools_class_init:
 **/
static void katja_pkgtools_class_init(KatjaPkgtoolsClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = katja_pkgtools_finalize;

	klass->collect_cache_info = NULL;
	klass->generate_cache = NULL;
	klass->download = NULL;
	klass->install = NULL;
}

/**
 * katja_pkgtools_init:
 **/
static void katja_pkgtools_init(KatjaPkgtools *pkgtools) {
	pkgtools->name = NULL;
	pkgtools->mirror = NULL;
	pkgtools->blacklist = NULL;
	pkgtools->order = 0;
}
