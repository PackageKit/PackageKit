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
#define PK_CLIENT_ERROR	 	(pk_client_error_quark ())
#define PK_CLIENT_TYPE_ERROR	(pk_client_error_get_type ())

/**
 * PK_CLIENT_PERCENTAGE_INVALID:
 *
 * The unknown percentage value
 */
#define PK_CLIENT_PERCENTAGE_INVALID	101

/**
 * PkClientError:
 *
 * Errors that can be thrown
 */
typedef enum
{
	PK_CLIENT_ERROR_FAILED,
	PK_CLIENT_ERROR_NO_TID,
	PK_CLIENT_ERROR_ALREADY_TID,
	PK_CLIENT_ERROR_ROLE_UNKNOWN,
	PK_CLIENT_ERROR_PROMISCUOUS,
	PK_CLIENT_ERROR_INVALID_PACKAGEID,
	PK_CLIENT_ERROR_LAST
} PkClientError;

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

GQuark		 pk_client_error_quark			(void);
GType		 pk_client_error_get_type		(void);
gboolean	 pk_client_error_print			(GError		**error);

GType		 pk_client_get_type			(void);
PkClient	*pk_client_new				(void);

gboolean	 pk_client_set_tid			(PkClient	*client,
							 const gchar	*tid,
							 GError		**error);
gboolean	 pk_client_set_promiscuous		(PkClient	*client,
							 gboolean	 enabled,
							 GError		**error);
gchar		*pk_client_get_tid			(PkClient	*client);

gboolean	 pk_client_set_use_buffer		(PkClient	*client,
							 gboolean	 use_buffer,
							 GError		**error);
gboolean	 pk_client_set_synchronous		(PkClient	*client,
							 gboolean	 synchronous,
							 GError		**error);
gboolean	 pk_client_set_name_filter		(PkClient	*client,
							 gboolean	 name_filter,
							 GError		**error);
gboolean	 pk_client_get_use_buffer		(PkClient	*client);
gboolean	 pk_client_get_allow_cancel		(PkClient	*client,
							 gboolean	*allow_cancel,
							 GError		**error);

/* general methods */
gboolean	 pk_client_get_status			(PkClient	*client,
							 PkStatusEnum	*status,
							 GError		**error);
gboolean	 pk_client_get_role			(PkClient	*client,
							 PkRoleEnum	*role,
							 gchar		**package_id,
							 GError		**error);
gboolean	 pk_client_get_progress			(PkClient	*client,
							 guint		*percentage,
							 guint		*subpercentage,
							 guint		*elapsed,
							 guint		*remaining,
							 GError		**error);
gboolean	 pk_client_get_package			(PkClient	*client,
							 gchar		**package,
							 GError		**error);
gboolean	 pk_client_cancel			(PkClient	*client,
							 GError		**error);



gboolean	 pk_client_get_updates			(PkClient	*client,
							 const gchar	*filter,
							 GError		**error);
gboolean	 pk_client_update_system		(PkClient	*client,
							 GError		**error);
gboolean	 pk_client_search_name			(PkClient	*client,
							 const gchar	*filter,
							 const gchar	*search,
							 GError		**error);
gboolean	 pk_client_search_details		(PkClient	*client,
							 const gchar	*filter,
							 const gchar	*search,
							 GError		**error);
gboolean	 pk_client_search_group			(PkClient	*client,
							 const gchar	*filter,
							 const gchar	*search,
							 GError		**error);
gboolean	 pk_client_search_file			(PkClient	*client,
							 const gchar	*filter,
							 const gchar	*search,
							 GError		**error);
gboolean	 pk_client_get_depends			(PkClient	*client,
							 const gchar	*filter,
							 const gchar	*package_id,
							 gboolean	 recursive,
							 GError		**error);
gboolean	 pk_client_get_update_detail		(PkClient	*client,
							 const gchar	*package_id,
							 GError		**error);
gboolean	 pk_client_get_requires			(PkClient	*client,
							 const gchar	*filter,
							 const gchar	*package_id,
							 gboolean	 recursive,
							 GError		**error);
gboolean	 pk_client_get_description		(PkClient	*client,
							 const gchar	*package_id,
							 GError		**error);
gboolean	 pk_client_get_files			(PkClient	*client,
							 const gchar	*package_id,
							 GError		**error);
gboolean	 pk_client_remove_package		(PkClient	*client,
							 const gchar	*package_id,
							 gboolean	 allow_deps,
							 gboolean	 autoremove,
							 GError		**error);
gboolean	 pk_client_refresh_cache		(PkClient	*client,
							 gboolean	 force,
							 GError		**error);
gboolean	 pk_client_install_package		(PkClient	*client,
							 const gchar	*package_id,
							 GError		**error);
gboolean	 pk_client_update_package		(PkClient	*client,
							 const gchar	*package_id,
							 GError		**error);
gboolean	 pk_client_install_file			(PkClient	*client,
							 const gchar	*file,
							 GError		**error);
gboolean	 pk_client_service_pack			(PkClient	*client,
							 const gchar	*location,
							 GError		**error);
gboolean	 pk_client_resolve			(PkClient	*client,
							 const gchar	*filter,
							 const gchar	*package,
							 GError		**error);
gboolean	 pk_client_rollback			(PkClient	*client,
							 const gchar	*transaction_id,
							 GError		**error);
gboolean	 pk_client_cancel			(PkClient	*client,
							 GError		**error);
gboolean	 pk_client_requeue			(PkClient	*client,
							 GError		**error);

/* repo stuff */
gboolean	 pk_client_get_repo_list		(PkClient	*client,
							 GError		**error);
gboolean	 pk_client_repo_enable			(PkClient	*client,
							 const gchar	*repo_id,
							 gboolean	 enabled,
							 GError		**error);
gboolean	 pk_client_repo_set_data		(PkClient	*client,
							 const gchar	*repo_id,
							 const gchar	*parameter,
							 const gchar	*value,
							 GError		**error);

/* cached stuff */
guint		 pk_client_package_buffer_get_size	(PkClient	*client);
PkPackageItem	*pk_client_package_buffer_get_item	(PkClient	*client,
							 guint		 item);
PkRestartEnum	 pk_client_get_require_restart		(PkClient	*client);

/* not job specific */
PkEnumList	*pk_client_get_actions			(PkClient	*client);
PkEnumList	*pk_client_get_filters			(PkClient	*client);
PkEnumList	*pk_client_get_groups			(PkClient	*client);
gboolean	 pk_client_reset			(PkClient	*client,
							 GError		**error);
gboolean	 pk_client_get_old_transactions		(PkClient	*client,
							 guint		 number,
							 GError		**error);
gboolean	 pk_client_get_backend_detail		(PkClient	*client,
							 gchar		**name,
							 gchar		**author,
							 GError		**error);
gboolean	 pk_client_get_time_since_action	(PkClient	*client,
							 PkRoleEnum	 role,
							 guint		*seconds,
							 GError		**error);
gboolean	 pk_client_is_caller_active		(PkClient	*client,
							 gboolean	*is_active,
							 GError		**error);

G_END_DECLS

#endif /* __PK_CLIENT_H */
