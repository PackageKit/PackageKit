/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Anders F Bjorklund <afb@users.sourceforge.net>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _GNU_SOURCE

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include <pk-backend.h>

#include <slapt.h>

#include <openssl/md5.h>

struct category_map {
    const char *category;
    PkGroupEnum group;
};

static struct category_map CATGROUP[] = {
/* Slackware */
{          "a", PK_GROUP_ENUM_SYSTEM /* The base Slackware system. */ },
{         "ap", PK_GROUP_ENUM_OTHER /* Linux applications. */ },
{          "d", PK_GROUP_ENUM_PROGRAMMING /* Program development tools. */ },
{          "e", PK_GROUP_ENUM_OTHER /* GNU Emacs. */ },
{          "f", PK_GROUP_ENUM_DOCUMENTATION /* FAQs and HOWTOs for common tasks. */ },
{          "k", PK_GROUP_ENUM_OTHER /* Linux kernel source. */ },
{        "kde", PK_GROUP_ENUM_DESKTOP_KDE /* The K Desktop Environment and applications. */ },
{       "kdei", PK_GROUP_ENUM_LOCALIZATION /* Language support for the K Desktop Environment. */ },
{          "l", PK_GROUP_ENUM_SYSTEM /* System libraries. */ },
{          "n", PK_GROUP_ENUM_NETWORK /* Networking applications and utilities. */ },
{          "t", PK_GROUP_ENUM_OTHER /* TeX typesetting language. */ },
{        "tcl", PK_GROUP_ENUM_OTHER /* Tcl/Tk/TclX scripting languages and tools. */ },
{          "x", PK_GROUP_ENUM_SYSTEM /* X Window System graphical user interface. */ },
{        "xap", PK_GROUP_ENUM_DESKTOP_OTHER /* Applications for the X Window System. */ },
{          "y", PK_GROUP_ENUM_GAMES /* Classic text-based BSD games. */ },

{        "gsb", PK_GROUP_ENUM_DESKTOP_GNOME /* GNOME SlackBuild */ },
/* Vector */
{  "base-apps", PK_GROUP_ENUM_OTHER },
{       "base", PK_GROUP_ENUM_SYSTEM },
{        "dev", PK_GROUP_ENUM_PROGRAMMING },
{    "drivers", PK_GROUP_ENUM_OTHER },
{  "emulators", PK_GROUP_ENUM_OTHER },
{      "fonts", PK_GROUP_ENUM_FONTS },
{      "games", PK_GROUP_ENUM_GAMES },
{        "kde", PK_GROUP_ENUM_DESKTOP_KDE },
{       "kdei", PK_GROUP_ENUM_LOCALIZATION },
{       "libs", PK_GROUP_ENUM_SYSTEM },
{        "net", PK_GROUP_ENUM_NETWORK },
{     "x-apps", PK_GROUP_ENUM_DESKTOP_OTHER },
{      "x-dev", PK_GROUP_ENUM_PROGRAMMING },
{          "x", PK_GROUP_ENUM_OTHER },
{       "xfce", PK_GROUP_ENUM_DESKTOP_XFCE },
/* Wolvix */
{     "compiz", PK_GROUP_ENUM_OTHER },
{    "desktop", PK_GROUP_ENUM_DESKTOP_OTHER },
{"development", PK_GROUP_ENUM_PROGRAMMING },
{    "drivers", PK_GROUP_ENUM_OTHER },
{      "games", PK_GROUP_ENUM_GAMES },
{      "gnome", PK_GROUP_ENUM_DESKTOP_GNOME },
{   "graphics", PK_GROUP_ENUM_GRAPHICS },
{     "kernel", PK_GROUP_ENUM_OTHER },
{       "lxde", PK_GROUP_ENUM_DESKTOP_OTHER },
{       "meta", PK_GROUP_ENUM_COLLECTIONS },
{ "multimedia", PK_GROUP_ENUM_MULTIMEDIA },
{    "network", PK_GROUP_ENUM_NETWORK },
{     "office", PK_GROUP_ENUM_OFFICE },
{ "scientific", PK_GROUP_ENUM_SCIENCE },
{     "system", PK_GROUP_ENUM_SYSTEM },
{  "utilities", PK_GROUP_ENUM_OTHER },
{     "wolvix", PK_GROUP_ENUM_VENDOR },
{ "xfce-extra", PK_GROUP_ENUM_DESKTOP_XFCE },
{       "xfce", PK_GROUP_ENUM_DESKTOP_XFCE },
/* Sentinel */
{         NULL, PK_GROUP_ENUM_UNKNOWN }
};
static GHashTable *_cathash = NULL;

static PkBackend *_backend = NULL;

static slapt_rc_config *_config = NULL;
static const gchar *_config_file = "/etc/slapt-get/slapt-getrc";

