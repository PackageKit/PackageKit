/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offtask_text: 8 -*-
 *
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_AUTOCLEANUPS_H
#define __PK_AUTOCLEANUPS_H

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC

G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkCategory, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkClient, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkControl, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkDesktop, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkDetails, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkDistroUpgrade, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkError, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkEulaRequired, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkFiles, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkItemProgress, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkMediaChangeRequired, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkPackage, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkPackageSack, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkProgress, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkRepoDetail, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkRepoSignatureRequired, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkRequireRestart, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkResults, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkSource, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkTask, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkTransactionList, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkTransactionPast, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkUpdateDetail, g_object_unref)

#endif

#endif /* __PK_AUTOCLEANUPS_H */
