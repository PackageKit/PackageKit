/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_CONTROL_H
#define __PK_CONTROL_H

#include <glib-object.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-enum.h>

G_BEGIN_DECLS

#define PK_TYPE_CONTROL		(pk_control_get_type ())
#define PK_CONTROL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CONTROL, PkControl))
#define PK_CONTROL_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CONTROL, PkControlClass))
#define PK_IS_CONTROL(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CONTROL))
#define PK_IS_CONTROL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CONTROL))
#define PK_CONTROL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CONTROL, PkControlClass))
#define PK_CONTROL_ERROR	(pk_control_error_quark ())
#define PK_CONTROL_TYPE_ERROR	(pk_control_error_get_type ())

typedef struct _PkControlPrivate	PkControlPrivate;
typedef struct _PkControl		PkControl;
typedef struct _PkControlClass		PkControlClass;

/**
 * PkControlError:
 * @PK_CONTROL_ERROR_FAILED: the transaction failed for an unknown reason
 *
 * Errors that can be thrown
 */
typedef enum
{
	PK_CONTROL_ERROR_FAILED,
	PK_CONTROL_ERROR_CANNOT_START_DAEMON
} PkControlError;

struct _PkControl
{
	 GObject		 parent;
	 PkControlPrivate	*priv;
};

struct _PkControlClass
{
	GObjectClass	parent_class;

	/* signals */
	void		(* transaction_list_changed)	(PkControl	*control,
							 gchar		**transaction_ids);
	void		(* updates_changed)		(PkControl	*control);
	void		(* repo_list_changed)		(PkControl	*control);
	void		(* network_state_changed)	(PkControl	*control);
	void		(* restart_schedule)		(PkControl	*control);
	void		(* locked)			(PkControl	*control,
							 gboolean	 is_locked);
	void		(* connection_changed)		(PkControl	*control,
							 gboolean	 connected);
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GQuark		 pk_control_error_quark			(void);
GType		 pk_control_get_type		  	(void);
PkControl	*pk_control_new				(void);
void		 pk_control_test			(gpointer		 user_data);

void		 pk_control_get_tid_async		(PkControl		*control,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
gchar		*pk_control_get_tid_finish		(PkControl		*control,
							 GAsyncResult		*res,
							 GError			**error);
void		 pk_control_suggest_daemon_quit_async	(PkControl		*control,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
gboolean	 pk_control_suggest_daemon_quit_finish	(PkControl		*control,
							 GAsyncResult		*res,
							 GError			**error);
void		 pk_control_get_daemon_state_async	(PkControl		*control,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
gchar		*pk_control_get_daemon_state_finish	(PkControl		*control,
							 GAsyncResult		*res,
							 GError			**error);
void		 pk_control_set_proxy_async		(PkControl		*control,
							 const gchar		*proxy_http,
							 const gchar		*proxy_ftp,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
gboolean	 pk_control_set_proxy_finish		(PkControl		*control,
							 GAsyncResult		*res,
							 GError			**error);
void		 pk_control_set_root_async		(PkControl		*control,
							 const gchar		*root,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
gboolean	 pk_control_set_root_finish		(PkControl		*control,
							 GAsyncResult		*res,
							 GError			**error);
void		 pk_control_get_network_state_async	(PkControl		*control,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
PkNetworkEnum	 pk_control_get_network_state_finish	(PkControl		*control,
							 GAsyncResult		*res,
							 GError			**error);
void		 pk_control_get_time_since_action_async	(PkControl		*control,
							 PkRoleEnum		 role,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
guint		 pk_control_get_time_since_action_finish (PkControl		*control,
							 GAsyncResult		*res,
							 GError			**error);
void		 pk_control_get_transaction_list_async	(PkControl		*control,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
gchar		**pk_control_get_transaction_list_finish (PkControl		*control,
							 GAsyncResult		*res,
							 GError			**error);
void		 pk_control_can_authorize_async		(PkControl		*control,
							 const gchar		*action_id,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
PkAuthorizeEnum	 pk_control_can_authorize_finish	(PkControl		*control,
							 GAsyncResult		*res,
							 GError			**error);
void		 pk_control_get_properties_async	(PkControl		*control,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
gboolean	 pk_control_get_properties_finish	(PkControl		*control,
							 GAsyncResult		*res,
							 GError			**error);

G_END_DECLS

#endif /* __PK_CONTROL_H */