/* prototypes */
static void _show_transaction(PkBackend *backend, slapt_transaction_t *transaction);
static void _install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids, gboolean simulate);
static void _update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids, gboolean simulate);
static void _remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove, gboolean simulate);

/* CURLOPT_PROGRESSFUNCTION */
static int backend_progress_callback(void *clientp,
double dltotal, double dlnow, double ultotal, double ulnow)
{
	unsigned int percentage;
	struct slapt_progress_data *cb_data = clientp;

	(void) cb_data; /* unused */

	if (dltotal == 0)
	    percentage = 0;
	else
	    percentage = (dlnow * 100.0) / dltotal;

	pk_backend_set_percentage (_backend, percentage);
	return 0;
}

/**
 * pk_backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_initialize (PkBackend *backend)
{
	struct category_map *catgroup;

	_config = slapt_read_rc_config(_config_file);
	if (_config == NULL)
	    _config = slapt_init_config();

	_cathash = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
	for (catgroup = CATGROUP; catgroup->category != NULL; catgroup++) {
	     g_hash_table_insert(_cathash,
	         (gpointer) catgroup->category, (gpointer) catgroup->group);
	}

	_backend = backend;
	if (_backend != NULL)
	    _config->progress_cb = &backend_progress_callback;

	chdir(_config->working_dir);
}

/**
 * pk_backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_destroy (PkBackend *backend)
{
	slapt_free_rc_config(_config);
	g_hash_table_destroy(_cathash);
}

/**
 * pk_backend_get_groups:
 */
PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		/*	PK_GROUP_ENUM_ACCESSIBILITY, */
		/*	PK_GROUP_ENUM_ACCESSORIES, */
		/*	PK_GROUP_ENUM_ADMIN_TOOLS, */
		/*	PK_GROUP_ENUM_COMMUNICATION, */
			PK_GROUP_ENUM_DESKTOP_GNOME,
			PK_GROUP_ENUM_DESKTOP_KDE,
			PK_GROUP_ENUM_DESKTOP_OTHER,
			PK_GROUP_ENUM_DESKTOP_XFCE,
		/*	PK_GROUP_ENUM_EDUCATION, */
			PK_GROUP_ENUM_FONTS,
			PK_GROUP_ENUM_GAMES,
			PK_GROUP_ENUM_GRAPHICS,
		/*	PK_GROUP_ENUM_INTERNET, */
		/*	PK_GROUP_ENUM_LEGACY, */
			PK_GROUP_ENUM_LOCALIZATION,
		/*	PK_GROUP_ENUM_MAPS, */
			PK_GROUP_ENUM_MULTIMEDIA,
			PK_GROUP_ENUM_NETWORK,
			PK_GROUP_ENUM_OFFICE,
			PK_GROUP_ENUM_OTHER,
		/*	PK_GROUP_ENUM_POWER_MANAGEMENT, */
			PK_GROUP_ENUM_PROGRAMMING,
		/*	PK_GROUP_ENUM_PUBLISHING, */
		/*	PK_GROUP_ENUM_REPOS, */
		/*	PK_GROUP_ENUM_SECURITY, */
		/*	PK_GROUP_ENUM_SERVERS, */
			PK_GROUP_ENUM_SYSTEM,
		/*	PK_GROUP_ENUM_VIRTUALIZATION, */
			PK_GROUP_ENUM_SCIENCE,
			PK_GROUP_ENUM_DOCUMENTATION,
		/*	PK_GROUP_ENUM_ELECTRONICS, */
			PK_GROUP_ENUM_COLLECTIONS,
			PK_GROUP_ENUM_VENDOR,
		/*	PK_GROUP_ENUM_NEWEST, */
		-1);
}

/**
 * pk_backend_get_filters:
 */
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_NEWEST,
		-1);
}

/**
 * pk_backend_get_mime_types:
 */
gchar *
pk_backend_get_mime_types (PkBackend *backend)
{
	return g_strdup ("application/x-compressed-tar;"	/* .tgz */
			 "application/x-bzip-compressed-tar;"	/* .tbz */
			 "application/x-lzma-compressed-tar;"	/* .tlz */
			 "application/x-xz-compressed-tar"	/* .txz */);
}

/**
 * pk_backend_cancel:
 */
void
pk_backend_cancel (PkBackend *backend)
{
}

