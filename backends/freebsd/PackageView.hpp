/*
 * Copyright (C) Serenity Cybersecurity, LLC <license@futurecrew.ru>
 *               Author: Gleb Popov <arrowd@FreeBSD.org>
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

#pragma once

#include <pk-backend.h>
#include <pkg.h>

class PackageView
{
public:
    PackageView(struct pkg* pkg)
    : pk_id(nullptr) {
        name_el = pkg_get_element(pkg, PKG_NAME);
        version_el = pkg_get_element(pkg, PKG_VERSION);
        arch_el = pkg_get_element(pkg, (pkg_attr)XXX_PKG_ARCH); // TODO: Use pkg_asprintf() to get this
        reponame_el = pkg_get_element(pkg, PKG_REPONAME);
        comment_el = pkg_get_element(pkg, PKG_COMMENT);
    }

    ~PackageView() {
        g_free(pk_id);
    }

    const gchar* name() const {
        return name_el->string;
    }

    const gchar* version() const {
        return version_el->string;
    }

    const gchar* comment() const {
        return comment_el->string;
    }

    const gchar* arch() const {
        return arch_el->string;
    }

    const gchar* repository() const {
        return reponame_el->string;
    }

    gchar* packageKitId() {
        if (!pk_id)
            pk_id = pk_package_id_build(name_el->string, version_el->string,
                                        arch_el->string, reponame_el->string);
        return pk_id;
    }
private:
    struct pkg_el* name_el, *version_el,
        // TODO: arch or abi?
        *arch_el, *reponame_el, *comment_el;
    gchar* pk_id;
};
