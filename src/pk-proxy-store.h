/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2025-2026 Matthias Klumpp <matthias@tenstral.net>
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __PK_PROXY_STORE_H
#define __PK_PROXY_STORE_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
	gchar *http;
	gchar *https;
	gchar *ftp;
	gchar *socks;
	gchar *no_proxy;
	gchar *pac;
} PkProxySettings;

PkProxySettings *pk_proxy_settings_new (const gchar *http,
					const gchar *https,
					const gchar *ftp,
					const gchar *socks,
					const gchar *no_proxy,
					const gchar *pac);
void		 pk_proxy_settings_free (PkProxySettings *settings);
gboolean	 pk_proxy_settings_is_empty (const PkProxySettings *settings);
gboolean	 pk_proxy_settings_equal (const PkProxySettings *a,
					  const PkProxySettings *b);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PkProxySettings, pk_proxy_settings_free)

gboolean pk_proxy_store_load (GKeyFile	       *conf,
			      guint		uid,
			      const gchar      *session,
			      PkProxySettings **settings,
			      GError	      **error);
gboolean pk_proxy_store_save (GKeyFile		    *conf,
			      guint		     uid,
			      const gchar	    *session,
			      const PkProxySettings *settings,
			      GError		   **error);

G_END_DECLS

#endif /* __PK_PROXY_STORE_H */
