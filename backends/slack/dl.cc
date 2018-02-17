#include <sqlite3.h>
#include <stdlib.h>
#include "dl.h"
#include "utils.h"

namespace slack {

/**
 * slack::Dl::collect_cache_info:
 * @tmpl: temporary directory for downloading the files.
 *
 * Download files needed to get the information like the list of packages
 * in available repositories, updates, package descriptions and so on.
 *
 * Returns: List of files needed for building the cache.
 **/
GSList *
Dl::collect_cache_info (const gchar *tmpl) noexcept
{
	CURL *curl = NULL;
	GSList *file_list = NULL;
	GFile *tmp_dir, *repo_tmp_dir;

	/* Create the temporary directory for the repository */
	tmp_dir = g_file_new_for_path(tmpl);
	repo_tmp_dir = g_file_get_child(tmp_dir, this->get_name ());
	g_file_make_directory(repo_tmp_dir, NULL, NULL);

	/* There is no ChangeLog yet to check if there are updates or not. Just mark the index file for download */
	auto source_dest = static_cast<gchar **> (g_malloc_n(3, sizeof(gchar *)));
	source_dest[0] = g_strdup(this->index_file);
	source_dest[1] = g_build_filename(tmpl,
	                                  this->get_name (),
	                                  "IndexFile",
	                                  NULL);
	source_dest[2] = NULL;
	/* Check if the remote file can be found */
	if (get_file(&curl, source_dest[0], NULL))
	{
		g_strfreev(source_dest);
	}
	else
	{
		file_list = g_slist_append(file_list, source_dest);
	}
	g_object_unref(repo_tmp_dir);
	g_object_unref(tmp_dir);

	if (curl)
	{
		curl_easy_cleanup(curl);
	}
	return file_list;
}

/**
 * slack::Dl::generate_cache:
 * @job: A #PkBackendJob.
 * @tmpl: temporary directory for downloading the files.
 *
 * Download files needed to get the information like the list of packages
 * in available repositories, updates, package descriptions and so on.
 *
 * Returns: List of files needed for building the cache.
 **/
void
Dl::generate_cache(PkBackendJob *job, const gchar *tmpl) noexcept
{
	gchar **line_tokens, **pkg_tokens, *line, *collection_name = NULL, *list_filename;
	gboolean skip = FALSE;
	GFile *list_file;
	GFileInputStream *fin;
	GDataInputStream *data_in = NULL;
	sqlite3_stmt *stmt = NULL;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	/* Check if the temporary directory for this repository exists. If so the file metadata have to be generated */
	list_filename = g_build_filename(tmpl,
	                                 this->get_name (),
	                                 "IndexFile",
	                                 NULL);
	list_file = g_file_new_for_path(list_filename);
	if (!(fin = g_file_read(list_file, NULL, NULL)))
	{
		goto out;
	}
	data_in = g_data_input_stream_new(G_INPUT_STREAM(fin));

	/* Remove the old entries from this repository */
	if (sqlite3_prepare_v2(job_data->db,
						   "DELETE FROM repos WHERE repo LIKE @repo",
						   -1,
						   &stmt,
						   NULL) == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, this->get_name (), -1, SQLITE_TRANSIENT);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}
	if (sqlite3_prepare_v2(job_data->db,
	                       "INSERT INTO repos (repo_order, repo) VALUES (@repo_order, @repo)",
	                       -1,
	                       &stmt,
	                       NULL) != SQLITE_OK)
	{
		goto out;
	}
	sqlite3_bind_int(stmt, 1, this->get_order ());
	sqlite3_bind_text(stmt, 2, this->get_name (), -1, SQLITE_TRANSIENT);
	sqlite3_step(stmt);
	if (sqlite3_finalize(stmt) != SQLITE_OK)
	{
		goto out;
	}

	/* Insert new records */
	if ((sqlite3_prepare_v2(job_data->db,
	                        "INSERT INTO pkglist (full_name, name, ver, arch, "
	                        "summary, desc, compressed, uncompressed, cat, repo_order, ext) "
	                        "VALUES (@full_name, @name, @ver, @arch, @summary, "
	                        "@desc, @compressed, @uncompressed, @cat, @repo_order, @ext)",
	                        -1,
	                        &stmt,
	                        NULL) != SQLITE_OK))
	{
		goto out;
	}
	sqlite3_exec(job_data->db, "BEGIN TRANSACTION", NULL, NULL, NULL);