static slapt_pkg_info_t* _get_pkg_from_id(gchar *pi,
                            slapt_pkg_list_t *avail_pkgs,
                            slapt_pkg_list_t *installed_pkgs)
{
	gchar **pis;
	slapt_pkg_info_t *pkg;
	gchar **fields;
	const gchar *version;

	pis = pk_package_id_split(pi);
	fields = g_strsplit(pis[PK_PACKAGE_ID_VERSION], "-", 2);
	version = g_strdup_printf("%s-%s-%s", fields[0], pis[PK_PACKAGE_ID_ARCH], fields[1]);
	pkg = slapt_get_exact_pkg(avail_pkgs, pis[PK_PACKAGE_ID_NAME], version);
	if (pkg == NULL && installed_pkgs != NULL) {
		pkg = slapt_get_exact_pkg(installed_pkgs, pis[PK_PACKAGE_ID_NAME], version);
	}
	g_free((gpointer) version);
	g_strfreev(fields);
	g_strfreev(pis);

	return pkg;
}

static slapt_pkg_info_t* _get_pkg_from_string(const gchar *package_id,
                            slapt_pkg_list_t *avail_pkgs,
                            slapt_pkg_list_t *installed_pkgs)
{
	return _get_pkg_from_id((gchar *) package_id, avail_pkgs, installed_pkgs);
}

static gchar* _get_id_from_pkg(slapt_pkg_info_t *pkg)
{
	gchar *pi;
	gchar **fields;
	const gchar *version;
	const char *data;

	fields = g_strsplit(pkg->version, "-", 3);
	version = g_strdup_printf("%s-%s", fields[0], fields[2]);
	data = pkg->installed ? "installed" : "available"; /* TODO: source */
	pi = pk_package_id_build(pkg->name, version, fields[1], data);
	g_free((gpointer) version);
	g_strfreev(fields);

	return pi;
}

static const gchar* _get_string_from_pkg(slapt_pkg_info_t *pkg)
{
	return (const gchar *) _get_id_from_pkg(pkg);
}

/* return the last item of the pkg->location, after the slash */
static const char *_get_pkg_category(slapt_pkg_info_t *pkg)
{
	char *p;

	p = strrchr(pkg->location, '/');
	if (p == NULL)
	    return "";
	else
	    return (const char *) p + 1;
}

/* return the PackageKit group matching the Slackware category */
static PkGroupEnum _get_pkg_group(const char *category)
{
	gpointer value;

	value = g_hash_table_lookup(_cathash, category);
	if (value == NULL)
	    value = PK_GROUP_ENUM_UNKNOWN;

	return (PkGroupEnum) value;
}

/* return the first line of the pkg->description, without the prefix */
static const gchar *_get_pkg_summary(slapt_pkg_info_t *pkg)
{
	char *p;
	char *lf;
	const gchar *text;

	p = pkg->description;
	lf = strchr(p, '\n');

	if (lf == NULL)
	    text = g_strdup((const gchar *) p);
	else
	    text = g_strndup((const gchar *) p, lf - p);

	slapt_clean_description((char*) text, pkg->name);

	return text;
}

/* return the third plus lines of the pkg->description, without the prefix */
static const gchar *_get_pkg_description(slapt_pkg_info_t *pkg)
{
	char *p;
	char *lf;
	const gchar *text;

	p = pkg->description;
	lf = strchr(p, '\n');

	if (lf == NULL)
	    text = g_strdup((const gchar *) p);
	else
	    text = g_strdup((const gchar *) lf + 1);

	slapt_clean_description((char*) text, pkg->name);

	return text;
}

