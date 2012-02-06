/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008-2010 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010-2011 Jonathan Conder <jonno.conder@gmail.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <alpm.h>
#include <glob.h>
#include <string.h>
#include <sys/utsname.h>

#include "pk-backend-alpm.h"
#include "pk-backend-config.h"
#include "pk-backend-databases.h"
#include "pk-backend-error.h"

typedef struct
{
	 gboolean	 checkspace, ilovecandy, totaldl, usedelta, usesyslog,
			 verbosepkglists;

	 gchar		*arch, *cleanmethod, *dbpath, *gpgdir, *logfile, *root,
			*xfercmd;

	 alpm_list_t	*cachedirs, *holdpkgs, *ignoregroups, *ignorepkgs,
			*noextracts, *noupgrades, *syncfirsts;

	 alpm_list_t	*repos;
	 GHashTable	*servers;
	 GHashTable	*levels;
	 GRegex		*xrepo, *xarch;
} PkBackendConfig;

static PkBackendConfig *
pk_backend_config_new (void)
{
	PkBackendConfig *config = g_new0 (PkBackendConfig, 1);
	alpm_siglevel_t *level = g_new0 (alpm_siglevel_t, 1);

	config->servers = g_hash_table_new_full (g_str_hash, g_str_equal,
						 g_free, NULL);
	config->levels = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
						g_free);

	*level |= ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL;
	*level |= ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL;
	g_hash_table_insert (config->levels, g_strdup ("options"), level);

	config->xrepo = g_regex_new ("\\$repo", 0, 0, NULL);
	config->xarch = g_regex_new ("\\$arch", 0, 0, NULL);

	return config;
}

static void
pk_backend_config_list_free (alpm_list_t *list)
{
	alpm_list_free_inner (list, g_free);
	alpm_list_free (list);
}

static gboolean
pk_backend_config_servers_free (gpointer repo, gpointer list, gpointer data)
{
	pk_backend_config_list_free ((alpm_list_t *) list);
	return TRUE;
}

static void
pk_backend_config_free (PkBackendConfig *config)
{
	g_return_if_fail (config != NULL);

	g_free (config->arch);
	g_free (config->cleanmethod);
	g_free (config->dbpath);
	g_free (config->gpgdir);
	g_free (config->logfile);
	g_free (config->root);
	g_free (config->xfercmd);

	FREELIST (config->cachedirs);
	FREELIST (config->holdpkgs);
	FREELIST (config->ignoregroups);
	FREELIST (config->ignorepkgs);
	FREELIST (config->noextracts);
	FREELIST (config->noupgrades);
	FREELIST (config->syncfirsts);

	pk_backend_config_list_free (config->repos);
	g_hash_table_foreach_remove (config->servers,
				     pk_backend_config_servers_free, NULL);
	g_hash_table_unref (config->servers);
	g_hash_table_unref (config->levels);

	g_regex_unref (config->xrepo);
	g_regex_unref (config->xarch);
}

static void
pk_backend_config_set_checkspace (PkBackendConfig *config)
{
	g_return_if_fail (config != NULL);

	config->checkspace = TRUE;
}

static void
pk_backend_config_set_ilovecandy (PkBackendConfig *config)
{
	g_return_if_fail (config != NULL);

	config->ilovecandy = TRUE;
}

static void
pk_backend_config_set_totaldl (PkBackendConfig *config)
{
	g_return_if_fail (config != NULL);

	config->totaldl = TRUE;
}

static void
pk_backend_config_set_usedelta (PkBackendConfig *config)
{
	g_return_if_fail (config != NULL);

	config->usedelta = TRUE;
}

static void
pk_backend_config_set_usesyslog (PkBackendConfig *config)
{
	g_return_if_fail (config != NULL);

	config->usesyslog = TRUE;
}

static void
pk_backend_config_set_verbosepkglists (PkBackendConfig *config)
{
	g_return_if_fail (config != NULL);

	config->verbosepkglists = TRUE;
}

typedef struct
{
	 const gchar	*name;
	 void		(*func) (PkBackendConfig *config);
} PkBackendConfigBoolean;

