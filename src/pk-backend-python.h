/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#include <glib-object.h>
#include "pk-backend.h"

G_BEGIN_DECLS

#define PK_TYPE_BACKEND_PYTHON		(pk_backend_python_get_type ())
#define PK_BACKEND_PYTHON(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_BACKEND_PYTHON, PkBackendPython))
#define PK_BACKEND_PYTHON_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_BACKEND_PYTHON, PkBackendPythonClass))
#define PK_IS_BACKEND_PYTHON(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_BACKEND_PYTHON))
#define PK_IS_BACKEND_PYTHON_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_BACKEND_PYTHON))
#define PK_BACKEND_PYTHON_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_BACKEND_PYTHON, PkBackendPythonClass))

typedef struct PkBackendPythonPrivate PkBackendPythonPrivate;

typedef struct
{
	 GObject		 parent;
	 PkBackendPythonPrivate	*priv;
} PkBackendPython;

typedef struct
{
	GObjectClass	parent_class;
} PkBackendPythonClass;

GType		 pk_backend_python_get_type		(void) G_GNUC_CONST;
PkBackendPython	*pk_backend_python_new			(void);
gboolean	 pk_backend_python_refresh_cache	(PkBackendPython *python);
gboolean	 pk_backend_python_update_system	(PkBackendPython *python);
gboolean	 pk_backend_python_resolve		(PkBackendPython *python);
gboolean	 pk_backend_python_rollback		(PkBackendPython *python);
gboolean	 pk_backend_python_search_name		(PkBackendPython *python);
gboolean	 pk_backend_python_search_details	(PkBackendPython *python);
gboolean	 pk_backend_python_search_group		(PkBackendPython *python);
gboolean	 pk_backend_python_search_file		(PkBackendPython *python);
gboolean	 pk_backend_python_get_packages		(PkBackendPython *python);
gboolean	 pk_backend_python_download_packages	(PkBackendPython *python);
gboolean	 pk_backend_python_get_depends		(PkBackendPython *python);
gboolean	 pk_backend_python_get_requires		(PkBackendPython *python);
gboolean	 pk_backend_python_get_update_detail	(PkBackendPython *python);
gboolean	 pk_backend_python_get_details		(PkBackendPython *python);
gboolean	 pk_backend_python_get_files		(PkBackendPython *python);
gboolean	 pk_backend_python_remove_packages	(PkBackendPython *python);
gboolean	 pk_backend_python_install_packages	(PkBackendPython *python);
gboolean	 pk_backend_python_update_packages	(PkBackendPython *python);
gboolean	 pk_backend_python_install_files	(PkBackendPython *python);
gboolean	 pk_backend_python_service_pack		(PkBackendPython *python);
gboolean	 pk_backend_python_what_provides	(PkBackendPython *python);
gboolean	 pk_backend_python_repo_enable		(PkBackendPython *python);
gboolean	 pk_backend_python_repo_set_data	(PkBackendPython *python);
gboolean	 pk_backend_python_get_repo_list	(PkBackendPython *python);
gboolean	 pk_backend_python_cancel		(PkBackendPython *python);
gboolean	 pk_backend_python_get_updates		(PkBackendPython *python);
gboolean	 pk_backend_python_startup		(PkBackendPython *python,
							 const gchar	 *filename);

G_END_DECLS

#endif /* __PK_BACKEND_PYTHON_H */