static void _show_transaction(PkBackend *backend, slapt_transaction_t *tran)
{
	slapt_pkg_info_t *pkg;
	const gchar *package_id;
	PkInfoEnum state;
	const char *summary;
	unsigned int i;

	for (i = 0; i < tran->queue->count; i++) {

	    if (tran->queue->pkgs[i]->type == INSTALL) {
		pkg = tran->queue->pkgs[i]->pkg.i;
		state = PK_INFO_ENUM_INSTALLING;
		package_id = _get_string_from_pkg(pkg);
		summary = _get_pkg_summary(pkg);
		pk_backend_package (backend, state, package_id, summary);
	    } else if (tran->queue->pkgs[i]->type == UPGRADE) {
		pkg = tran->queue->pkgs[i]->pkg.u->upgrade;
		state = PK_INFO_ENUM_UPDATING;
		package_id = _get_string_from_pkg(pkg);
		summary = _get_pkg_summary(pkg);
		pk_backend_package (backend, state, package_id, summary);
	    }

	}

	for (i = 0; i < tran->remove_pkgs->pkg_count; i++) {
	    pkg = tran->remove_pkgs->pkgs[i];
	    state = PK_INFO_ENUM_REMOVING;
	    package_id = _get_string_from_pkg(pkg);
	    summary = _get_pkg_summary(pkg);
	    pk_backend_package (backend, state, package_id, summary);
	}

}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	guint i;
	guint len;
	const gchar *package_id;
	gchar *pi;

	slapt_pkg_info_t *pkg;
	int ret;

	slapt_pkg_list_t *installed;
	slapt_pkg_list_t *available;

	slapt_pkg_list_t *depends;

	slapt_pkg_err_list_t *conflicts;
	slapt_pkg_err_list_t *missing;

	PkInfoEnum state;
	const char *summary;

	installed = slapt_get_installed_pkgs();
	available = slapt_get_available_pkgs();

	conflicts = slapt_init_pkg_err_list();
	missing = slapt_init_pkg_err_list();

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	len = g_strv_length (package_ids);
	for (i=0; i<len; i++) {
	    pi = package_ids[i];
	    if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished (backend);
		return;
	    }
	    pkg = _get_pkg_from_id(pi, available, installed);
	    if (pkg == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "package not found");
		continue;
	    }

	    depends = slapt_init_pkg_list();

	    ret = slapt_get_pkg_dependencies(_config,
		available, installed, pkg, depends, conflicts, missing);

	    for (i = 0; i < depends->pkg_count; i++) {
		pkg = depends->pkgs[i];

		state = pkg->installed ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
		package_id = _get_string_from_pkg(pkg);
		summary = _get_pkg_summary(pkg);
		pk_backend_package (backend, state, package_id, summary);
		g_free((gpointer) summary);
		g_free((gpointer) package_id);
	    }

	    slapt_free_pkg_list(depends);
	}

	slapt_free_pkg_err_list(missing);
	slapt_free_pkg_err_list(conflicts);

	slapt_free_pkg_list(available);
	slapt_free_pkg_list(installed);

	pk_backend_finished (backend);
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, gchar **package_ids)
{
	guint i;
	guint len;
	const gchar *package_id;
	gchar *pi;
	const gchar *license = "";
	const gchar *homepage = "";
	const gchar *description;
	PkGroupEnum group;

	slapt_pkg_list_t *installed;
	slapt_pkg_list_t *available;
	slapt_pkg_info_t *pkg;
	const char *category;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	installed = slapt_get_installed_pkgs();
	available = slapt_get_available_pkgs();

	len = g_strv_length (package_ids);
	for (i=0; i<len; i++) {
	    pi = package_ids[i];
	    if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished (backend);
		return;
	    }
	    pkg = _get_pkg_from_id(pi, available, installed);
	    if (pkg == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "package not found");
		continue;
	    }

	    category = _get_pkg_category(pkg);
	    group = _get_pkg_group(category);

	    package_id = _get_string_from_pkg(pkg);
	    description = g_strstrip((gchar*) _get_pkg_description(pkg));

	    pk_backend_details (backend, package_id,
		license, group, description, homepage, pkg->size_c * 1024);

	    g_free((gpointer) description);
	    g_free((gpointer) package_id);
	}

	slapt_free_pkg_list(available);
	slapt_free_pkg_list(installed);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
}

/**
 * pk_backend_get_requires:
 */
void
pk_backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	guint i;
	guint len;
	const gchar *package_id;
	gchar *pi;

	slapt_pkg_info_t *pkg;

	slapt_pkg_list_t *installed;
	slapt_pkg_list_t *available;
	slapt_pkg_list_t *to_install;
	slapt_pkg_list_t *to_remove;

	slapt_pkg_list_t *requires;

	PkInfoEnum state;
	const char *summary;

	installed = slapt_get_installed_pkgs();
	available = slapt_get_available_pkgs();
	to_install = slapt_init_pkg_list();
	to_remove = slapt_init_pkg_list();

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	len = g_strv_length (package_ids);
	for (i=0; i<len; i++) {
	    pi = package_ids[i];
	    if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished (backend);
		return;
	    }
	    pkg = _get_pkg_from_id(pi, available, installed);
	    if (pkg == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "package not found");
		continue;
	    }

	    requires = slapt_is_required_by(_config, available, installed, to_install, to_remove, pkg);

	    for (i = 0; i < requires->pkg_count; i++) {
		pkg = requires->pkgs[i];

		state = pkg->installed ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
		package_id = _get_string_from_pkg(pkg);
		summary = _get_pkg_summary(pkg);
		pk_backend_package (backend, state, package_id, summary);
		g_free((gpointer) summary);
		g_free((gpointer) package_id);
	    }

	    slapt_free_pkg_list(requires);
	}

	slapt_free_pkg_list(available);
	slapt_free_pkg_list(installed);
	slapt_free_pkg_list(to_install);
	slapt_free_pkg_list(to_remove);

	pk_backend_finished (backend);
}

/**
 * pk_backend_get_update_detail:
 */