/* keep this in alphabetical order */
static const PkBackendConfigBoolean pk_backend_config_boolean_options[] = {
	{ "CheckSpace", pk_backend_config_set_checkspace },
	{ "ILoveCandy", pk_backend_config_set_ilovecandy },
	{ "TotalDownload", pk_backend_config_set_totaldl },
	{ "UseDelta", pk_backend_config_set_usedelta },
	{ "UseSyslog", pk_backend_config_set_usesyslog },
	{ "VerbosePkgLists", pk_backend_config_set_verbosepkglists },
	{ NULL, NULL }
};

static gboolean
pk_backend_config_set_boolean (PkBackendConfig *config, const gchar *option)
{
	gsize i;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (option != NULL, FALSE);

	for (i = 0;; ++i) {
		const gchar *name = pk_backend_config_boolean_options[i].name;
		gint cmp = g_strcmp0 (option, name);

		if (name == NULL || cmp < 0) {
			return FALSE;
		} else if (cmp == 0) {
			pk_backend_config_boolean_options[i].func (config);
			return TRUE;
		}
	}
}

static void
pk_backend_config_add_cachedir (PkBackendConfig *config, const gchar *path)
{
	gsize length;
	gchar *cachedir;

	g_return_if_fail (config != NULL);
	g_return_if_fail (path != NULL);

	/* allocate normally */
	length = strlen (path) + 1;
	cachedir = malloc (length * sizeof (gchar));
	g_strlcpy (cachedir, path, length);
	config->cachedirs = alpm_list_add (config->cachedirs, cachedir);
}

static void
pk_backend_config_set_arch (PkBackendConfig *config, const gchar *arch)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (arch != NULL);

	g_free (config->arch);
	if (g_strcmp0 (arch, "auto") == 0) {
		struct utsname un;
		uname (&un);
		config->arch = g_strdup (un.machine);
	} else {
		config->arch = g_strdup (arch);
	}
}

static void
pk_backend_config_set_cleanmethod (PkBackendConfig *config, const gchar *method)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (method != NULL);

	g_free (config->cleanmethod);
	config->cleanmethod = g_strdup (method);
}

static void
pk_backend_config_set_dbpath (PkBackendConfig *config, const gchar *path)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (path != NULL);

	g_free (config->dbpath);
	config->dbpath = g_strdup (path);
}

static void
pk_backend_config_set_gpgdir (PkBackendConfig *config, const gchar *path)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (path != NULL);

	g_free (config->gpgdir);
	config->gpgdir = g_strdup (path);
}


static void
pk_backend_config_set_logfile (PkBackendConfig *config, const gchar *filename)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (filename != NULL);

	g_free (config->logfile);
	config->logfile = g_strdup (filename);
}

static void
pk_backend_config_set_root (PkBackendConfig *config, const gchar *path)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (path != NULL);

	g_free (config->root);
	config->root = g_strdup (path);
}

static void
pk_backend_config_set_xfercmd (PkBackendConfig *config, const gchar *command)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (command != NULL);

	g_free (config->xfercmd);
	config->xfercmd = g_strdup (command);
}

typedef struct
{
	 const gchar	*name;
	 void		(*func) (PkBackendConfig *config, const gchar *s);
} PkBackendConfigString;

/* keep this in alphabetical order */
static const PkBackendConfigString pk_backend_config_string_options[] = {
	{ "Architecture", pk_backend_config_set_arch },
	{ "CacheDir", pk_backend_config_add_cachedir },
	{ "CleanMethod", pk_backend_config_set_cleanmethod },
	{ "DBPath", pk_backend_config_set_dbpath },
	{ "GPGDir", pk_backend_config_set_gpgdir },
	{ "LogFile", pk_backend_config_set_logfile },
	{ "RootDir", pk_backend_config_set_root },
	{ "XferCommand", pk_backend_config_set_xfercmd },
	{ NULL, NULL }
};

static gboolean
pk_backend_config_set_string (PkBackendConfig *config, const gchar *option,
			      const gchar *s)
{
	gsize i;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (option != NULL, FALSE);
	g_return_val_if_fail (s != NULL, FALSE);

	for (i = 0;; ++i) {
		const gchar *name = pk_backend_config_string_options[i].name;
		gint cmp = g_strcmp0 (option, name);

		if (name == NULL || cmp < 0) {
			return FALSE;
		} else if (cmp == 0) {
			pk_backend_config_string_options[i].func (config, s);
			return TRUE;
		}
	}
}

