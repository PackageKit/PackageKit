#include <stdlib.h>
#include <bzlib.h>
#include "pkgtools.h"
#include "binary.h"

enum
{
	PROP_NAME = 1,
	PROP_MIRROR,
	PROP_ORDER,
	PROP_BLACKLIST,
};

typedef struct _KatjaBinaryPrivate
{
	gchar *name;
	gchar *mirror;
	gushort order;
	GRegex *blacklist;
} KatjaBinaryPrivate;

// Prototypes
static void katja_binary_pkgtools_interface_init(KatjaPkgtoolsInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE(KatjaBinary, katja_binary, G_TYPE_OBJECT,
                                 G_ADD_PRIVATE(KatjaBinary)
                                 G_IMPLEMENT_INTERFACE(KATJA_TYPE_PKGTOOLS,
                                 katja_binary_pkgtools_interface_init))

static GSList *
katja_binary_collect_cache_info(KatjaPkgtools *pkgtools, const gchar *tmpl)
{
	KatjaBinaryClass *klass;

	g_return_val_if_fail(KATJA_IS_BINARY(pkgtools), NULL);

	klass = KATJA_BINARY_GET_CLASS(pkgtools);
	g_return_val_if_fail(klass->collect_cache_info != NULL, NULL);

	return klass->collect_cache_info(pkgtools, tmpl);
}

static void
katja_binary_generate_cache(KatjaPkgtools *pkgtools, PkBackendJob *job, const gchar *tmpl)
{
	KatjaBinaryClass *klass;

	g_return_if_fail(KATJA_IS_BINARY(pkgtools));

	klass = KATJA_BINARY_GET_CLASS(pkgtools);
	g_return_if_fail(klass->generate_cache != NULL);

	klass->generate_cache(pkgtools, job, tmpl);
}

static gboolean
katja_binary_download(KatjaPkgtools *pkgtools,
                      PkBackendJob *job,
                      gchar *dest_dir_name,
                      gchar *pkg_name)
{
	gchar *dest_filename, *source_url;
	gboolean ret = FALSE;
	sqlite3_stmt *statement = NULL;
	CURL *curl = NULL;
	auto job_data = static_cast<PkBackendKatjaJobData*>(pk_backend_job_get_user_data(job));
	KatjaBinary *binary = KATJA_BINARY(pkgtools);
	auto priv = static_cast<KatjaBinaryPrivate*>(katja_binary_get_instance_private(binary));

	if ((sqlite3_prepare_v2(job_data->db,
							"SELECT location, (full_name || '.' || ext) FROM pkglist "
							"WHERE name LIKE @name AND repo_order = @repo_order",
							-1,
							&statement,
							NULL) != SQLITE_OK))
		return FALSE;

	sqlite3_bind_text(statement, 1, pkg_name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(statement, 2, priv->order);

	if (sqlite3_step(statement) == SQLITE_ROW) {
		dest_filename = g_build_filename(dest_dir_name, sqlite3_column_text(statement, 1), NULL);
		source_url = g_strconcat(priv->mirror,
								 sqlite3_column_text(statement, 0),
								 "/",
								 sqlite3_column_text(statement, 1),
								 NULL);

		if (!g_file_test(dest_filename, G_FILE_TEST_EXISTS)) {
			if (katja_get_file(&curl, source_url, dest_filename) == CURLE_OK) {
				ret = TRUE;
			}
		} else {
			ret = TRUE;
		}

		if (curl)
			curl_easy_cleanup(curl);

		g_free(source_url);
		g_free(dest_filename);
	}
	sqlite3_finalize(statement);

	return ret;
}

static void
katja_binary_install(KatjaPkgtools *pkgtools,
                     PkBackendJob *job,
                     gchar *pkg_name)
{
	gchar *pkg_filename, *cmd_line;
	sqlite3_stmt *statement = NULL;
	auto job_data = static_cast<PkBackendKatjaJobData*>(pk_backend_job_get_user_data(job));
	KatjaBinary *binary = KATJA_BINARY(pkgtools);
	auto priv = static_cast<KatjaBinaryPrivate*>(katja_binary_get_instance_private(binary));

	if ((sqlite3_prepare_v2(job_data->db,
							"SELECT (full_name || '.' || ext) FROM pkglist "
							"WHERE name LIKE @name AND repo_order = @repo_order",
							-1,
							&statement,
							NULL) != SQLITE_OK))
	{
		return;
	}

	sqlite3_bind_text(statement, 1, pkg_name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(statement, 2, priv->order);

	if (sqlite3_step(statement) == SQLITE_ROW) {
		pkg_filename = g_build_filename(LOCALSTATEDIR,
								        "cache",
								        "PackageKit",
								        "downloads",
								        sqlite3_column_text(statement, 0),
								        NULL);
		cmd_line = g_strconcat("/sbin/upgradepkg --install-new ", pkg_filename, NULL);
		g_spawn_command_line_sync(cmd_line, NULL, NULL, NULL, NULL);
		g_free(cmd_line);

		g_free(pkg_filename);
	}
	sqlite3_finalize(statement);
}

void
katja_binary_pkgtools_interface_init(KatjaPkgtoolsInterface *iface)
{
	iface->collect_cache_info = katja_binary_collect_cache_info;
	iface->generate_cache = katja_binary_generate_cache;
	iface->download = katja_binary_download;
	iface->install = katja_binary_install;
}

/**
 * katja_binary_manifest:
 * @binary:   a #KatjaBinary.
 * @job:      a #PkBackendJob.
 * @tmpl:     temporary directory.
 * @filename: manifest filename
 *
 * Parse the manifest file and save the file list in the database.
 **/
void katja_binary_manifest(KatjaBinary *binary,
                           PkBackendJob *job,
                           const gchar *tmpl,
                           gchar *filename)
{
	FILE *manifest;
	gint err, read_len;
	guint pos;
	gchar buf[KATJA_BINARY_MAX_BUF_SIZE], *path, *pkg_filename, *rest = NULL, *start;
	gchar **line, **lines;
	BZFILE *manifest_bz2;
	GRegex *pkg_expr = NULL, *file_expr = NULL;
	GMatchInfo *match_info;
	sqlite3_stmt *statement = NULL;
	auto job_data = static_cast<PkBackendKatjaJobData*>(pk_backend_job_get_user_data(job));
	GValue name = G_VALUE_INIT;

	g_value_init(&name, G_TYPE_STRING);
	g_object_get_property(G_OBJECT(binary), "name", &name);

	path = g_build_filename(tmpl, g_value_get_string(&name), filename, NULL);
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
	                       static_cast<GRegexCompileFlags>(G_REGEX_OPTIMIZE | G_REGEX_DUPNAMES),
	                       static_cast<GRegexMatchFlags>(0),
	                       NULL);
	file_expr = g_regex_new("^[-bcdlps][-r][-w][-xsS][-r][-w][-xsS][-r][-w]"
	                        "[-xtT][[:space:]][^[:space:]]+[[:space:]]+"
	                        "[[:digit:]]+[[:space:]][[:digit:]-]+[[:space:]]"
	                        "[[:digit:]:]+[[:space:]](?!install\\/|\\.)(.*)",
	                        static_cast<GRegexCompileFlags>(G_REGEX_OPTIMIZE | G_REGEX_DUPNAMES),
	                        static_cast<GRegexMatchFlags>(0),
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
	while ((read_len = BZ2_bzRead(&err, manifest_bz2, buf, KATJA_BINARY_MAX_BUF_SIZE - 1)))
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
			gchar *full_name = NULL;

			if (g_regex_match(pkg_expr, *line, static_cast<GRegexMatchFlags>(0), &match_info))
			{
				if (g_match_info_get_match_count(match_info) > 2)
				{ /* If the extension matches */
					full_name = g_match_info_fetch(match_info, 1);
				}
			}
			g_match_info_free(match_info);

			match_info = NULL;
			if (full_name && g_regex_match(file_expr, *line, static_cast<GRegexMatchFlags>(0), &match_info))
			{
				pkg_filename = g_match_info_fetch(match_info, 1);
				sqlite3_bind_text(statement, 1, full_name, -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(statement, 2, pkg_filename, -1, SQLITE_TRANSIENT);
				sqlite3_step(statement);
				sqlite3_clear_bindings(statement);
				sqlite3_reset(statement);
				g_free(pkg_filename);
			}

			g_free(full_name);
			g_match_info_free(match_info);
		}
		g_strfreev(lines);
	}

	sqlite3_exec(job_data->db, "END TRANSACTION", NULL, NULL, NULL);
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

static void
katja_binary_init(KatjaBinary *binary)
{
}

static void
katja_binary_set_property(GObject *object,
                          guint property_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
	KatjaBinary *binary = KATJA_BINARY(object);
	auto priv = static_cast<KatjaBinaryPrivate*>(katja_binary_get_instance_private(binary));

	switch (property_id)
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
			priv->blacklist = static_cast<GRegex*>(g_value_get_boxed(value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void
katja_binary_get_property(GObject *object,
                          guint property_id,
                          GValue *value,
                          GParamSpec *pspec)
{
	KatjaBinary *binary = KATJA_BINARY(object);
	auto priv = static_cast<KatjaBinaryPrivate*>(katja_binary_get_instance_private(binary));

	switch (property_id)
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
			g_value_take_boxed(value, priv->blacklist);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void
katja_binary_class_init(KatjaBinaryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	// Properties
	object_class->set_property = katja_binary_set_property;
	object_class->get_property = katja_binary_get_property;

	g_object_class_override_property(object_class, PROP_NAME, "name");
	g_object_class_override_property(object_class, PROP_MIRROR, "mirror");
	g_object_class_override_property(object_class, PROP_ORDER, "order");
	g_object_class_override_property(object_class, PROP_BLACKLIST, "blacklist");

	// Implementations
	klass->collect_cache_info = katja_binary_collect_cache_info;
	klass->generate_cache = katja_binary_generate_cache;
}