void
pk_backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	guint i;
	guint len;
	const gchar *package_id;
	const gchar *old_package_id;
	gchar *pi;

	slapt_pkg_list_t *installed;
	slapt_pkg_list_t *available;
	const gchar *search;
	slapt_pkg_list_t *results;
	slapt_pkg_info_t *pkg;
	slapt_pkg_info_t *oldpkg;
	const gchar *title;
	char *changelog;
	const gchar *issued;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	installed = slapt_get_installed_pkgs();
	available = slapt_get_available_pkgs();

	len = g_strv_length (package_ids);
	for (i=0; i<len; i++) {
	    pi = package_ids[i];
	    if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished (backend);
		return;
	    }
	    pkg = _get_pkg_from_id(pi, available, installed);
	    if (pkg == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "package not found");
		continue;
	    }
	    package_id = _get_string_from_pkg(pkg);

	    search = g_strdup_printf("^%s$", pkg->name);
	    results = slapt_search_pkg_list(installed, search);
	    g_free((gpointer) search);
	    if (results->pkg_count > 0) {
		oldpkg = results->pkgs[0];
		old_package_id = _get_string_from_pkg(oldpkg);
	    } else {
		oldpkg = NULL;
		old_package_id = "";
	    }

	    changelog = slapt_get_pkg_changelog(pkg);
	    title = ""; /* first line of changelog */

	    issued = NULL;

	    pk_backend_update_detail (backend, package_id, old_package_id,
	        "", "", "", NULL, PK_RESTART_ENUM_NONE,
	        title, changelog, PK_UPDATE_STATE_ENUM_UNKNOWN, issued, NULL);

	    g_free((gpointer) issued);
	    if (changelog != NULL)
	        free(changelog);
	}

	slapt_free_pkg_list(available);
	slapt_free_pkg_list(installed);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
}

/**
 * pk_backend_get_updates:
 */
void
pk_backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	guint i;
	const gchar *package_id;
	const gchar *new_package_id;

	slapt_pkg_list_t *installed;
	slapt_pkg_list_t *available;
	slapt_pkg_info_t *pkg;
	slapt_pkg_info_t *newpkg;
	const gchar *summary;
	const char *changelog;
	PkInfoEnum state;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	installed = slapt_get_installed_pkgs();
	available = slapt_get_available_pkgs();

	for (i = 0; i < installed->pkg_count; i++) {
	    pkg = installed->pkgs[i];
	    newpkg = slapt_get_newest_pkg(available, pkg->name);
	    if (newpkg == NULL)
		continue;
	    if (slapt_cmp_pkgs(pkg,newpkg) >= 0)
		continue;

	    package_id = _get_string_from_pkg(pkg);
	    new_package_id = _get_string_from_pkg(newpkg);

	    changelog = slapt_get_pkg_changelog(newpkg);
	    if (changelog != NULL &&
	        strstr(changelog, "(* Security fix *)") != NULL)
		state = PK_INFO_ENUM_SECURITY;
	    else
		state = PK_INFO_ENUM_NORMAL;

	    summary =_get_pkg_summary(newpkg);
	    pk_backend_package (backend, state,
	                        new_package_id, summary);
	    g_free((gpointer) summary);
	    g_free((gpointer) new_package_id);
	    g_free((gpointer) package_id);
	}

	slapt_free_pkg_list(available);
	slapt_free_pkg_list(installed);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	_install_packages (backend, only_trusted, package_ids, FALSE);
}

/**
 * pk_backend_simulate_install_packages:
 */
void
pk_backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	_install_packages (backend, FALSE, package_ids, TRUE);
}

static void
_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids, gboolean simulate)
{
	guint i;
	guint len;
	gchar *pi;
	int ret;

	slapt_pkg_list_t *installed;
	slapt_pkg_list_t *available;
	slapt_transaction_t *transaction;
	slapt_pkg_info_t *pkg;

	/* FIXME: support only_trusted */

	pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	pk_backend_set_percentage (backend, 0);

	installed = slapt_get_installed_pkgs();
	available = slapt_get_available_pkgs();

	transaction = slapt_init_transaction();

	len = g_strv_length (package_ids);
	for (i=0; i<len; i++) {
	    pi = package_ids[i];
	    if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished (backend);
		return;
	    }
	    pkg = _get_pkg_from_id(pi, available, installed);
	    if (pkg == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "package not found");
		continue;
	    }

	    if (pkg->installed) {
		char *pkgname = slapt_stringify_pkg(pkg);
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED, "package %s already installed", pkgname);
		free(pkgname);
		continue;
	    }

	    slapt_add_install_to_transaction(transaction, pkg);
	}

	if (simulate) {
	    _show_transaction(backend, transaction);
	} else {
	    ret = slapt_handle_transaction(_config, transaction);
	    if (ret != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, "install failed");
	    }
	}

	slapt_free_transaction(transaction);

	slapt_free_pkg_list(available);
	slapt_free_pkg_list(installed);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
}

/**
 * pk_backend_refresh_cache:
 */