static void
pk_backend_config_add_holdpkg (PkBackendConfig *config, gchar *package)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (package != NULL);

	config->holdpkgs = alpm_list_add (config->holdpkgs, package);
}

static void
pk_backend_config_add_ignoregroup (PkBackendConfig *config, gchar *group)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (group != NULL);

	config->ignoregroups = alpm_list_add (config->ignoregroups, group);
}

static void
pk_backend_config_add_ignorepkg (PkBackendConfig *config, gchar *package)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (package != NULL);

	config->ignorepkgs = alpm_list_add (config->ignorepkgs, package);
}

static void
pk_backend_config_add_noextract (PkBackendConfig *config, gchar *filename)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (filename != NULL);

	config->noextracts = alpm_list_add (config->noextracts, filename);
}

static void
pk_backend_config_add_noupgrade (PkBackendConfig *config, gchar *filename)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (filename != NULL);

	config->noupgrades = alpm_list_add (config->noupgrades, filename);
}

static void
pk_backend_config_add_syncfirst (PkBackendConfig *config, gchar *package)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (package != NULL);

	config->syncfirsts = alpm_list_add (config->syncfirsts, package);
}

typedef struct
{
	const gchar	*name;
	void		(*func) (PkBackendConfig *config, gchar *value);
} PkBackendConfigList;

/* keep this in alphabetical order */
static const PkBackendConfigList pk_backend_config_list_options[] = {
	{ "HoldPkg", pk_backend_config_add_holdpkg },
	{ "IgnoreGroup", pk_backend_config_add_ignoregroup },
	{ "IgnorePkg", pk_backend_config_add_ignorepkg },
	{ "NoExtract", pk_backend_config_add_noextract },
	{ "NoUpgrade", pk_backend_config_add_noupgrade },
	{ "SyncFirst", pk_backend_config_add_syncfirst },
	{ NULL, NULL }
};

static void
pk_backend_config_list_add (PkBackendConfig *config, gsize option,
			    const gchar *list)
{
	gchar *str;

	for (str = strchr (list, ' '); str != NULL; str = strchr (list, ' ')) {
		/* allocate normally */
		gchar *value = malloc ((++str - list) * sizeof (gchar));
		g_strlcpy (value, list, str - list);
		pk_backend_config_list_options[option].func (config, value);
		list = str;
	}
	pk_backend_config_list_options[option].func (config, strdup (list));
}

static gboolean
pk_backend_config_set_list (PkBackendConfig *config, const gchar *option,
			    const gchar *list)
{
	gsize i;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (option != NULL, FALSE);
	g_return_val_if_fail (list != NULL, FALSE);

	for (i = 0;; ++i) {
		const gchar *name = pk_backend_config_list_options[i].name;
		gint cmp = g_strcmp0 (option, name);

		if (name == NULL || cmp < 0) {
			return FALSE;
		} else if (cmp == 0) {
			pk_backend_config_list_add (config, i, list);
			return TRUE;
		}
	}
}

static void
pk_backend_config_add_repo (PkBackendConfig *config, const gchar *repo)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (repo != NULL);

	if (alpm_list_find_str (config->repos, repo) == NULL) {
		config->repos = alpm_list_add (config->repos, g_strdup (repo));
	}
}

