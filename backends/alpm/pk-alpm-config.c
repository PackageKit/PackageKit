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
#include <glib/gstdio.h>

#include "pk-backend-alpm.h"
#include "pk-alpm-config.h"
#include "pk-alpm-databases.h"
#include "pk-alpm-error.h"

// bad API choice
static gchar *xfercmd = NULL;

typedef struct
{
	 gboolean		 checkspace, color, ilovecandy, totaldl,
				 usesyslog, verbosepkglists;
	 gdouble		 deltaratio;

	 gchar			*arch, *cleanmethod, *dbpath, *gpgdir, *logfile,
				*root, *xfercmd;

	 alpm_list_t		*cachedirs, *holdpkgs, *ignoregroups,
				*ignorepkgs, *localfilesiglevels, *noextracts,
				*noupgrades, *remotefilesiglevels;

	 alpm_list_t		*sections;
	 GRegex			*xrepo, *xarch;
	 PkBackend		*backend;
} PkAlpmConfig;

typedef struct
{
	 gchar		*name;
	 alpm_list_t	*servers, *siglevels;
} PkAlpmConfigSection;

static PkAlpmConfig *
pk_alpm_config_new (PkBackend *backend)
{
	PkAlpmConfig *config = g_new0 (PkAlpmConfig, 1);
	config->backend = backend;
	config->deltaratio = 0.0;

	config->xrepo = g_regex_new ("\\$repo", 0, 0, NULL);
	config->xarch = g_regex_new ("\\$arch", 0, 0, NULL);

	return config;
}

static void
pk_alpm_config_section_free (gpointer data)
{
	PkAlpmConfigSection *section = data;
	if (section == NULL)
		return;
	g_free (section->name);
	alpm_list_free_inner (section->servers, g_free);
	alpm_list_free (section->servers);
	FREELIST (section->siglevels);
	g_free (section);
}

static void
pk_alpm_config_free (PkAlpmConfig *config)
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
	FREELIST (config->localfilesiglevels);
	FREELIST (config->noextracts);
	FREELIST (config->noupgrades);
	FREELIST (config->remotefilesiglevels);

	alpm_list_free_inner (config->sections, pk_alpm_config_section_free);
	alpm_list_free (config->sections);

	g_regex_unref (config->xrepo);
	g_regex_unref (config->xarch);
}

static void
pk_alpm_config_set_checkspace (PkAlpmConfig *config)
{
	g_return_if_fail (config != NULL);

	config->checkspace = TRUE;
}

static void
pk_alpm_config_set_color (PkAlpmConfig *config)
{
	g_return_if_fail (config != NULL);

	config->color = TRUE;
}

static void
pk_alpm_config_set_ilovecandy (PkAlpmConfig *config)
{
	g_return_if_fail (config != NULL);

	config->ilovecandy = TRUE;
}

static void
pk_alpm_config_set_totaldl (PkAlpmConfig *config)
{
	g_return_if_fail (config != NULL);

	config->totaldl = TRUE;
}

static void
pk_alpm_config_set_usedelta (PkAlpmConfig *config)
{
	g_return_if_fail (config != NULL);

	config->deltaratio = 0.7;
}

static void
pk_alpm_config_set_usesyslog (PkAlpmConfig *config)
{
	g_return_if_fail (config != NULL);

	config->usesyslog = TRUE;
}

static void
pk_alpm_config_set_verbosepkglists (PkAlpmConfig *config)
{
	g_return_if_fail (config != NULL);

	config->verbosepkglists = TRUE;
}

typedef struct
{
	 const gchar	*name;
	 void		(*func) (PkAlpmConfig *config);
} PkAlpmConfigBoolean;

/* keep this in alphabetical order */
static const PkAlpmConfigBoolean pk_alpm_config_boolean_options[] = {
	{ "CheckSpace", pk_alpm_config_set_checkspace },
	{ "Color", pk_alpm_config_set_color },
	{ "ILoveCandy", pk_alpm_config_set_ilovecandy },
	{ "TotalDownload", pk_alpm_config_set_totaldl },
	{ "UseDelta", pk_alpm_config_set_usedelta },
	{ "UseSyslog", pk_alpm_config_set_usesyslog },
	{ "VerbosePkgLists", pk_alpm_config_set_verbosepkglists },
	{ NULL, NULL }
};