void
pk_backend_refresh_cache (PkBackend *backend, gboolean force)
{
	pk_backend_set_allow_cancel (backend, TRUE);
	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	slapt_update_pkg_cache(_config);
	pk_backend_finished (backend);
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBitfield filters, gchar **packages)
{
	guint i;
	guint len;

	const gchar *package_id;
	slapt_pkg_list_t *pkglist;
	slapt_pkg_info_t *pkg = NULL;
	slapt_pkg_list_t *results = NULL;

	PkInfoEnum state;
	const gchar *search;
	const char *summary;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		pkglist = slapt_get_installed_pkgs();
		state = PK_INFO_ENUM_INSTALLED;
	} else {
		pkglist = slapt_get_available_pkgs();
		state = PK_INFO_ENUM_AVAILABLE;
	}

	len = g_strv_length (packages);
	for (i=0; i<len; i++) {

		search = g_strdup_printf("^%s$", packages[i]); /* regexp */
		results = slapt_search_pkg_list(pkglist, search);
		g_free((gpointer) search);
		if (results == NULL) {
		    pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "package not found");
		    continue;
		}

		for (i = 0; i < results->pkg_count; i++) {
			pkg = results->pkgs[i];

			package_id = _get_string_from_pkg(pkg);
			summary = _get_pkg_summary(pkg);
			pk_backend_package (backend, state, package_id, summary);
			g_free((gpointer) summary);
			g_free((gpointer) package_id);
		}

		slapt_free_pkg_list(results);
	}

	slapt_free_pkg_list(pkglist);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	_remove_packages (backend, package_ids, allow_deps, autoremove, FALSE);
}

/**
 * pk_backend_simulate_remove_packages:
 */
void
pk_backend_simulate_remove_packages (PkBackend *backend, gchar **package_ids, gboolean autoremove)
{
	_remove_packages (backend, package_ids, TRUE, autoremove, TRUE);
}

static void
_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove, gboolean simulate)
{
	guint i;
	guint len;
	gchar *pi;
	int ret;

	slapt_pkg_list_t *installed;
	slapt_pkg_list_t *available;
	slapt_transaction_t *transaction;
	slapt_pkg_info_t *pkg;

	/* FIXME: support only_trusted */

	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
	pk_backend_set_percentage (backend, 0);

	installed = slapt_get_installed_pkgs();
	available = slapt_get_available_pkgs();

	transaction = slapt_init_transaction();

	len = g_strv_length (package_ids);
	for (i=0; i<len; i++) {
	    pi = package_ids[i];
	    if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished (backend);
		return;
	    }
	    pkg = _get_pkg_from_id(pi, installed, NULL);
	    if (pkg == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "package not found");
		continue;
	    }

	    if (!pkg->installed) {
		char *pkgname = slapt_stringify_pkg(pkg);
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED, "package %s not installed", pkgname);
		free(pkgname);
		continue;
	    }

	    slapt_add_remove_to_transaction(transaction, pkg);
	}

	if (simulate) {
	    _show_transaction(backend, transaction);
	} else {
	    ret = slapt_handle_transaction(_config, transaction);
	    if (ret != 0) {
	        pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, "remove failed");
	    }
	}

	slapt_free_transaction(transaction);

	slapt_free_pkg_list(available);
	slapt_free_pkg_list(installed);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	guint i;
	gchar *search;

	const gchar *package_id;
	slapt_pkg_list_t *pkglist;
	slapt_pkg_info_t *pkg = NULL;
	slapt_pkg_list_t *results = NULL;

	PkInfoEnum state;
	const char *summary;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	search = g_strjoinv ("&", values);

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		pkglist = slapt_get_installed_pkgs();
		state = PK_INFO_ENUM_INSTALLED;
	} else {
		pkglist = slapt_get_available_pkgs();
		state = PK_INFO_ENUM_AVAILABLE;
	}

		results = slapt_search_pkg_list(pkglist, search);
		for (i = 0; i < results->pkg_count; i++) {
			pkg = results->pkgs[i];

			package_id = _get_string_from_pkg(pkg);
			summary = _get_pkg_summary(pkg);
			pk_backend_package (backend, state, package_id, summary);
			g_free((gpointer) summary);
			g_free((gpointer) package_id);
		}

		slapt_free_pkg_list(results);

	slapt_free_pkg_list(pkglist);

	g_free (search);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
}