	while ((line = g_data_input_stream_read_line(data_in, NULL, NULL, NULL)))
	{
		line_tokens = g_strsplit(line, ":", 0);
		if ((g_strv_length(line_tokens) > 6)
		 && !this->is_blacklisted (line_tokens[0]))
		{
			pkg_tokens = split_package_name(line_tokens[0]);

			/* If the split_package_name doesn't return a full name and an
			 * extension, it is a collection. We save its name in this case */
			if (pkg_tokens[3])
			{
				sqlite3_bind_text(stmt, 1, pkg_tokens[3], -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 9, "desktop-gnome", -1, SQLITE_STATIC);
				if (g_strcmp0(line_tokens[1], "obsolete"))
				{
					sqlite3_bind_text(stmt, 11, pkg_tokens[4], -1, SQLITE_TRANSIENT);
				}
				else
				{
					sqlite3_bind_text(stmt, 11, "obsolete", -1, SQLITE_STATIC);
				}
			}
			else if (!collection_name)
			{
				collection_name = g_strdup(pkg_tokens[0]);
				sqlite3_bind_text(stmt, 1, line_tokens[0], -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 9, "collections", -1, SQLITE_STATIC);
				sqlite3_bind_null(stmt, 11);
			}
			else
			{
				skip = TRUE; /* Skip other candidates for collections */
			}
			if (skip)
			{
				skip = FALSE;
			}
			else
			{
				sqlite3_bind_text(stmt, 2, pkg_tokens[0], -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 3, pkg_tokens[1], -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 4, pkg_tokens[2], -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 5, line_tokens[2], -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 6, line_tokens[2], -1, SQLITE_TRANSIENT);
				sqlite3_bind_int(stmt, 7, atoi(line_tokens[5]));
				sqlite3_bind_int(stmt, 8, atoi(line_tokens[5]));
				sqlite3_bind_int(stmt, 10, this->get_order ());

				sqlite3_step(stmt);
				sqlite3_clear_bindings(stmt);
				sqlite3_reset(stmt);
			}
			g_strfreev(pkg_tokens);
		}
		g_strfreev(line_tokens);
		g_free(line);
	}

	/* Create a collection entry */
	if (collection_name && g_seekable_seek(G_SEEKABLE(data_in), 0, G_SEEK_SET, NULL, NULL)
	 && (sqlite3_prepare_v2(job_data->db,
	                        "INSERT INTO collections (name, repo_order, collection_pkg) "
	                        "VALUES (@name, @repo_order, @collection_pkg)",
	                        -1,
	                        &stmt,
	                        NULL) == SQLITE_OK))
	{
		while ((line = g_data_input_stream_read_line(data_in, NULL, NULL, NULL)))
		{
			line_tokens = g_strsplit(line, ":", 0);
			if ((g_strv_length(line_tokens) > 6)
			 && !this->is_blacklisted (line_tokens[0]))
			{
				pkg_tokens = split_package_name(line_tokens[0]);

				/* If not a collection itself */
				if (pkg_tokens[3]) /* Save this package as a part of the collection */
				{
					sqlite3_bind_text(stmt, 1, collection_name, -1, SQLITE_TRANSIENT);
					sqlite3_bind_int(stmt, 2, this->get_order ());
					sqlite3_bind_text(stmt, 3, pkg_tokens[0], -1, SQLITE_TRANSIENT);
					sqlite3_step(stmt);
					sqlite3_clear_bindings(stmt);
					sqlite3_reset(stmt);
				}
				g_strfreev(pkg_tokens);
			}
			g_strfreev(line_tokens);
			g_free(line);
		}
		sqlite3_finalize(stmt);
	}
	g_free(collection_name);

	sqlite3_exec(job_data->db, "END TRANSACTION", NULL, NULL, NULL);

out:
	if (data_in)
	{
		g_object_unref(data_in);
	}
	if (fin)
	{
		g_object_unref(fin);
	}
	g_object_unref(list_file);
	g_free(list_filename);
}

Dl::~Dl () noexcept
{
	if (this->blacklist)
	{
		g_regex_unref (this->blacklist);
	}

	g_free (this->name);
	g_free (this->mirror);
	g_free (this->index_file);
}

/**
 * slack::Dl::Dl:
 * @name: Repository name.
 * @mirror: Repository mirror.
 * @order: Repository order.
 * @blacklist: Repository blacklist.
 * @index_file: The index file URL.
 *
 * Constructor.
 *
 * Return value: New #slack::Dl.
 **/
Dl::Dl (const gchar *name, const gchar *mirror,
		guint8 order, const gchar *blacklist, gchar *index_file) noexcept
{
	GRegex *regex;

	if (blacklist)
	{
		regex = static_cast<GRegex *> (g_regex_new (blacklist,
					G_REGEX_OPTIMIZE, static_cast<GRegexMatchFlags> (0), NULL));
	}
	else
	{
		regex = NULL;
	}

	this->name = g_strdup (name);
	this->mirror = g_strdup (mirror);

	this->order = order;

	this->blacklist = regex;

	this->index_file = index_file;
}

}