static gboolean
pk_alpm_config_set_boolean (PkAlpmConfig *config, const gchar *option)
{
	gsize i;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (option != NULL, FALSE);

	for (i = 0;; ++i) {
		const gchar *name = pk_alpm_config_boolean_options[i].name;
		gint cmp = g_strcmp0 (option, name);

		if (name == NULL || cmp < 0) {
			return FALSE;
		} else if (cmp == 0) {
			pk_alpm_config_boolean_options[i].func (config);
			return TRUE;
		}
	}
}

static void
pk_alpm_config_add_cachedir (PkAlpmConfig *config, const gchar *path)
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
pk_alpm_config_set_arch (PkAlpmConfig *config, const gchar *arch)
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
pk_alpm_config_set_cleanmethod (PkAlpmConfig *config, const gchar *method)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (method != NULL);

	g_free (config->cleanmethod);
	config->cleanmethod = g_strdup (method);
}

static void
pk_alpm_config_set_dbpath (PkAlpmConfig *config, const gchar *path)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (path != NULL);

	g_free (config->dbpath);
	config->dbpath = g_strdup (path);
}

static void
pk_alpm_config_set_gpgdir (PkAlpmConfig *config, const gchar *path)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (path != NULL);

	g_free (config->gpgdir);
	config->gpgdir = g_strdup (path);
}

static void
pk_alpm_config_set_logfile (PkAlpmConfig *config, const gchar *filename)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (filename != NULL);

	g_free (config->logfile);
	config->logfile = g_strdup (filename);
}

static void
pk_alpm_config_set_root (PkAlpmConfig *config, const gchar *path)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (path != NULL);

	g_free (config->root);
	config->root = g_strdup (path);
}

static void
pk_alpm_config_set_deltaratio (PkAlpmConfig *config, const gchar *number)
{
	gdouble ratio;
	gchar *endptr;

	g_return_if_fail (config != NULL);
	g_return_if_fail (number != NULL);

	ratio = g_ascii_strtod (number, &endptr);
	/* this ignores invalid values whereas pacman reports an error */
	if (*endptr == '\0' && 0.0 <= ratio && ratio <= 2.0) {
		config->deltaratio = ratio;
	}
}

static void
pk_alpm_config_set_xfercmd (PkAlpmConfig *config, const gchar *command)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (command != NULL);

	g_free (config->xfercmd);
	config->xfercmd = g_strdup (command);
}

typedef struct
{
	 const gchar	*name;
	 void		(*func) (PkAlpmConfig *config, const gchar *s);
} PkAlpmConfigString;

/* keep this in alphabetical order */
static const PkAlpmConfigString pk_alpm_config_string_options[] = {
	{ "Architecture", pk_alpm_config_set_arch },
	{ "CacheDir", pk_alpm_config_add_cachedir },
	{ "CleanMethod", pk_alpm_config_set_cleanmethod },
	{ "DBPath", pk_alpm_config_set_dbpath },
	{ "GPGDir", pk_alpm_config_set_gpgdir },
	{ "LogFile", pk_alpm_config_set_logfile },
	{ "RootDir", pk_alpm_config_set_root },
	{ "UseDelta", pk_alpm_config_set_deltaratio },
	{ "XferCommand", pk_alpm_config_set_xfercmd },
	{ NULL, NULL }
};

static gboolean
pk_alpm_config_set_string (PkAlpmConfig *config, const gchar *option,
			      const gchar *s)
{
	gsize i;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (option != NULL, FALSE);
	g_return_val_if_fail (s != NULL, FALSE);

	for (i = 0;; ++i) {
		const gchar *name = pk_alpm_config_string_options[i].name;
		gint cmp = g_strcmp0 (option, name);

		if (name == NULL || cmp < 0) {
			return FALSE;
		} else if (cmp == 0) {
			pk_alpm_config_string_options[i].func (config, s);
			return TRUE;
		}
	}
}

typedef struct
{
	const gchar	*name;
	glong		 offset;
} PkAlpmConfigList;

/* keep this in alphabetical order */
static const PkAlpmConfigList pk_alpm_config_list_options[] = {
	{ "HoldPkg", G_STRUCT_OFFSET (PkAlpmConfig, holdpkgs) },
	{ "IgnoreGroup", G_STRUCT_OFFSET (PkAlpmConfig, ignoregroups) },
	{ "IgnorePkg", G_STRUCT_OFFSET (PkAlpmConfig, ignorepkgs) },
	{ "LocalFileSigLevel", G_STRUCT_OFFSET (PkAlpmConfig,
						localfilesiglevels) },
	{ "NoExtract", G_STRUCT_OFFSET (PkAlpmConfig, noextracts) },
	{ "NoUpgrade", G_STRUCT_OFFSET (PkAlpmConfig, noupgrades) },
	{ "RemoteFileSigLevel", G_STRUCT_OFFSET (PkAlpmConfig,
						 remotefilesiglevels) },
	{ NULL, 0 }
};

