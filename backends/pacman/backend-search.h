/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008, 2009 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010 Jonathan Conder <j@skurvy.no-ip.org>
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

#include <pk-backend.h>

void	 backend_get_packages	(PkBackend	*backend,
				 PkBitfield	 filters);
void	 backend_search_details	(PkBackend	*backend,
				 PkBitfield	 filters,
				 gchar		**values);
void	 backend_search_files	(PkBackend	*backend,
				 PkBitfield	 filters,
				 gchar		**values);
void	 backend_search_groups	(PkBackend	*backend,
				 PkBitfield	 filters,
				 gchar		**values);
void	 backend_search_names	(PkBackend	*backend,
				 PkBitfield	 filters,
				 gchar		**values);
void	 backend_what_provides	(PkBackend	*backend,
				 PkBitfield	 filters,
				 PkProvidesEnum	 provides,
				 gchar		**values);
