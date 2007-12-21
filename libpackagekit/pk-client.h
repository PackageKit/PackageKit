/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_CLIENT_H
#define __PK_CLIENT_H

#include <glib-object.h>
#include "pk-enum.h"
#include "pk-enum-list.h"
#include "pk-package-list.h"

G_BEGIN_DECLS

#define PK_TYPE_CLIENT		(pk_client_get_type ())
#define PK_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CLIENT, PkClient))
#define PK_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CLIENT, PkClientClass))
#define PK_IS_CLIENT(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CLIENT))
#define PK_IS_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CLIENT))
#define PK_CLIENT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CLIENT, PkClientClass))

/**
 * PK_CLIENT_PERCENTAGE_INVALID:
 *
 * The unknown percentage value
 */
#define PK_CLIENT_PERCENTAGE_INVALID	101

typedef struct PkClientPrivate PkClientPrivate;

typedef struct
{
	GObject		 parent;
	PkClientPrivate	*priv;
} PkClient;

typedef struct
{
	GObjectClass	parent_class;
} PkClientClass;

GType		 pk_client_get_type			(void);
PkClient	*pk_client_new				(void);

gboolean	 pk_client_set_tid			(PkClient	*client,
							 const gchar	*tid);
gboolean	 pk_client_set_promiscuous		(PkClient	*client,
							 gboolean	 enabled);
gchar		*pk_client_get_tid			(PkClient	*client);

gboolean	 pk_client_set_use_buffer		(PkClient	*client,
							 gboolean	 use_buffer);
gboolean	 pk_client_get_use_buffer		(PkClient	*client);

/* general methods */
gboolean	 pk_client_get_status			(PkClient	*client,
							 PkStatusEnum	*status);
gboolean	 pk_client_get_role			(PkClient	*client,
							 PkRoleEnum	*role,
							 gchar		**package_id);
gboolean	 pk_client_get_progress			(PkClient	*client,
							 guint		*percentage,
							 guint		*subpercentage,
							 guint		*elapsed,
							 guint		*remaining);
gboolean	 pk_client_get_package			(PkClient	*client,
							 gchar		**package_id);
gboolean	 pk_client_cancel			(PkClient	*client);



gboolean	 pk_client_get_updates			(PkClient	*client);
gboolean	 pk_client_update_system		(PkClient	*client);
gboolean	 pk_client_search_name			(PkClient	*client,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_client_search_details		(PkClient	*client,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_client_search_group			(PkClient	*client,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_client_search_file			(PkClient	*client,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_client_get_depends			(PkClient	*client,
							 const gchar	*package_id,
							 gboolean	 recursive);
gboolean	 pk_client_get_update_detail		(PkClient	*client,
							 const gchar	*package_id);
gboolean	 pk_client_get_requires			(PkClient	*client,
							 const gchar	*package_id,
							 gboolean	 recursive);
gboolean	 pk_client_get_description		(PkClient	*client,
							 const gchar	*package_id);
gboolean	 pk_client_get_files			(PkClient	*client,
							 const gchar	*package_id);
gboolean	 pk_client_remove_package		(PkClient	*client,
							 const gchar	*package,
							 gboolean	 allow_deps);
gboolean	 pk_client_refresh_cache		(PkClient	*client,
							 gboolean	 force);
gboolean	 pk_client_install_package		(PkClient	*client,
							 const gchar	*package_id);
gboolean	 pk_client_update_package		(PkClient	*client,
							 const gchar	*package_id);
gboolean	 pk_client_install_file			(PkClient	*client,
							 const gchar	*full_path);
gboolean	 pk_client_resolve			(PkClient	*client,
							 const gchar	*filter,
							 const gchar	*package);
gboolean	 pk_client_rollback			(PkClient	*client,
							 const gchar	*transaction_id);
gboolean	 pk_client_cancel			(PkClient	*client);
gboolean	 pk_client_requeue			(PkClient	*client);

/* repo stuff */
gboolean	 pk_client_get_repo_list		(PkClient	*client);
gboolean	 pk_client_repo_enable			(PkClient	*client,
							 const gchar	*repo_id,
							 gboolean	 enabled);
gboolean	 pk_client_repo_set_data		(PkClient	*client,
							 const gchar	*repo_id,
							 const gchar	*parameter,
							 const gchar	*value);

/* cached stuff */
guint		 pk_client_package_buffer_get_size	(PkClient	*client);
PkPackageItem	*pk_client_package_buffer_get_item	(PkClient	*client,
							 guint		 item);
PkRestartEnum	 pk_client_get_require_restart		(PkClient	*client);

/* not job specific */
PkEnumList	*pk_client_get_actions			(PkClient	*client);
PkEnumList	*pk_client_get_filters			(PkClient	*client);
PkEnumList	*pk_client_get_groups			(PkClient	*client);
gboolean	 pk_client_reset			(PkClient	*client);
gboolean	 pk_client_get_old_transactions		(PkClient	*client,
							 guint		 number);
gboolean	 pk_client_get_backend_detail		(PkClient	*client,
							 gchar		**name,
							 gchar		**author);
gboolean	 pk_client_get_time_since_action	(PkClient	*client,
							 PkRoleEnum	 role,
							 guint		*seconds);
gboolean	 pk_client_is_caller_active		(PkClient	*client,
							 gboolean	*is_active);

G_END_DECLS

#endif /* __PK_CLIENT_H */
