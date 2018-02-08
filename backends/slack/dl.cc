#include <sqlite3.h>
#include <stdlib.h>
#include "dl.h"
#include "slack-utils.h"

typedef struct
{
	gchar *name;
	gchar *mirror;
	guint8 order;
	GRegex *blacklist;
	gchar *index_file;
} SlackDlPrivate;

enum
{
	PROP_BLACKLIST = 1,
	N_PROPERTIES,
	PROP_NAME,
	PROP_MIRROR,
	PROP_ORDER
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

static void
slack_dl_pkgtools_interface_init(SlackPkgtoolsInterface *iface);

G_DEFINE_TYPE_WITH_CODE(SlackDl, slack_dl, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(SLACK_TYPE_PKGTOOLS,
                                              slack_dl_pkgtools_interface_init);
						G_ADD_PRIVATE(SlackDl))

static GSList *
slack_dl_collect_cache_info(SlackPkgtools *pkgtools, const gchar *tmpl)
{
	CURL *curl = NULL;
	SlackDl *dl = SLACK_DL(pkgtools);
	auto priv = static_cast<SlackDlPrivate *> (slack_dl_get_instance_private(dl));
	GSList *file_list = NULL;
	GFile *tmp_dir, *repo_tmp_dir;

	/* Create the temporary directory for the repository */
	tmp_dir = g_file_new_for_path(tmpl);
	repo_tmp_dir = g_file_get_child(tmp_dir, dl->get_name ());
	g_file_make_directory(repo_tmp_dir, NULL, NULL);

	/* There is no ChangeLog yet to check if there are updates or not. Just mark the index file for download */
	auto source_dest = static_cast<gchar **> (g_malloc_n(3, sizeof(gchar *)));
	source_dest[0] = g_strdup(priv->index_file);
	source_dest[1] = g_build_filename(tmpl,
	                                  dl->get_name (),
	                                  "IndexFile",
	                                  NULL);
	source_dest[2] = NULL;
	/* Check if the remote file can be found */
	if (slack_get_file(&curl, source_dest[0], NULL))
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

static void
slack_dl_generate_cache(SlackPkgtools *pkgtools,
                        PkBackendJob *job,
                        const gchar *tmpl)
{
	SlackDl *dl = SLACK_DL(pkgtools);
	gchar **line_tokens, **pkg_tokens, *line, *collection_name = NULL, *list_filename;
	gboolean skip = FALSE;
	GFile *list_file;
	GFileInputStream *fin;
	GDataInputStream *data_in = NULL;
	sqlite3_stmt *stmt = NULL;
	auto job_data = static_cast<JobData *> (pk_backend_job_get_user_data(job));

	/* Check if the temporary directory for this repository exists. If so the file metadata have to be generated */
	list_filename = g_build_filename(tmpl,
	                                 dl->get_name (),
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
		sqlite3_bind_text(stmt, 1, dl->get_name (), -1, SQLITE_TRANSIENT);
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
	sqlite3_bind_int(stmt, 1, slack_dl_get_order(dl));
	sqlite3_bind_text(stmt, 2, dl->get_name (), -1, SQLITE_TRANSIENT);
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
		 && !slack_dl_is_blacklisted(dl, line_tokens[0]))
		{
			pkg_tokens = slack_split_package_name(line_tokens[0]);

			/* If the slack_split_package_name doesn't return a full name and an
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
				sqlite3_bind_int(stmt, 10, slack_dl_get_order(dl));

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
			 && !slack_dl_is_blacklisted(dl, line_tokens[0]))
			{
				pkg_tokens = slack_split_package_name(line_tokens[0]);

				/* If not a collection itself */
				if (pkg_tokens[3]) /* Save this package as a part of the collection */
				{
					sqlite3_bind_text(stmt, 1, collection_name, -1, SQLITE_TRANSIENT);
					sqlite3_bind_int(stmt, 2, slack_dl_get_order(dl));
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

static void
slack_dl_pkgtools_interface_init(SlackPkgtoolsInterface *iface)
{
	iface->collect_cache_info = slack_dl_collect_cache_info;
	iface->generate_cache = slack_dl_generate_cache;
}

static void
slack_dl_init(SlackDl *dl)
{
}

static void
slack_dl_dispose(GObject *object)
{
	SlackDl *dl = SLACK_DL(object);
	auto priv = static_cast<SlackDlPrivate *> (slack_dl_get_instance_private(dl));

	if (priv->blacklist)
	{
		g_regex_unref(priv->blacklist);
	}

	G_OBJECT_CLASS(slack_dl_parent_class)->dispose(object);
}

static void
slack_dl_finalize(GObject *object)
{
	SlackDl *dl = SLACK_DL(object);
	auto priv = static_cast<SlackDlPrivate *> (slack_dl_get_instance_private(dl));

	g_free(priv->name);
	g_free(priv->mirror);
	g_free(priv->index_file);

	G_OBJECT_CLASS(slack_dl_parent_class)->finalize(object);
}

static void
slack_dl_set_property(GObject *object,
                      guint prop_id,
                      const GValue *value,
                      GParamSpec *pspec)
{
	SlackDl *dl = SLACK_DL(object);
	auto priv = static_cast<SlackDlPrivate *> (slack_dl_get_instance_private(dl));

	switch (prop_id)
	{
		case PROP_NAME:
			g_free(priv->name);
			priv->name = g_value_dup_string(value);
			break;

		case PROP_MIRROR:
			g_free(priv->mirror);
			priv->mirror = g_value_dup_string(value);
			break;

		case PROP_ORDER:
			priv->order = g_value_get_uint(value);
			break;

		case PROP_BLACKLIST:
			if (priv->blacklist)
			{
				g_regex_unref(priv->blacklist);
			}
			priv->blacklist = static_cast<GRegex *> (g_value_get_boxed(value));
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void
slack_dl_get_property(GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
	SlackDl *dl = SLACK_DL(object);
	auto priv = static_cast<SlackDlPrivate *> (slack_dl_get_instance_private(dl));

	switch (prop_id)
	{
		case PROP_NAME:
			g_value_set_string(value, priv->name);
			break;

		case PROP_MIRROR:
			g_value_set_string(value, priv->mirror);
			break;

		case PROP_ORDER:
			g_value_set_uint(value, priv->order);
			break;

		case PROP_BLACKLIST:
			g_value_set_boxed(value, priv->blacklist);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void
slack_dl_class_init(SlackDlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = slack_dl_dispose;
	object_class->finalize = slack_dl_finalize;

	object_class->set_property = slack_dl_set_property;
	object_class->get_property = slack_dl_get_property;

	g_object_class_override_property(object_class, PROP_NAME, "name");
	g_object_class_override_property(object_class, PROP_MIRROR, "mirror");
	g_object_class_override_property(object_class, PROP_ORDER, "order");

	properties[PROP_BLACKLIST] = g_param_spec_boxed ("blacklist",
			"Blacklist", "Blacklist", G_TYPE_REGEX,
			static_cast<GParamFlags> (G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

	g_object_class_install_properties(object_class, N_PROPERTIES, properties);
}

/**
 * slack_dl_new:
 * @name: Repository name.
 * @mirror: Repository mirror.
 * @order: Repository order.
 * @blacklist: Repository blacklist.
 * @index_file: The index file URL.
 *
 * Constructor.
 *
 * Return value: New #SlackDl.
 **/
SlackDl *
slack_dl_new(const gchar *name,
             const gchar *mirror,
             guint8 order,
             const gchar *blacklist,
             gchar *index_file)
{
	GRegex *regex;

	if (blacklist)
	{
		regex = g_regex_new(blacklist, G_REGEX_OPTIMIZE, static_cast<GRegexMatchFlags> (0), NULL);
	}
	else
	{
		regex = NULL;
	}

	auto dl = static_cast<SlackDl *> (g_object_new (SLACK_TYPE_DL,
				"name", name,
				"mirror", mirror,
				"order", order,
				"blacklist", regex,
				NULL));
	auto priv = static_cast<SlackDlPrivate *> (slack_dl_get_instance_private(dl));
	priv->index_file = index_file;

	if (regex)
	{
		g_regex_unref(regex);
	}

	return dl;
}

/**
 * slack_dl_get_order:
 * @dl: This class instance.
 *
 * Retrieves the repository order.
 *
 * Return value: Repository order.
 **/
guint8
slack_dl_get_order(SlackDl *dl)
{
	GValue order = G_VALUE_INIT;

	g_value_init(&order, G_TYPE_UINT);
	g_object_get_property(G_OBJECT(dl), "order", &order);

	return g_value_get_uint(&order);
}

/**
 * slack_dl_is_blacklisted:
 * @dl: This class instance.
 * @pkg: Package name to check for.
 *
 * Checks whether a package is blacklisted.
 *
 * Returns: %TRUE if the package is blacklisted, %FALSE otherwise.
 **/
gboolean
slack_dl_is_blacklisted(SlackDl *dl, const gchar *pkg)
{
	GValue blacklist = G_VALUE_INIT;

	g_value_init(&blacklist, G_TYPE_REGEX);
	g_object_get_property(G_OBJECT(dl), "blacklist", &blacklist);

	auto regex = static_cast<GRegex *> (g_value_get_boxed(&blacklist));
	return regex && g_regex_match(regex, pkg, static_cast<GRegexMatchFlags> (0), NULL);
}
