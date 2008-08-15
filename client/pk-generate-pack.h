/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2008 Shishir Goel <crazyontheedge@gmail.com>
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

#ifndef __PK_GENERATE_PACK_H
#define __PK_GENERATE_PACK_H

gchar		*pk_generate_pack_perhaps_resolve		(PkClient *client,
								 PkFilterEnum filter, 
								 const gchar *package,
								 GError **error);
gboolean	 pk_generate_pack_download_only 		(PkClient *client,
								 gchar **package_ids,
								 const gchar *directory);
gboolean	 pk_generate_pack_exclude_packages		(PkPackageList *list,
								 const gchar *package_list);
gboolean	 pk_generate_pack_set_metadata			(const gchar *full_path);
gboolean	 pk_generate_pack_create 			(const gchar *tarfilename,
								 GPtrArray *file_array,
								 GError **error);
GPtrArray 	*pk_generate_pack_scan_dir			(const gchar *directory);
gboolean	 pk_generate_pack_main				(const gchar *pack_filename,
								 const gchar *directory,
								 const gchar *package,
								 const gchar *package_list,
								 GError **error);

#endif /* __PK_GENERATE_PACK_H */
