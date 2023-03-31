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
    : pk_id(nullptr), pk_id_parts(nullptr), pk_namever(nullptr) {
        name_el = pkg_get_element(pkg, PKG_NAME);
        version_el = pkg_get_element(pkg, PKG_VERSION);
        arch_el = pkg_get_element(pkg, (pkg_attr)XXX_PKG_ARCH); // TODO: Use pkg_asprintf() to get this
        reponame_el = pkg_get_element(pkg, PKG_REPONAME);
        comment_el = pkg_get_element(pkg, PKG_COMMENT);
    }

    PackageView(gchar* package_id)
    : pk_id (package_id), pk_namever(nullptr) {
        g_assert (pk_package_id_check (pk_id));
        pk_id_parts = pk_package_id_split (pk_id);
    }

    PackageView(PackageView&& other)
    : name_el(other.name_el), version_el(other.version_el),
      arch_el(other.arch_el), reponame_el(other.reponame_el),
      comment_el(other.comment_el), pk_id(other.pk_id), free_pk_id(other.free_pk_id),
      pk_id_parts(other.pk_id_parts), pk_namever(other.pk_namever) {
          other.free_pk_id = false;
          other.pk_id_parts = nullptr;
          other.pk_namever = nullptr;
      }

    PackageView(const PackageView&) = delete;

    ~PackageView() {
        if (free_pk_id)
            g_free(pk_id);
        if (pk_id_parts)
            g_strfreev (pk_id_parts);
        if (pk_namever)
            g_free(pk_namever);
    }

    const gchar* name() const {
        if (pk_id_parts)
            return pk_id_parts[PK_PACKAGE_ID_NAME];
        else
            return name_el->string;
    }

    const gchar* version() const {
        if (pk_id_parts)
            return pk_id_parts[PK_PACKAGE_ID_VERSION];
        else
            return version_el->string;
    }

    gchar* nameversion() {
        if (pk_namever)
            return pk_namever;

        pk_namever = g_strconcat(name(), "-", version(), NULL);
        return pk_namever;
    }

    const gchar* comment() const {
        // comment can only be obtained from pkg*
        g_assert (pk_id_parts == nullptr);
        return comment_el->string;
    }

    const gchar* arch() const {
        if (pk_id_parts)
            return pk_id_parts[PK_PACKAGE_ID_ARCH];
        else
            return arch_el->string;
    }

    const gchar* repository() const {
        if (pk_id_parts)
            return pk_id_parts[PK_PACKAGE_ID_DATA];
        else
            return reponame_el->string;
    }

    gchar* packageKitId() {
        if (!pk_id) {
            pk_id = pk_package_id_build(name_el->string, version_el->string,
                                        arch_el->string, reponame_el->string);
            free_pk_id = true;
        }
        return pk_id;
    }
private:
    struct pkg_el* name_el, *version_el,
        // TODO: arch or abi?
        *arch_el, *reponame_el, *comment_el;
    gchar* pk_id;
    bool free_pk_id = false;
    gchar** pk_id_parts;
    gchar* pk_namever;
};