static alpm_list_t *
pk_alpm_list_add_words (alpm_list_t *list, const gchar *words)
{
	gchar *str;

	while ((str = strchr (words, ' ')) != NULL) {
		/* allocate normally */
		gchar *word = malloc ((++str - words) * sizeof (gchar));
		g_strlcpy (word, words, str - words);
		list = alpm_list_add (list, word);
		words = str;
	}

	return alpm_list_add (list, strdup (words));
}

static gboolean
pk_alpm_config_set_list (PkAlpmConfig *config, const gchar *option,
			    const gchar *words)
{
	gsize i;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (option != NULL, FALSE);
	g_return_val_if_fail (words != NULL, FALSE);

	for (i = 0;; ++i) {
		const gchar *name = pk_alpm_config_list_options[i].name;
		gint cmp = g_strcmp0 (option, name);

		if (name == NULL || cmp < 0) {
			return FALSE;
		} else if (cmp == 0) {
			glong offset = pk_alpm_config_list_options[i].offset;
			alpm_list_t **list = G_STRUCT_MEMBER_P (config, offset);
			*list = pk_alpm_list_add_words (*list, words);
			return TRUE;
		}
	}
}

static gint
pk_alpm_config_section_match (gconstpointer element, gconstpointer name)
{
	const PkAlpmConfigSection *section = element;

	g_return_val_if_fail (section != NULL, -1);

	return g_strcmp0 (section->name, name);
}

static PkAlpmConfigSection *
pk_alpm_config_enter_section (PkAlpmConfig *config, const gchar *name)
{
	PkAlpmConfigSection *section;

	g_return_val_if_fail (config != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	section = alpm_list_find (config->sections, name,
				  pk_alpm_config_section_match);
	if (section != NULL) {
		return section;
	}

	section = g_new0 (PkAlpmConfigSection, 1);
	section->name = g_strdup (name);
	config->sections = alpm_list_add (config->sections, section);
	return section;
}

static gboolean
pk_alpm_config_add_server (PkAlpmConfig *config,
			      PkAlpmConfigSection *section,
			      const gchar *address, GError **e)
{
	_cleanup_free_ gchar *url = NULL;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (section != NULL, FALSE);
	g_return_val_if_fail (address != NULL, FALSE);

	url = g_regex_replace_literal (config->xrepo, address, -1, 0,
				       section->name, 0, e);
	if (url == NULL)
		return FALSE;

	if (config->arch != NULL) {
		_cleanup_free_ gchar *temp = url;
		url = g_regex_replace_literal (config->xarch, temp, -1, 0,
					       config->arch, 0, e);
		if (url == NULL)
			return FALSE;
	} else if (strstr (url, "$arch") != NULL) {
		g_set_error (e, PK_ALPM_ERROR, PK_ALPM_ERR_CONFIG_INVALID,
			     "url contained $arch, which is not set");
		return FALSE;
	}

	section->servers = alpm_list_add (section->servers, g_strdup (url));
	return TRUE;
}

static void
pk_alpm_config_add_siglevel (PkAlpmConfig *config,
				PkAlpmConfigSection *section,
				const gchar *words)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (section != NULL);
	g_return_if_fail (words != NULL);

	section->siglevels = pk_alpm_list_add_words (section->siglevels, words);
}

