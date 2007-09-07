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

#ifndef __PK_ACTION_LIST_H
#define __PK_ACTION_LIST_H

#include <glib-object.h>
#include <pk-enum.h>

G_BEGIN_DECLS

typedef GPtrArray PkActionList;
PkActionList	*pk_action_list_new			(PkTaskAction	 action, ...);
PkActionList	*pk_action_list_new_from_string		(const gchar	*actions);
gchar		*pk_action_list_to_string		(PkActionList	*alist);
gboolean	 pk_action_list_contains		(PkActionList	*alist,
							 PkTaskAction	 action);
gboolean	 pk_action_list_append			(PkActionList	*alist,
							 PkTaskAction	 action);
gboolean	 pk_action_list_free			(PkActionList	*alist);

G_END_DECLS

#endif /* __PK_ACTION_LIST_H */
