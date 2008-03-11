/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_RUNNER_H
#define __PK_RUNNER_H

#include <glib-object.h>
#include <pk-enum-list.h>
#include "pk-enum.h"
#include "pk-package-list.h"

G_BEGIN_DECLS

#define PK_TYPE_RUNNER		(pk_runner_get_type ())
#define PK_RUNNER(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_RUNNER, PkRunner))
#define PK_RUNNER_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_RUNNER, PkRunnerClass))
#define PK_IS_RUNNER(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_RUNNER))
#define PK_IS_RUNNER_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_RUNNER))
#define PK_RUNNER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_RUNNER, PkRunnerClass))

typedef struct PkRunnerPrivate PkRunnerPrivate;

typedef struct
{
	 GObject		 parent;
	 PkRunnerPrivate	*priv;
} PkRunner;

typedef struct
{
	GObjectClass	parent_class;
} PkRunnerClass;

/* general */
GType		 pk_runner_get_type			(void);
PkRunner	*pk_runner_new				(void);
gboolean	 pk_runner_run				(PkRunner      *runner)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_runner_cancel			(PkRunner	*runner,
							 gchar		**error_text);
gboolean	 pk_runner_get_depends			(PkRunner	*runner,
							 const gchar	*filter,
							 const gchar	*package_id,
							 gboolean	 recursive);
gboolean	 pk_runner_get_update_detail		(PkRunner	*runner,
							 const gchar	*package_id);
gboolean	 pk_runner_get_description		(PkRunner	*runner,
							 const gchar	*package_id);
gboolean	 pk_runner_get_files 			(PkRunner	*runner,
							 const gchar	*package_id);
gboolean	 pk_runner_get_requires			(PkRunner	*runner,
							 const gchar	*filter,
							 const gchar	*package_id,
							 gboolean	 recursive);
gboolean	 pk_runner_get_updates			(PkRunner	*runner,
							 const gchar	*filter);
gboolean	 pk_runner_install_package		(PkRunner	*runner,
							 const gchar	*package_id);
gboolean	 pk_runner_install_file			(PkRunner	*runner,
							 const gchar	*full_path);
gboolean	 pk_runner_service_pack			(PkRunner	*runner,
							 const gchar	*location);
gboolean	 pk_runner_refresh_cache		(PkRunner	*runner,
							 gboolean	 force);
gboolean	 pk_runner_remove_package		(PkRunner	*runner,
							 const gchar	*package_id,
							 gboolean	 allow_deps,
							 gboolean	 autoremoe);
gboolean	 pk_runner_search_details		(PkRunner	*runner,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_runner_resolve			(PkRunner	*runner,
							 const gchar	*filter,
							 const gchar	*package);
gboolean	 pk_runner_rollback			(PkRunner	*runner,
							 const gchar	*transaction_id);
gboolean	 pk_runner_search_file			(PkRunner	*runner,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_runner_search_group			(PkRunner	*runner,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_runner_search_name			(PkRunner	*runner,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_runner_update_package		(PkRunner	*runner,
							 const gchar	*package_id);
gboolean	 pk_runner_update_system		(PkRunner	*runner);
gboolean	 pk_runner_get_repo_list		(PkRunner	*runner);
gboolean	 pk_runner_repo_enable			(PkRunner	*runner,
							 const gchar	*repo_id,
							 gboolean	 enabled);
gboolean	 pk_runner_repo_set_data		(PkRunner	*runner,
							 const gchar	*repo_id,
							 const gchar	*parameter,
							 const gchar	*value);

/* get status */
const gchar	*pk_runner_get_tid			(PkRunner	*runner);
PkStatusEnum	 pk_runner_get_status			(PkRunner	*runner);
PkRoleEnum	 pk_runner_get_role			(PkRunner	*runner);
const gchar	*pk_runner_get_text			(PkRunner	*runner);
gboolean	 pk_runner_get_package			(PkRunner	*runner,
							 gchar		**package_id);
gboolean	 pk_runner_get_allow_cancel		(PkRunner	*runner);
PkPackageList	*pk_runner_get_package_list		(PkRunner	*runner);
PkEnumList	*pk_runner_get_actions			(PkRunner	*runner);
PkEnumList	*pk_runner_get_groups			(PkRunner	*runner);
PkEnumList	*pk_runner_get_filters			(PkRunner	*runner);
guint		 pk_runner_get_runtime			(PkRunner	*runner);
gboolean	 pk_runner_set_dbus_name		(PkRunner	*runner,
							 const gchar	*dbus_name);
gboolean	 pk_runner_is_caller_active		(PkRunner	*runner,
							 gboolean	*is_active);

/* set status */
gboolean	 pk_runner_set_tid			(PkRunner	*runner,
							 const gchar	*tid);
gboolean	 pk_runner_set_role			(PkRunner	*runner,
							 PkRoleEnum	 role);

G_END_DECLS

#endif /* __PK_RUNNER_H */
