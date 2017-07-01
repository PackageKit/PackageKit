#include <stdlib.h>
#include "dl.h"

namespace katja
{

Dl::Dl(const std::string& name,
       const std::string& mirror,
       std::uint8_t order,
       const gchar* blacklist,
       const std::string& indexFile)
	: indexFile_(indexFile)
{
	name_ = name;
	mirror_ = mirror;
	order_ = order;

	if (blacklist)
	{
		blacklist_ = g_regex_new(blacklist, G_REGEX_OPTIMIZE, static_cast<GRegexMatchFlags>(0), NULL);
	}
}

Dl::~Dl()
{
	g_regex_unref(blacklist_);
}

GSList*
Dl::collectCacheInfo(const gchar *tmpl)
{
	CURL *curl = NULL;
	gchar **source_dest;
	GSList *file_list = NULL;
	GFile *tmp_dir, *repo_tmp_dir;

	/* Create the temporary directory for the repository */
	tmp_dir = g_file_new_for_path(tmpl);
	repo_tmp_dir = g_file_get_child(tmp_dir, name().c_str());
	g_file_make_directory(repo_tmp_dir, NULL, NULL);

	/* There is no ChangeLog yet to check if there are updates or not. Just mark the index file for download */
	source_dest = static_cast<gchar**>(g_malloc_n(3, sizeof(gchar *)));
	source_dest[0] = g_strdup(indexFile_.c_str());
	source_dest[1] = g_build_filename(tmpl,
	                                  name().c_str(),
	                                  "IndexFile",
	                                  NULL);
	source_dest[2] = NULL;
	/* Check if the remote file can be found */
	if (katja_get_file(&curl, source_dest[0], NULL))
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

void
Dl::generateCache(PkBackendJob *job, const gchar *tmpl)
{
	gchar **line_tokens, **pkg_tokens, *line, *collection_name = NULL, *list_filename;
	gboolean skip = FALSE;
	GFile *list_file;
	GFileInputStream *fin;
	GDataInputStream *data_in = NULL;
	sqlite3_stmt *stmt = NULL;
	auto job_data = static_cast<PkBackendKatjaJobData*>(pk_backend_job_get_user_data(job));

	/* Check if the temporary directory for this repository exists. If so the file metadata have to be generated */
	list_filename = g_build_filename(tmpl,
	                                 name().c_str(),
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
		sqlite3_bind_text(stmt, 1, name().c_str(), -1, SQLITE_TRANSIENT);
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
	sqlite3_bind_int(stmt, 1, order());
	sqlite3_bind_text(stmt, 2, name().c_str(), -1, SQLITE_TRANSIENT);
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
		 && (!blacklist() || !g_regex_match(blacklist(),
		                                    line_tokens[0],
		                                    static_cast<GRegexMatchFlags>(0),
		                                    NULL)))
		{
			pkg_tokens = katja_cut_pkg(line_tokens[0]);

			/* If the katja_cut_pkg doesn't return a full name and an extension, it is a collection.
			 * We save its name in this case */
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
				sqlite3_bind_int(stmt, 10, order());

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
			 && (!blacklist()
			  || !g_regex_match(blacklist(),
			                    line_tokens[0],
								static_cast<GRegexMatchFlags>(0),
								NULL)))
			{
				pkg_tokens = katja_cut_pkg(line_tokens[0]);

				/* If not a collection itself */
				if (pkg_tokens[3]) /* Save this package as a part of the collection */
				{
					sqlite3_bind_text(stmt, 1, collection_name, -1, SQLITE_TRANSIENT);
					sqlite3_bind_int(stmt, 2, order());
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

}