static gboolean
pk_alpm_config_parse (PkAlpmConfig *config, const gchar *filename,
			 PkAlpmConfigSection *section, GError **error)
{
	_cleanup_object_unref_ GFile *file = NULL;
	_cleanup_object_unref_ GFileInputStream *is = NULL;
	_cleanup_object_unref_ GDataInputStream *input = NULL;
	gchar *key, *str, *line = NULL;
	guint num = 1;
	GError *e = NULL;

	g_return_val_if_fail (config != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	file = g_file_new_for_path (filename);
	is = g_file_read (file, NULL, &e);
	if (is == NULL) {
		g_propagate_error (error, e);
		return FALSE;
	}

	input = g_data_input_stream_new (G_INPUT_STREAM (is));

	for (;; g_free (line), ++num) {
		line = g_data_input_stream_read_line (input, NULL, NULL, &e);

		if (line == NULL)
			break;
		/* skip empty lines */
		g_strstrip (line);
		if (*line == '\0' || *line == '#')
			continue;

		/* remove trailing comments */
		for (str = line; *str != '\0' && *str != '#'; ++str);
		*str-- = '\0';

		/* change sections */
		if (*line == '[' && *str == ']') {
			*str = '\0';
			str = line + 1;

			if (*str == '\0') {
				g_set_error (&e, PK_ALPM_ERROR,
					     PK_ALPM_ERR_CONFIG_INVALID,
					     "empty section name");
				break;
			}

			section = pk_alpm_config_enter_section (config, str);
			continue;
		}

		/* parse a directive */
		if (section == NULL) {
			g_set_error (&e, PK_ALPM_ERROR, PK_ALPM_ERR_CONFIG_INVALID,
				     "directive must belong to a section");
			break;
		}

		str = line;
		key = strsep (&str, "=");
		g_strchomp (key);
		if (str != NULL)
			g_strchug (str);

		if (str == NULL) {
			/* set a boolean directive */
			if (pk_alpm_config_section_match (section,
							     "options") == 0 &&
			    pk_alpm_config_set_boolean (config, key)) {
				continue;
			}
			/* report error below */
		} else if (g_strcmp0 (key, "Include") == 0) {
			gsize i;
			glob_t match = { 0 };

			/* ignore globbing errors */
			if (glob (str, GLOB_NOCHECK, NULL, &match) != 0)
				continue;

			/* parse the files that matched */
			for (i = 0; i < match.gl_pathc; ++i) {
				if (!pk_alpm_config_parse (config,
							      match.gl_pathv[i],
							      section, &e)) {
					break;
				}
			}

			globfree (&match);
			if (e == NULL)
				continue;
			break;
		} else if (pk_alpm_config_section_match (section,
							    "options") == 0) {
			/* set a string or list directive */
			if (pk_alpm_config_set_string (config, key, str) ||
			    pk_alpm_config_set_list (config, key, str)) {
				continue;
			}
			/* report error below */
		} else if (g_strcmp0 (key, "Server") == 0) {
			if (!pk_alpm_config_add_server (config, section,
							   str, &e)) {
				break;
			}
			continue;
		}

		if (g_strcmp0 (key, "SigLevel") == 0 && str != NULL) {
			pk_alpm_config_add_siglevel (config, section, str);
			continue;
		}

		/* report errors from above */
		g_set_error (&e, PK_ALPM_ERROR, PK_ALPM_ERR_CONFIG_INVALID,
			     "unrecognised directive '%s'", key);
		break;
	}

	if (e != NULL) {
		g_propagate_prefixed_error (error, e, "%s:%u", filename, num);
		return FALSE;
	} else
		return TRUE;
}

static alpm_handle_t *
pk_alpm_config_initialize_alpm (PkAlpmConfig *config, GError **error)
{
	alpm_handle_t *handle;
	alpm_errno_t errno;
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
		g_set_error_literal (error, PK_ALPM_ERROR, errno,
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
		g_set_error (error, PK_ALPM_ERROR, errno, "GPGDir: %s",
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
		g_set_error (error, PK_ALPM_ERROR, errno, "LogFile: %s",
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
		g_set_error (error, PK_ALPM_ERROR, errno, "CacheDir: %s",
			     alpm_strerror (errno));
		return handle;
	}
	config->cachedirs = NULL;

	return handle;
}

static alpm_siglevel_t
pk_alpm_siglevel_parse (alpm_siglevel_t base, const alpm_list_t *list, GError **error)
{
	for (; list != NULL; list = list->next) {
		gboolean package = TRUE, database = TRUE;
		const gchar *level = (const gchar *) list->data;

		if (g_str_has_prefix (level, "Package")) {
			database = FALSE;
			level += 7;
		} else if (g_str_has_prefix (level, "Database")) {
			package = FALSE;
			level += 8;
		}

		if (g_strcmp0 (level, "Never") == 0) {
			if (package) {
				base &= ~ALPM_SIG_PACKAGE;
				base |= ALPM_SIG_PACKAGE_SET;
			}
			if (database) {
				base &= ~ALPM_SIG_DATABASE;
			}
		} else if (g_strcmp0 (level, "Optional") == 0) {
			if (package) {
				base |= ALPM_SIG_PACKAGE;
				base |= ALPM_SIG_PACKAGE_OPTIONAL;
				base |= ALPM_SIG_PACKAGE_SET;
			}
			if (database) {
				base |= ALPM_SIG_DATABASE;
				base |= ALPM_SIG_DATABASE_OPTIONAL;
			}
		} else if (g_strcmp0 (level, "Required") == 0) {
			if (package) {
				base |= ALPM_SIG_PACKAGE;
				base &= ~ALPM_SIG_PACKAGE_OPTIONAL;
				base |= ALPM_SIG_PACKAGE_SET;
			}
			if (database) {
				base |= ALPM_SIG_DATABASE;
				base &= ~ALPM_SIG_DATABASE_OPTIONAL;
			}
		} else if (g_strcmp0 (level, "TrustedOnly") == 0) {
			if (package) {
				base &= ~ALPM_SIG_PACKAGE_MARGINAL_OK;
				base &= ~ALPM_SIG_PACKAGE_UNKNOWN_OK;
				base |= ALPM_SIG_PACKAGE_TRUST_SET;
			}
			if (database) {
				base &= ~ALPM_SIG_DATABASE_MARGINAL_OK;
				base &= ~ALPM_SIG_DATABASE_UNKNOWN_OK;
			}
		} else if (g_strcmp0 (level, "TrustAll") == 0) {
			if (package) {
				base |= ALPM_SIG_PACKAGE_MARGINAL_OK;
				base |= ALPM_SIG_PACKAGE_UNKNOWN_OK;
				base |= ALPM_SIG_PACKAGE_TRUST_SET;
			}
			if (database) {
				base |= ALPM_SIG_DATABASE_MARGINAL_OK;
				base |= ALPM_SIG_DATABASE_UNKNOWN_OK;
			}
		} else {
			g_set_error (error, PK_ALPM_ERROR, PK_ALPM_ERR_CONFIG_INVALID,
				     "invalid SigLevel value: %s", level);
			return ALPM_SIG_USE_DEFAULT;
		}
	}

	return base;
}

static alpm_siglevel_t
pk_alpm_siglevel_cross (alpm_siglevel_t base, const alpm_list_t *list, GError **error)
{
	alpm_siglevel_t level;

	if (list == NULL)
		return base;

	level = pk_alpm_siglevel_parse (0, list, error);
	if (level == ALPM_SIG_USE_DEFAULT)
		return level;

	/* based on unexplained code in pacman */
	if ((level & ALPM_SIG_PACKAGE_SET) == 0)
		level |= base & (ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL);
	if ((level & ALPM_SIG_PACKAGE_TRUST_SET) == 0) {
		level |= base & (ALPM_SIG_PACKAGE_MARGINAL_OK |
				 ALPM_SIG_PACKAGE_UNKNOWN_OK);
	}

	return level;
}

static gboolean
pk_alpm_config_configure_repos (PkAlpmConfig *config,
				   alpm_handle_t *handle, GError **error)
{
	alpm_siglevel_t base, local, remote;
	const alpm_list_t *i;
	PkAlpmConfigSection *options;

	g_return_val_if_fail (config != NULL, FALSE);

	base = ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL |
	       ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL;

	i = config->sections;
	options = i->data;

	base = pk_alpm_siglevel_parse (base, options->siglevels, error);
	if (base == ALPM_SIG_USE_DEFAULT)
		return FALSE;

	local = pk_alpm_siglevel_cross (base, config->localfilesiglevels, error);
	if (local == ALPM_SIG_USE_DEFAULT)
		return FALSE;

	remote = pk_alpm_siglevel_cross (base, config->remotefilesiglevels, error);
	if (remote == ALPM_SIG_USE_DEFAULT)
		return FALSE;

	alpm_option_set_default_siglevel (handle, base);
	alpm_option_set_local_file_siglevel (handle, local);
	alpm_option_set_remote_file_siglevel (handle, remote);

	while ((i = i->next) != NULL) {
		PkAlpmConfigSection *repo = i->data;
		alpm_siglevel_t level;

		level = pk_alpm_siglevel_parse (base, repo->siglevels, error);
		if (level == ALPM_SIG_USE_DEFAULT)
			return FALSE;
		pk_alpm_add_database (repo->name, repo->servers, level);
	}

	return TRUE;
}

static gboolean
pk_alpm_spawn (const gchar *command)
{
	int status;
	_cleanup_error_free_ GError *error = NULL;

	g_return_val_if_fail (command != NULL, FALSE);

	if (!g_spawn_command_line_sync (command, NULL, NULL, &status, &error)) {
		g_warning ("could not spawn command: %s", error->message);
		return FALSE;
	}

	if (WIFEXITED (status) == 0) {
		g_warning ("command did not execute correctly");
		return FALSE;
	}

	if (WEXITSTATUS (status) != EXIT_SUCCESS) {
		gint code = WEXITSTATUS (status);
		g_warning ("command returned error code %d", code);
		return FALSE;
	}

	return TRUE;
}

static gint
pk_alpm_fetchcb (const gchar *url, const gchar *path, gint force)
{
	GRegex *xo, *xi;
	gint result = 0;
	_cleanup_free_ gchar *basename = NULL;
	_cleanup_free_ gchar *file = NULL;
	_cleanup_free_ gchar *finalcmd = NULL;
	_cleanup_free_ gchar *oldpwd = NULL;
	_cleanup_free_ gchar *part = NULL;
	_cleanup_free_ gchar *tempcmd = NULL;

	g_return_val_if_fail (url != NULL, -1);
	g_return_val_if_fail (path != NULL, -1);
	g_return_val_if_fail (xfercmd != NULL, -1);

	oldpwd = g_get_current_dir ();
	if (g_chdir (path) < 0) {
		g_warning ("could not find or read directory '%s'", path);
		g_free (oldpwd);
		return -1;
	}

	xo = g_regex_new ("%o", 0, 0, NULL);
	xi = g_regex_new ("%u", 0, 0, NULL);

	basename = g_path_get_basename (url);
	file = g_strconcat (path, basename, NULL);
	part = g_strconcat (file, ".part", NULL);

	if (force != 0 && g_file_test (part, G_FILE_TEST_EXISTS))
		g_unlink (part);
	if (force != 0 && g_file_test (file, G_FILE_TEST_EXISTS))
		g_unlink (file);

	tempcmd = g_regex_replace_literal (xo, xfercmd, -1, 0, part, 0, NULL);
	if (tempcmd == NULL) {
		result = -1;
		goto out;
	}

	finalcmd = g_regex_replace_literal (xi, tempcmd, -1, 0, url, 0, NULL);
	if (finalcmd == NULL) {
		result = -1;
		goto out;
	}

	if (!pk_alpm_spawn (finalcmd)) {
		result = -1;
		goto out;
	}
	if (g_strrstr (xfercmd, "%o") != NULL) {
		/* using .part filename */
		if (g_rename (part, file) < 0) {
			g_warning ("could not rename %s", part);
			result = -1;
			goto out;
		}
	}
out:
	g_regex_unref (xi);
	g_regex_unref (xo);

	g_chdir (oldpwd);

	return result;
}

static alpm_handle_t *
pk_alpm_config_configure_alpm (PkAlpmConfig *config, GError **error)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (config->backend);
	alpm_handle_t *handle;

	g_return_val_if_fail (config != NULL, FALSE);

	handle = pk_alpm_config_initialize_alpm (config, error);
	if (handle == NULL)
		return NULL;

	alpm_option_set_checkspace (handle, config->checkspace);
	alpm_option_set_usesyslog (handle, config->usesyslog);
	alpm_option_set_arch (handle, config->arch);
	alpm_option_set_deltaratio (handle, config->deltaratio);

	/* backend takes ownership */
	g_free (xfercmd);
	xfercmd = config->xfercmd;
	config->xfercmd = NULL;

	if (xfercmd != NULL) {
		alpm_option_set_fetchcb (handle, pk_alpm_fetchcb);
	} else {
		alpm_option_set_fetchcb (handle, NULL);
	}

	/* backend takes ownership */
	FREELIST (priv->holdpkgs);
	priv->holdpkgs = config->holdpkgs;
	config->holdpkgs = NULL;

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

	pk_alpm_config_configure_repos (config, handle, error);

	return handle;
}

alpm_handle_t *
pk_alpm_configure (PkBackend *backend, const gchar *filename, GError **error)
{
	PkAlpmConfig *config;
	alpm_handle_t *handle = NULL;
	GError *e = NULL;

	g_return_val_if_fail (filename != NULL, FALSE);

	g_debug ("reading config from %s", filename);
	config = pk_alpm_config_new (backend);
	pk_alpm_config_enter_section (config, "options");

	if (pk_alpm_config_parse (config, filename, NULL, &e))
		handle = pk_alpm_config_configure_alpm (config, &e);

	pk_alpm_config_free (config);
	if (e != NULL) {
		g_propagate_error (error, e);
		if (handle != NULL)
			alpm_release (handle);
		return NULL;
	}
	return handle;
}