/**
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	guint i;
	gchar *search;

	const gchar *package_id;
	slapt_pkg_list_t *pkglist;
	slapt_pkg_info_t *pkg = NULL;
	PkGroupEnum group;
	PkGroupEnum search_group;
	const char *category;

	PkInfoEnum state;
	const char *summary;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	search = g_strjoinv ("&", values);

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		pkglist = slapt_get_installed_pkgs();
		state = PK_INFO_ENUM_INSTALLED;
	} else {
		pkglist = slapt_get_available_pkgs();
		state = PK_INFO_ENUM_AVAILABLE;
	}

	search_group = pk_group_enum_from_string(search);

	for (i = 0; i < pkglist->pkg_count; i++) {
		pkg = pkglist->pkgs[i];

		category = _get_pkg_category(pkg);
		group = _get_pkg_group(category);

		if (group == search_group) {

			package_id = _get_string_from_pkg(pkg);
			summary = _get_pkg_summary(pkg);
			pk_backend_package (backend, state, package_id, summary);
			g_free((gpointer) summary);
			g_free((gpointer) package_id);

		}
	}

	slapt_free_pkg_list(pkglist);

	g_free (search);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
}

/**
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	unsigned int i;
	gchar *search;

	const gchar *package_id;
	slapt_pkg_list_t *pkglist;
	slapt_pkg_info_t *pkg = NULL;
	slapt_pkg_list_t *results = NULL;

	PkInfoEnum state;
	const char *summary;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	search = g_strjoinv ("&", values);

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		pkglist = slapt_get_installed_pkgs();
		state = PK_INFO_ENUM_INSTALLED;
	} else {
		pkglist = slapt_get_available_pkgs();
		state = PK_INFO_ENUM_AVAILABLE;
	}

		results = slapt_search_pkg_list(pkglist, search);
		g_free((gpointer) search);
		if (results == NULL) {
		    pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "package not found");
		    goto out;
		}

		for (i = 0; i < results->pkg_count; i++) {
			pkg = results->pkgs[i];

			package_id = _get_string_from_pkg(pkg);
			summary = _get_pkg_summary(pkg);
			pk_backend_package (backend, state, package_id, summary);
			g_free((gpointer) summary);
			g_free((gpointer) package_id);
		}

		slapt_free_pkg_list(results);

out:
	slapt_free_pkg_list(pkglist);

	g_free (search);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	_update_packages (backend, only_trusted, package_ids, FALSE);
}

/**
 * pk_backend_simulate_update_packages:
 */
void
pk_backend_simulate_update_packages (PkBackend *backend, gchar **package_ids)
{
	_update_packages (backend, FALSE, package_ids, TRUE);
}

static void
_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids, gboolean simulate)
{
	guint i;
	guint len;
	const gchar *package_id;
	int ret;

	slapt_pkg_list_t *installed;
	slapt_pkg_list_t *available;
	const gchar *search;
	slapt_pkg_list_t *results = NULL;
	slapt_transaction_t *transaction;
	slapt_pkg_info_t *pkg;
	slapt_pkg_info_t *oldpkg;

	/* FIXME: support only_trusted */

	pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
	pk_backend_set_percentage (backend, 0);

	installed = slapt_get_installed_pkgs();
	available = slapt_get_available_pkgs();

	transaction = slapt_init_transaction();

	len = g_strv_length (package_ids);
	for (i=0; i<len; i++) {
	    package_id = package_ids[i];
	    pkg = _get_pkg_from_string(package_id, available, installed);
	    if (pkg == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "package not found");
		continue;
	    }
	    oldpkg = NULL;
	    search = g_strdup_printf("^%s$", pkg->name);
	    results = slapt_search_pkg_list(installed, search);
	    g_free((gpointer) search);
	    if (results->pkg_count > 0) {
		oldpkg = results->pkgs[0];
	    } else {
		continue;
	    }

	    slapt_add_upgrade_to_transaction(transaction, oldpkg, pkg);
	}

	if (simulate) {
	    _show_transaction(backend, transaction);
	} else {
	    ret = slapt_handle_transaction(_config, transaction);
	    if (ret != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, "install failed");
	    }
	}

	slapt_free_transaction(transaction);

	slapt_free_pkg_list(available);
	slapt_free_pkg_list(installed);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
}

/* return a unique repoid (currently uses MD5), for a package source */
static const gchar *_get_source_repoid(slapt_source_t *src)
{
	unsigned char md5[MD5_DIGEST_LENGTH];
	const gchar *text;
	MD5_CTX ctx;
	int i;

	MD5_Init(&ctx);
	MD5_Update(&ctx, src->url, strlen(src->url));
	MD5_Update(&ctx, &src->priority, sizeof(SLAPT_PRIORITY_T));
	MD5_Final(md5, &ctx);

	text = g_malloc0(MD5_DIGEST_LENGTH * 2 + 1);
	for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
	    sprintf((char *) text + 2 * i, "%.2x", md5[i]);
	}
	return text;
}

