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

#include <vector>
#include <memory>

#include <glib.h>

// alias for an unique_ptr with custom deleter
template<typename T>
using deleted_unique_ptr = std::unique_ptr<T,std::function<void(T*)>>;


// a variant of std::default_delete that calls free() instead of delete
template <class T>
struct free_delete {
  inline constexpr free_delete() noexcept = default;
  template <class U>
  inline
  free_delete(const free_delete<U>&,
                 typename std::enable_if<std::is_convertible<U*, T*>::value>::type* =
                     0) noexcept {}

  inline void operator()(T* __ptr) const noexcept {
    free(__ptr);
  }
};

// alias for an unique_ptr with free() deleter
template<typename T>
using free_deleted_unique_ptr = std::unique_ptr<T,free_delete<T>>;


// a variant of std::default_delete that calls g_free() instead of delete
template <class T>
struct g_free_delete {
  inline constexpr g_free_delete() noexcept = default;
  template <class U>
  inline
  g_free_delete(const g_free_delete<U>&,
                 typename std::enable_if<std::is_convertible<U*, T*>::value>::type* =
                     0) noexcept {}

  inline void operator()(T* __ptr) const noexcept {
    g_free(__ptr);
  }
};

// alias for an unique_ptr with free() deleter
template<typename T>
using g_free_deleted_unique_ptr = std::unique_ptr<T,g_free_delete<T>>;


// a variant of std::default_delete that calls g_strfreev() instead of delete
struct g_strfreev_delete {
  inline constexpr g_strfreev_delete() noexcept = default;

  inline void operator()(gchar** __ptr) const noexcept {
    g_strfreev(__ptr);
  }
};

// alias for an unique_ptr with g_strfreev() deleter
template<typename T>
using g_strfreev_deleted_unique_ptr = std::unique_ptr<T,g_strfreev_delete>;

// a variant of std::vector that g_free()'s its elements on destruction

class gchar_ptr_vector : public std::vector<gchar*> {
public:
  ~gchar_ptr_vector() {
    for (gchar* ptr : *this)
      g_free (ptr);
  }
};