static gboolean
pk_backend_config_repo_add_server (PkBackendConfig *config, const gchar *repo,
				   const gchar *value, GError **e)
{
	alpm_list_t *list;
	gchar *url;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (repo != NULL, FALSE);
	g_return_val_if_fail (alpm_list_find_str (config->repos, repo) != NULL,
			      FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	url = g_regex_replace_literal (config->xrepo, value, -1, 0, repo, 0, e);
	if (url == NULL) {
		return FALSE;
	}

	if (config->arch != NULL) {
		gchar *temp = url;
		url = g_regex_replace_literal (config->xarch, temp, -1, 0,
					       config->arch, 0, e);
		g_free (temp);

		if (url == NULL) {
			return FALSE;
		}
	} else if (strstr (url, "$arch") != NULL) {
		g_set_error (e, ALPM_ERROR, ALPM_ERR_CONFIG_INVALID,
			     "url contained $arch, which is not set");
	}

	list = (alpm_list_t *) g_hash_table_lookup (config->servers, repo);
	list = alpm_list_add (list, url);
	g_hash_table_insert (config->servers, g_strdup (repo), list);

	return TRUE;
}

static gboolean
pk_backend_config_set_siglevel (PkBackendConfig *config, const gchar *section,
				const gchar *list, GError **error)
{
	alpm_siglevel_t *level;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (section != NULL, FALSE);
	g_return_val_if_fail (list != NULL, FALSE);

	level = g_hash_table_lookup (config->levels, section);
	if (level == NULL) {
		level = g_hash_table_lookup (config->levels, "options");
		level = g_memdup (level, sizeof (alpm_siglevel_t));
		g_hash_table_insert (config->levels, g_strdup (section), level);
	}

	while (TRUE) {
		gboolean package = TRUE, database = TRUE;

		if (g_str_has_prefix (list, "Package")) {
			database = FALSE;
			list += 7;
		} else if (g_str_has_prefix (list, "Database")) {
			package = FALSE;
			list += 8;
		}

		/* this also allows e.g. NeverEver, so put prefixes last */
		if (g_str_has_prefix (list, "Never")) {
			if (package) {
				*level &= ~ALPM_SIG_PACKAGE;
			}
			if (database) {
				*level &= ~ALPM_SIG_DATABASE;
			}
		} else if (g_str_has_prefix (list, "Optional")) {
			if (package) {
				*level |= ALPM_SIG_PACKAGE;
				*level |= ALPM_SIG_PACKAGE_OPTIONAL;
			}
			if (database) {
				*level |= ALPM_SIG_DATABASE;
				*level |= ALPM_SIG_DATABASE_OPTIONAL;
			}
		} else if (g_str_has_prefix (list, "Required")) {
			if (package) {
				*level |= ALPM_SIG_PACKAGE;
				*level &= ~ALPM_SIG_PACKAGE_OPTIONAL;
			}
			if (database) {
				*level |= ALPM_SIG_DATABASE;
				*level &= ~ALPM_SIG_DATABASE_OPTIONAL;
			}
		} else if (g_str_has_prefix (list, "TrustedOnly")) {
			if (package) {
				*level &= ~ALPM_SIG_PACKAGE_MARGINAL_OK;
				*level &= ~ALPM_SIG_PACKAGE_UNKNOWN_OK;
			}
			if (database) {
				*level &= ~ALPM_SIG_DATABASE_MARGINAL_OK;
				*level &= ~ALPM_SIG_DATABASE_UNKNOWN_OK;
			}
		} else if (g_str_has_prefix (list, "TrustAll")) {
			if (package) {
				*level |= ALPM_SIG_PACKAGE_MARGINAL_OK;
				*level |= ALPM_SIG_PACKAGE_UNKNOWN_OK;
			}
			if (database) {
				*level |= ALPM_SIG_DATABASE_MARGINAL_OK;
				*level |= ALPM_SIG_DATABASE_UNKNOWN_OK;
			}
		} else {
			g_set_error (error, ALPM_ERROR, ALPM_ERR_CONFIG_INVALID,
				     "invalid SigLevel value: %s", list);
			return FALSE;
		}

		list = strchr (list, ' ');
		if (list == NULL) {
			break;
		} else {
			++list;
		}
	}

	return TRUE;
}

static gboolean
pk_backend_config_parse (PkBackendConfig *config, const gchar *filename,
			 gchar *section, GError **error)
{
	GFile *file;
	GFileInputStream *is;
	GDataInputStream *input;

	gchar *key, *str, *line = NULL;
	guint num = 1;

	GError *e = NULL;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	file = g_file_new_for_path (filename);
	is = g_file_read (file, NULL, &e);

	if (is == NULL) {
		g_propagate_error (error, e);
		g_object_unref (file);
		return FALSE;
	}

	input = g_data_input_stream_new (G_INPUT_STREAM (is));
	section = g_strdup (section);

	for (;; g_free (line), ++num) {
		line = g_data_input_stream_read_line (input, NULL, NULL, &e);

		if (line != NULL) {
			g_strstrip (line);
		} else {
			break;
		}

		/* skip empty lines */
		if (*line == '\0' || *line == '#') {
			continue;
		}

		/* remove trailing comments */
		for (str = line; *str != '\0' && *str != '#'; ++str);
		*str-- = '\0';

		/* change sections */
		if (*line == '[' && *str == ']') {
			*str = '\0';
			str = line + 1;

			if (*str == '\0') {
				g_set_error (&e, ALPM_ERROR,
					     ALPM_ERR_CONFIG_INVALID,
					     "empty section name");
				break;
			}

			g_free (section);
			section = g_strdup (str);

			if (g_strcmp0 (section, "options") != 0) {
				pk_backend_config_add_repo (config, section);
			}

			continue;
		}

		/* parse a directive */
		if (section == NULL) {
			g_set_error (&e, ALPM_ERROR, ALPM_ERR_CONFIG_INVALID,
				     "directive must belong to a section");
			break;
		}

		str = line;
		key = strsep (&str, "=");
		g_strchomp (key);
		if (str != NULL) {
			g_strchug (str);
		}

		if (str == NULL) {
			/* set a boolean directive */
			if (g_strcmp0 (section, "options") == 0 &&
			    pk_backend_config_set_boolean (config, key)) {
				continue;
			}
			/* report error below */
		} else if (g_strcmp0 (key, "Include") == 0) {
			gsize i;
			glob_t match = { 0 };

			/* ignore globbing errors */
			if (glob (str, GLOB_NOCHECK, NULL, &match) != 0) {
				continue;
			}

			/* parse the files that matched */
			for (i = 0; i < match.gl_pathc; ++i) {
				if (!pk_backend_config_parse (config,
							      match.gl_pathv[i],
							      section, &e)) {
					break;
				}
			}

			globfree (&match);
			if (e != NULL) {
				break;
			} else {
				continue;
			}
		} else if (g_strcmp0 (section, "options") == 0) {
			/* set a string or list directive */
			if (pk_backend_config_set_string (config, key, str) ||
			    pk_backend_config_set_list (config, key, str)) {
				continue;
			}
			/* report error below */
		} else if (g_strcmp0 (key, "Server") == 0) {
			if (!pk_backend_config_repo_add_server (config, section,
								str, &e)) {
				break;
			} else {
				continue;
			}
		}
	
		if (g_strcmp0 (key, "SigLevel") == 0 && str != NULL) {
			if (!pk_backend_config_set_siglevel (config, section,
							     str, &e)) {
				break;
			} else {
				continue;
			}
		}

		/* report errors from above */
		g_set_error (&e, ALPM_ERROR, ALPM_ERR_CONFIG_INVALID,
			     "unrecognised directive '%s'", key);
		break;
	}

	g_free (section);

	g_object_unref (input);
	g_object_unref (is);
	g_object_unref (file);

	if (e != NULL) {
		g_propagate_prefixed_error (error, e, "%s:%u", filename, num);
		return FALSE;
	} else {
		return TRUE;
	}
}

static alpm_handle_t *
pk_backend_config_initialize_alpm (PkBackendConfig *config, GError **error)
{
	alpm_handle_t *handle;
	enum _alpm_errno_t errno;
	gsize dir = 1;

	g_return_val_if_fail (config != NULL, FALSE);

	if (config->root == NULL || *config->root == '\0') {
		g_free (config->root);
		config->root = g_strdup (PK_BACKEND_DEFAULT_ROOT);
	} else if (!g_str_has_suffix (config->root, G_DIR_SEPARATOR_S)) {
		dir = 0;
	}

	if (config->dbpath == NULL) {
		config->dbpath = g_strconcat (config->root,
					      PK_BACKEND_DEFAULT_DBPATH + dir,
					      NULL);
	}

	g_debug ("initializing alpm");
	handle = alpm_initialize (config->root, config->dbpath, &errno);
	if (handle == NULL) {
		g_set_error_literal (error, ALPM_ERROR, errno,
				     alpm_strerror (errno));
		return handle;
	}

	if (config->gpgdir == NULL) {
		config->gpgdir = g_strconcat (config->root,
					      PK_BACKEND_DEFAULT_GPGDIR + dir,
					      NULL);
	}

	if (alpm_option_set_gpgdir (handle, config->gpgdir) < 0) {
		errno = alpm_errno (handle);
		g_set_error (error, ALPM_ERROR, errno, "GPGDir: %s",
			     alpm_strerror (errno));
		return handle;
	}

	if (config->logfile == NULL) {
		config->logfile = g_strconcat (config->root,
					       PK_BACKEND_DEFAULT_LOGFILE + dir,
					       NULL);
	}

	if (alpm_option_set_logfile (handle, config->logfile) < 0) {
		errno = alpm_errno (handle);
		g_set_error (error, ALPM_ERROR, errno, "LogFile: %s",
			     alpm_strerror (errno));
		return handle;
	}

	if (config->cachedirs == NULL) {
		gchar *path = g_strconcat (config->root,
					   PK_BACKEND_DEFAULT_CACHEDIR + dir,
					   NULL);
		config->cachedirs = alpm_list_add (NULL, path);
	}

	/* alpm takes ownership */
	if (alpm_option_set_cachedirs (handle, config->cachedirs) < 0) {
		errno = alpm_errno (handle);
		g_set_error (error, ALPM_ERROR, errno, "CacheDir: %s",
			     alpm_strerror (errno));
		return handle;
	}
	config->cachedirs = NULL;

	return handle;
}

static alpm_handle_t *
pk_backend_config_configure_alpm (PkBackendConfig *config, GError **error)
{
	alpm_handle_t *handle;
	alpm_siglevel_t *level;

	g_return_val_if_fail (config != NULL, FALSE);

	handle = pk_backend_config_initialize_alpm (config, error);
	if (handle == NULL) {
		return NULL;
	}

	alpm_option_set_checkspace (handle, config->checkspace);
	alpm_option_set_usedelta (handle, config->usedelta);
	alpm_option_set_usesyslog (handle, config->usesyslog);
	alpm_option_set_arch (handle, config->arch);

	level = g_hash_table_lookup (config->levels, "options");
	alpm_option_set_default_siglevel (handle, *level);

	/* backend takes ownership */
	g_free (xfercmd);
	xfercmd = config->xfercmd;
	config->xfercmd = NULL;

	if (xfercmd != NULL) {
		alpm_option_set_fetchcb (handle, pk_backend_fetchcb);
	} else {
		alpm_option_set_fetchcb (handle, NULL);
	}

	/* backend takes ownership */
	FREELIST (holdpkgs);
	holdpkgs = config->holdpkgs;
	config->holdpkgs = NULL;

	/* backend takes ownership */
	FREELIST (syncfirsts);
	syncfirsts = config->syncfirsts;
	config->syncfirsts = NULL;

	/* alpm takes ownership */
	alpm_option_set_ignoregroups (handle, config->ignoregroups);
	config->ignoregroups = NULL;

	/* alpm takes ownership */
	alpm_option_set_ignorepkgs (handle, config->ignorepkgs);
	config->ignorepkgs = NULL;

	/* alpm takes ownership */
	alpm_option_set_noextracts (handle, config->noextracts);
	config->noextracts = NULL;

	/* alpm takes ownership */
	alpm_option_set_noupgrades (handle, config->noupgrades);
	config->noupgrades = NULL;

	pk_backend_configure_repos (config->repos, config->servers,
				    config->levels);

	return handle;
}

alpm_handle_t *
pk_backend_configure (const gchar *filename, GError **error)
{
	PkBackendConfig *config;
	alpm_handle_t *handle;
	GError *e = NULL;

	g_return_val_if_fail (filename != NULL, FALSE);

	g_debug ("reading config from %s", filename);
	config = pk_backend_config_new ();

	if (pk_backend_config_parse (config, filename, NULL, &e)) {
		handle = pk_backend_config_configure_alpm (config, &e);
	} else {
		handle = NULL;
	}

	pk_backend_config_free (config);
	if (e != NULL) {
		g_propagate_error (error, e);
		if (handle != NULL) {
			alpm_release (handle);
		}
		return NULL;
	} else {
		return handle;
	}
}