/**
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	unsigned int i;
	slapt_source_t *source;
	const gchar *repo_id;
	const gchar *repo_description;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	for (i = 0; i < _config->sources->count; i++) {
	    source = _config->sources->src[i];

	    repo_id = _get_source_repoid(source);
	    repo_description = g_strdup_printf("%s (%s)", source->url,
	                       slapt_priority_to_str(source->priority));
	    pk_backend_repo_detail (backend, repo_id, repo_description,
	                                              !source->disabled);
	    g_free((gpointer) repo_description);
	    g_free((gpointer) repo_id);
	}

	pk_backend_finished (backend);
}

/**
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	unsigned int i;
	slapt_source_t *source;
	const gchar *repo_id;

	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);

	for (i = 0; i < _config->sources->count; i++) {
	    source = _config->sources->src[i];

	    repo_id = _get_source_repoid(source);
	    if (strcmp(repo_id, rid) == 0) {
		source->disabled = !enabled;
		/* Note: currently writing config deletes all comments! */
		slapt_write_rc_config(_config, _config_file);
		break;
	    }
	    g_free((gpointer) repo_id);
	}

	pk_backend_finished (backend);
}

/**
 * pk_backend_get_packages:
 */
void
pk_backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	PkFilterEnum list_order[] = {
	    PK_FILTER_ENUM_INSTALLED,
	    PK_FILTER_ENUM_NOT_INSTALLED,
	    PK_FILTER_ENUM_UNKNOWN
	};
	PkFilterEnum *list_filter;

	slapt_pkg_list_t *pkglist;
	slapt_pkg_info_t *pkg;
	slapt_pkg_info_t *other_pkg;
	unsigned int i;
	const gchar *package_id;

	slapt_pkg_list_t *installed;
	slapt_pkg_list_t *available;

	PkInfoEnum state;
	const char *summary;

	installed = slapt_get_installed_pkgs();
	available = slapt_get_available_pkgs();

	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);
	for (list_filter = list_order; *list_filter != PK_FILTER_ENUM_UNKNOWN; list_filter++) {

	    if (*list_filter == PK_FILTER_ENUM_INSTALLED) {
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED))
		    break;
		pkglist = installed;
	    } else if (*list_filter == PK_FILTER_ENUM_NOT_INSTALLED) {
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED))
		    break;
		pkglist = available;
	    } else {
		continue;
	    }

	    for (i = 0; i < pkglist->pkg_count; i++) {
		pkg = pkglist->pkgs[i];

		/* check so that we don't show installed pkgs twice */
		if (*list_filter == PK_FILTER_ENUM_NOT_INSTALLED &&
		    !pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		    other_pkg = slapt_get_exact_pkg(installed,
		                                    pkg->name, pkg->version);
		    if (other_pkg != NULL) {
			continue;
		    }
		}

		/* only display the newest pkg in each pkglist */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NEWEST)) {
		    other_pkg = slapt_get_newest_pkg(pkglist, pkg->name);
		    if (slapt_cmp_pkgs(pkg, other_pkg) <= 0) {
			continue;
		    }
		}

		state = pkg->installed ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
		package_id = _get_string_from_pkg(pkg);
		summary = _get_pkg_summary(pkg);
		pk_backend_package (backend, state, package_id, summary);
		g_free((gpointer) summary);
		g_free((gpointer) package_id);
	    }

	}

	slapt_free_pkg_list(available);
	slapt_free_pkg_list(installed);

	pk_backend_finished (backend);
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
{
	guint i;
	guint len;
	const gchar *package_id;
	const gchar *files;
	const char *error;
	slapt_pkg_list_t *installed;
	slapt_pkg_list_t *available;
	slapt_pkg_info_t *pkg;
	const char *summary;
	const char *note = NULL;
	char *filename;

	pk_backend_set_status (backend, PK_STATUS_ENUM_LOADING_CACHE);

	installed = slapt_get_installed_pkgs();
	available = slapt_get_available_pkgs();

	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	pk_backend_set_percentage (backend, 0);

	len = g_strv_length (package_ids);
	for (i=0; i<len; i++) {
	    package_id = package_ids[i];
	    pkg = _get_pkg_from_string(package_id, available, installed);
	    if (pkg == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "package not found");
		continue;
	    }

	    summary = _get_pkg_summary(pkg);
	    pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING, package_id, summary);
	    g_free((gpointer) summary);

	    error = slapt_download_pkg(_config, pkg, note);
	    if (error == NULL) {

		filename = slapt_gen_pkg_file_name(_config, pkg);
		files = g_strdup(filename);

		pk_backend_files (backend, package_id, files);

		g_free((gpointer) files);
		free(filename);
	    } else {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED, error);
	    }
	}

	slapt_free_pkg_list(available);
	slapt_free_pkg_list(installed);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
}

/**
 * pk_backend_get_description:
 */
gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("Slack");
}

/**
 * pk_backend_get_author:
 */
gchar *
pk_backend_get_author (PkBackend *backend)
{
	return g_strdup ("Anders F Björklund <afb@users.sourceforge.net>");
}

