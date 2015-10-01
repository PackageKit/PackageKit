/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Fabien Bourigault <bourigaultfabien@gmail.com>
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

#include <locale.h>
#include <sys/utsname.h>
#include <alpm.h>
#include <glib.h>

#include <config.h>
#include <pk-backend.h>

#include "pk-alpm-environment.h"

void
pk_alpm_environment_initialize (PkBackendJob *job)
{
	struct utsname un;
	const gchar *tmp;
	gchar *value;

	/* PATH might have been nuked by D-Bus */
	g_setenv ("PATH", PK_BACKEND_DEFAULT_PATH, FALSE);

	uname (&un);
	value = g_strdup_printf ("%s/%s (%s %s) libalpm/%s", PACKAGE_TARNAME,
				 PACKAGE_VERSION, un.sysname, un.machine,
				 alpm_version ());
	g_setenv ("HTTP_USER_AGENT", value, FALSE);
	g_free (value);

	tmp = pk_backend_job_get_locale (job);
	if (tmp != NULL)
		setlocale (LC_ALL, tmp);

	tmp = pk_backend_job_get_proxy_http (job);
	if (tmp != NULL) {
		_cleanup_free_ gchar *uri = pk_backend_convert_uri (tmp);
		g_setenv ("http_proxy", uri, TRUE);
	}

	tmp = pk_backend_job_get_proxy_https (job);
	if (tmp != NULL) {
		_cleanup_free_ gchar *uri = pk_backend_convert_uri (tmp);
		g_setenv ("https_proxy", uri, TRUE);
	}

	tmp = pk_backend_job_get_proxy_ftp (job);
	if (tmp != NULL) {
		_cleanup_free_ gchar *uri = pk_backend_convert_uri (tmp);
		g_setenv ("ftp_proxy", uri, TRUE);
	}

	tmp = pk_backend_job_get_proxy_socks (job);
	if (tmp != NULL) {
		_cleanup_free_ gchar *uri = pk_backend_convert_uri (tmp);
		g_setenv ("socks_proxy", uri, TRUE);
	}

	tmp = pk_backend_job_get_no_proxy (job);
	if (tmp != NULL) {
		g_setenv ("no_proxy", tmp, TRUE);
	}

	tmp = pk_backend_job_get_pac (job);
	if (tmp != NULL) {
		_cleanup_free_ gchar *uri = pk_backend_convert_uri (tmp);
		g_setenv ("pac", uri, TRUE);
	}
}

