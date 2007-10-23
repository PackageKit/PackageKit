/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 James Bowes <jbowes@dangerouslyinc.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef __PK_BACKEND_PYTHON_H
#define __PK_BACKEND_PYTHON_H

#include <gmodule.h>
#include <pk-backend.h>

G_BEGIN_DECLS

void	pk_backend_python_cancel		(PkBackend	*backend);

void	pk_backend_python_get_depends		(PkBackend	*backend,
						 const gchar	*package_id);

void	pk_backend_python_get_description	(PkBackend	*backend,
						 const gchar	*package_id);

void	pk_backend_python_get_requires		(PkBackend	*backend,
						 const gchar	*package_id);

void	pk_backend_python_get_updates		(PkBackend	*backend);

void	pk_backend_python_install_package	(PkBackend	*backend,
						 const gchar	*package_id);

void	pk_backend_python_install_file		(PkBackend	*backend,
						 const gchar	*full_path);

void	pk_backend_python_refresh_cache		(PkBackend	*backend,
						 gboolean	 force);

void	pk_backend_python_remove_package	(PkBackend	*backend,
						 const gchar	*package_id,
						 gboolean	 allow_deps);

void	pk_backend_python_search_details	(PkBackend	*backend,
						 const gchar	*filter,
						 const gchar	*search);

void	pk_backend_python_search_file		(PkBackend	*backend,
						 const gchar	*filter,
						 const gchar	*search);

void	pk_backend_python_search_group		(PkBackend	*backend,
						 const gchar	*filter,
						 const gchar	*search);

void	pk_backend_python_search_name		(PkBackend	*backend,
						 const gchar	*filter,
						 const gchar	*search);

void	pk_backend_python_update_package	(PkBackend	*backend,
						 const gchar	*package_id);

void	pk_backend_python_update_system		(PkBackend	*backend);

void	pk_backend_python_resolve		(PkBackend	*backend,
						 const gchar	*filter,
						 const gchar	*package_id);

void	pk_backend_python_get_repo_list		(PkBackend	*backend);

void	pk_backend_python_repo_enable		(PkBackend	*backend,
						 const gchar	*rid,
						 gboolean	 enabled);

G_END_DECLS

#endif /* __PK_BACKEND_PYTHON_H */
