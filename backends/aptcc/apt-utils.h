/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 200 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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

#ifndef APT_UTILS_H
#define APT_UTILS_H

#include <apt-pkg/depcache.h>
#include <apt-pkg/pkgrecords.h>

#include <packagekit-glib/packagekit.h>
#include <pk-backend.h>

/** \return a short description string corresponding to the given
 *  version.
 */
std::string get_short_description(const pkgCache::VerIterator &ver,
                                   pkgRecords *records);

/** \return a short description string corresponding to the given
 *  version.
 */
std::string get_long_description(const pkgCache::VerIterator &ver,
                                   pkgRecords *records);

/**
  * Return the PkEnumGroup of the give group string.
  */
PkGroupEnum get_enum_group (std::string group);
#endif