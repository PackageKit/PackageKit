#include <bzlib.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include "slackpkg.h"
#include "utils.h"

namespace slack {

GHashTable *Slackpkg::cat_map = NULL;

/*
 * slack::Slackpkg::manifest:
 * @job:      a #PkBackendJob.
 * @tmpl:     temporary directory.
 * @filename: manifest filename
 *
 * Parse the manifest file and save the file list in the database.
 */
void
Slackpkg::manifest (PkBackendJob *job,
		const gchar *tmpl, gchar *filename) noexcept
{
	FILE *manifest;
	gint err, read_len;
	guint pos;
	gchar buf[max_buf_size], *path, *pkg_filename, *rest = NULL, *start;
	gchar *full_name = NULL;
	gchar **line, **lines;
	BZFILE *manifest_bz2;
	GRegex *pkg_expr = NULL, *file_expr = NULL;
	GMatchInfo *match_info;
	sqlite3_stmt *statement = NULL;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	path = g_build_filename(tmpl,
	                        this->get_name (),
	                        filename,
	                        NULL);
	manifest = fopen(path, "rb");
	g_free(path);

	if (!manifest)
	{
		return;
	}
	if (!(manifest_bz2 = BZ2_bzReadOpen(&err, manifest, 0, 0, NULL, 0)))
	{
		goto out;
	}

	/* Prepare regular expressions */
	pkg_expr = g_regex_new("^\\|\\|[[:blank:]]+Package:[[:blank:]]+.+\\/(.+)\\.(t[blxg]z$)?",
	                       static_cast<GRegexCompileFlags> (G_REGEX_OPTIMIZE | G_REGEX_DUPNAMES),
	                       static_cast<GRegexMatchFlags> (0),
	                       NULL);
	file_expr = g_regex_new("^[-bcdlps][-r][-w][-xsS][-r][-w][-xsS][-r][-w]"
	                        "[-xtT][[:space:]][^[:space:]]+[[:space:]]+"
	                        "[[:digit:]]+[[:space:]][[:digit:]-]+[[:space:]]"
	                        "[[:digit:]:]+[[:space:]](?!install\\/|\\.)(.*)",
	                        static_cast<GRegexCompileFlags> (G_REGEX_OPTIMIZE | G_REGEX_DUPNAMES),
	                        static_cast<GRegexMatchFlags> (0),
	                        NULL);
	if (!(file_expr) || !(pkg_expr))
	{
		goto out;
	}

	/* Prepare SQL statements */
	if (sqlite3_prepare_v2(job_data->db,
						   "INSERT INTO filelist (full_name, filename) VALUES (@full_name, @filename)",
						   -1,
						   &statement,
						   NULL) != SQLITE_OK)
	{
		goto out;
	}

	sqlite3_exec(job_data->db, "BEGIN TRANSACTION", NULL, NULL, NULL);
	while ((read_len = BZ2_bzRead(&err, manifest_bz2, buf, max_buf_size - 1)))
	{
		if ((err != BZ_OK) && (err != BZ_STREAM_END))
		{
			break;
		}
		buf[read_len] = '\0';

		/* Split the read text into lines */
		lines = g_strsplit(buf, "\n", 0);
		if (rest)
		{ /* Add to the first line rest characters from the previous read operation */
			start = lines[0];
			lines[0] = g_strconcat(rest, lines[0], NULL);
			g_free(start);
			g_free(rest);
		}
		if (err != BZ_STREAM_END) /* The last line can be incomplete */
		{
			pos = g_strv_length(lines) - 1;
			rest = lines[pos];
			lines[pos] = NULL;
		}
		for (line = lines; *line; line++)
		{
			if (g_regex_match(pkg_expr, *line, static_cast<GRegexMatchFlags> (0), &match_info))
			{
				if (g_match_info_get_match_count(match_info) > 2)
				{ /* If the extension matches */
					g_free(full_name);
					full_name = g_match_info_fetch(match_info, 1);
				}
				else
				{
					full_name = NULL;
				}
			}
			g_match_info_free(match_info);

			match_info = NULL;
			if (full_name && g_regex_match(file_expr, *line, static_cast<GRegexMatchFlags> (0), &match_info))
			{
				pkg_filename = g_match_info_fetch(match_info, 1);
				sqlite3_bind_text(statement, 1, full_name, -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(statement, 2, pkg_filename, -1, SQLITE_TRANSIENT);
				sqlite3_step(statement);
				sqlite3_clear_bindings(statement);
				sqlite3_reset(statement);
				g_free(pkg_filename);
			}
			g_match_info_free(match_info);
		}
		g_strfreev(lines);
	}

	sqlite3_exec(job_data->db, "END TRANSACTION", NULL, NULL, NULL);
	g_free(full_name);
	BZ2_bzReadClose(&err, manifest_bz2);

out:
	sqlite3_finalize(statement);
	if (file_expr)
	{
		g_regex_unref(file_expr);
	}
	if (pkg_expr)
	{
		g_regex_unref(pkg_expr);
	}
	fclose(manifest);
}

/**
 * slack::Slackpkg::collect_cache_info:
 * @tmpl: temporary directory for downloading the files.
 *
 * Download files needed to get the information like the list of packages
 * in available repositories, updates, package descriptions and so on.
 *
 * Returns: List of files needed for building the cache.
 **/
GSList *
Slackpkg::collect_cache_info (const gchar *tmpl) noexcept
{
	CURL *curl = NULL;
	gchar **source_dest;
	GSList *file_list = NULL;
	GFile *tmp_dir, *repo_tmp_dir;

	/* Create the temporary directory for the repository */
	tmp_dir = g_file_new_for_path(tmpl);
	repo_tmp_dir = g_file_get_child(tmp_dir, this->get_name ());
	g_file_make_directory(repo_tmp_dir, NULL, NULL);

	/* Download PACKAGES.TXT. These files are most important, break if some of them couldn't be found */
	for (gchar **cur_priority = this->priority; *cur_priority; cur_priority++)
	{
		source_dest = static_cast<gchar **> (g_malloc_n(3, sizeof(gchar *)));
		source_dest[0] = g_strconcat(this->get_mirror (),
									 *cur_priority,
									 "/PACKAGES.TXT",
									 NULL);
		source_dest[1] = g_build_filename(tmpl,
		                                  this->get_name (),
		                                  "PACKAGES.TXT",
		                                  NULL);
		source_dest[2] = NULL;

		if (get_file(&curl, source_dest[0], NULL) == CURLE_OK)
		{
			file_list = g_slist_prepend(file_list, source_dest);
		}
		else
		{
			g_strfreev(source_dest);
			g_slist_free_full(file_list, (GDestroyNotify)g_strfreev);
			goto out;
		}

		/* Download file lists if available */
		source_dest = static_cast<gchar **> (g_malloc_n(3, sizeof(gchar *)));
		source_dest[0] = g_strconcat(this->get_mirror (),
		                             *cur_priority,
		                             "/MANIFEST.bz2",
		                             NULL);
		source_dest[1] = g_strconcat(tmpl,
		                             "/", this->get_name (),
		                             "/", *cur_priority, "-MANIFEST.bz2",
		                             NULL);
		source_dest[2] = NULL;
		if (get_file(&curl, source_dest[0], NULL) == CURLE_OK)
		{
			file_list = g_slist_prepend(file_list, source_dest);
		}
		else
		{
			g_strfreev(source_dest);
		}
	}
out:
	g_object_unref(repo_tmp_dir);
	g_object_unref(tmp_dir);

	if (curl)
	{
		curl_easy_cleanup(curl);
	}
	return file_list;
}

/**
 * slack::Slackpkg::generate_cache:
 * @job: A #PkBackendJob.
 * @tmpl: temporary directory for downloading the files.
 *
 * Download files needed to get the information like the list of packages
 * in available repositories, updates, package descriptions and so on.
 *
 * Returns: List of files needed for building the cache.
 **/
void
Slackpkg::generate_cache (PkBackendJob *job, const gchar *tmpl) noexcept
{
	gchar **pkg_tokens = NULL;
	gchar *query = NULL, *filename = NULL, *location = NULL, *summary = NULL, *line, *packages_txt;
	guint pkg_compressed = 0, pkg_uncompressed = 0;
	gushort pkg_name_len;
	GString *desc;
	GFile *list_file;
	GFileInputStream *fin = NULL;
	GDataInputStream *data_in = NULL;
	sqlite3_stmt *insert_statement = NULL, *update_statement = NULL, *insert_default_statement = NULL, *statement;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	/* Check if the temporary directory for this repository exists, then the file metadata have to be generated */
	packages_txt = g_build_filename(tmpl,
	                                this->get_name (),
	                                "PACKAGES.TXT",
	                                NULL);
	list_file = g_file_new_for_path(packages_txt);
	fin = g_file_read(list_file, NULL, NULL);
	g_object_unref(list_file);
	g_free(packages_txt);
	if (!fin)
	{
		goto out;
	}
	/* Remove the old entries from this repository */
	if (sqlite3_prepare_v2(job_data->db,
	                       "DELETE FROM repos WHERE repo LIKE @repo",
	                       -1,
	                       &statement,
	                       NULL) == SQLITE_OK)
	{
		sqlite3_bind_text(statement,
		                  1,
		                  this->get_name (),
		                  -1,
		                  SQLITE_TRANSIENT);
		sqlite3_step(statement);
		sqlite3_finalize(statement);
	}
	if (sqlite3_prepare_v2(job_data->db,
	                       "INSERT INTO repos (repo_order, repo) VALUES (@repo_order, @repo)",
	                       -1,
	                       &statement,
	                       NULL) != SQLITE_OK)
	{
		goto out;
	}
	sqlite3_bind_int(statement, 1, this->get_order ());
	sqlite3_bind_text(statement,
	                  2,
	                  this->get_name (),
	                  -1,
	                  SQLITE_TRANSIENT);
	sqlite3_step(statement);
	sqlite3_finalize(statement);

	/* Insert new records */
	if ((sqlite3_prepare_v2(job_data->db,
	                        "INSERT OR REPLACE INTO pkglist (full_name, ver, arch, ext, location, "
	                        "summary, desc, compressed, uncompressed, name, repo_order, cat) "
	                        "VALUES (@full_name, @ver, @arch, @ext, @location, @summary, "
	                        "@desc, @compressed, @uncompressed, @name, @repo_order, @cat)",
	                        -1,
	                        &insert_statement,
	                        NULL) != SQLITE_OK)
	 || (sqlite3_prepare_v2(job_data->db,
	                    "INSERT OR REPLACE INTO pkglist (full_name, ver, arch, ext, location, "
	                    "summary, desc, compressed, uncompressed, name, repo_order) "
	                    "VALUES (@full_name, @ver, @arch, @ext, @location, @summary, "
	                    "@desc, @compressed, @uncompressed, @name, @repo_order)",
	                    -1,
	                    &insert_default_statement,
	                    NULL) != SQLITE_OK)) 
	{
		goto out;
	}
	query = sqlite3_mprintf("UPDATE pkglist SET full_name = @full_name, ver = @ver, arch = @arch, "
	                        "ext = @ext, location = @location, summary = @summary, "
	                        "desc = @desc, compressed = @compressed, uncompressed = @uncompressed "
	                        "WHERE name LIKE @name AND repo_order = %u",
	                        this->get_order ());
	if (sqlite3_prepare_v2(job_data->db, query, -1, &update_statement, NULL) != SQLITE_OK)
	{
		goto out;
	}

	data_in = g_data_input_stream_new(G_INPUT_STREAM(fin));
	desc = g_string_new("");

	sqlite3_exec(job_data->db, "BEGIN TRANSACTION", NULL, NULL, NULL);

	while ((line = g_data_input_stream_read_line(data_in, NULL, NULL, NULL)))
	{
		if (!strncmp(line, "PACKAGE NAME:  ", 15))
		{
			filename = g_strdup(line + 15);
			if (this->is_blacklisted (filename))
			{
				g_free(filename);
				filename = NULL;
			}
		}
		else if (filename && !strncmp(line, "PACKAGE LOCATION:  ", 19))
		{
			location = g_strdup(line + 21); /* Exclude ./ at the path beginning */
		}
		else if (filename && !strncmp(line, "PACKAGE SIZE (compressed):  ", 28))
		{
			/* Remove the unit (kilobytes) */
			pkg_compressed = atoi(g_strndup(line + 28, strlen(line + 28) - 2)) * 1024;
		}
		else if (filename && !strncmp(line, "PACKAGE SIZE (uncompressed):  ", 30))
		{
			/* Remove the unit (kilobytes) */
			pkg_uncompressed = atoi(g_strndup(line + 30, strlen(line + 30) - 2)) * 1024;
		}
		else if (filename && !g_strcmp0(line, "PACKAGE DESCRIPTION:"))
		{
			g_free(line);
			line = g_data_input_stream_read_line(data_in, NULL, NULL, NULL); /* Short description */

			summary = g_strstr_len(line, -1, "(");
			if (summary) /* Else summary = NULL */
			{
				summary = g_strndup(summary + 1, strlen(summary) - 2); /* Without ( ) */
			}
			pkg_tokens = split_package_name(filename);
			pkg_name_len = strlen(pkg_tokens[0]); /* Description begins with pkg_name: */
		}
		else if (filename && !strncmp(line, pkg_tokens[0], pkg_name_len))
		{
			g_string_append(desc, line + pkg_name_len + 1);
		}
		else if (filename && !g_strcmp0(line, ""))
		{
			if (g_strcmp0(location, "patches/packages")) /* Insert a new package */
			{
				/* Get the package group based on its location */
				const char *cat = g_strrstr(location, "/");
				if (cat) /* Else cat = NULL */
				{
					cat = static_cast<const char *> (g_hash_table_lookup(cat_map, cat + 1));
				}
				if (cat)
				{
					statement = insert_statement;
					sqlite3_bind_text(insert_statement, 12, cat, -1, SQLITE_TRANSIENT);
				}
				else
				{
					statement = insert_default_statement;
				}
				sqlite3_bind_int(statement, 11, this->get_order ());
			}
			else /* Update package information if it is a patch */
			{
				statement = update_statement;
			}
			sqlite3_bind_text(statement, 1, pkg_tokens[3], -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 2, pkg_tokens[1], -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 3, pkg_tokens[2], -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 4, pkg_tokens[4], -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 5, location, -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 6, summary, -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 7, desc->str, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(statement, 8, pkg_compressed);
			sqlite3_bind_int(statement, 9, pkg_uncompressed);
			sqlite3_bind_text(statement, 10, pkg_tokens[0], -1, SQLITE_TRANSIENT);

			sqlite3_step(statement);
			sqlite3_clear_bindings(statement);
			sqlite3_reset(statement);

			/* Reset for the next package */
			g_strfreev(pkg_tokens);
			g_free(filename);
			g_free(location);
			g_free(summary);
			filename = location = summary = NULL;
			g_string_assign(desc, "");
			pkg_compressed = pkg_uncompressed = 0;
		}
		g_free(line);
	}
	sqlite3_exec(job_data->db, "END TRANSACTION", NULL, NULL, NULL);

	g_string_free(desc, TRUE);
	g_object_unref(data_in);

	/* Parse MANIFEST.bz2 */
	for (gchar **p = this->priority; *p; p++)
	{
		filename = g_strconcat(*p, "-MANIFEST.bz2", NULL);
		manifest (job, tmpl, filename);
		g_free(filename);
	}
out:
	sqlite3_finalize(update_statement);
	sqlite3_free(query);
	sqlite3_finalize(insert_default_statement);
	sqlite3_finalize(insert_statement);

	if (fin)
	{
		g_object_unref(fin);
	}
}

Slackpkg::~Slackpkg () noexcept
{
	if (this->blacklist)
	{
		g_regex_unref (this->blacklist);
	}

	g_free (this->name);
	g_free (this->mirror);
	if (this->priority)
	{
		g_strfreev (this->priority);
	}
}

/**
 * slack::Slackpkg::Slackpkg:
 * @name: Repository name.
 * @mirror: Repository mirror.
 * @order: Repository order.
 * @blacklist: Blacklist.
 * @priority: Groups priority.
 *
 * Constructor.
 *
 * Returns: New #slack::Slackpkg.
 **/
Slackpkg::Slackpkg (const gchar *name, const gchar *mirror,
		guint8 order, const gchar *blacklist, gchar **priority) noexcept
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

	this->priority = priority;

	// Initialize category map
	if (cat_map == NULL)
	{
		cat_map = g_hash_table_new(g_str_hash, g_str_equal);
		g_hash_table_insert (cat_map, (gpointer) "a", (gpointer) "system");
		g_hash_table_insert (cat_map, (gpointer) "ap", (gpointer) "admin-tools");
		g_hash_table_insert (cat_map, (gpointer) "d", (gpointer) "programming");
		g_hash_table_insert (cat_map, (gpointer) "e", (gpointer) "programming");
		g_hash_table_insert (cat_map, (gpointer) "f", (gpointer) "documentation");
		g_hash_table_insert (cat_map, (gpointer) "k", (gpointer) "system");
		g_hash_table_insert (cat_map, (gpointer) "kde", (gpointer) "desktop-kde");
		g_hash_table_insert (cat_map, (gpointer) "kdei", (gpointer) "localization");
		g_hash_table_insert (cat_map, (gpointer) "l", (gpointer) "system");
		g_hash_table_insert (cat_map, (gpointer) "n", (gpointer) "network");
		g_hash_table_insert (cat_map, (gpointer) "t", (gpointer) "publishing");
		g_hash_table_insert (cat_map, (gpointer) "tcl", (gpointer) "system");
		g_hash_table_insert (cat_map, (gpointer) "x", (gpointer) "desktop-other");
		g_hash_table_insert (cat_map, (gpointer) "xap", (gpointer) "accessories");
		g_hash_table_insert (cat_map, (gpointer) "xfce", (gpointer) "desktop-xfce");
		g_hash_table_insert (cat_map, (gpointer) "y", (gpointer) "games");
	}
}

}
