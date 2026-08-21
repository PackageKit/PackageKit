/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2014 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2010-2026 Matthias Klumpp <matthias@tenstral.net>
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

#ifndef __PACKAGEKIT_H__
#define __PACKAGEKIT_H__

#define __PACKAGEKIT_H_INSIDE__

#include "pk-category.h"
#include "pk-client.h"
#include "pk-client-sync.h"
#include "pk-common.h"
#include "pk-control.h"
#include "pk-control-sync.h"
#include "pk-desktop.h"
#include "pk-details.h"
#include "pk-distro-upgrade.h"
#include "pk-enum.h"
#include "pk-enum-types.h"
#include "pk-error.h"
#include "pk-eula-required.h"
#include "pk-files.h"
#include "pk-media-change-required.h"
#include "pk-item-progress.h"
#include "pk-offline.h"
#include "pk-package-id.h"
#include "pk-package-ids.h"
#include "pk-package-sack.h"
#include "pk-package-sack-sync.h"
#include "pk-progress.h"
#include "pk-repo-detail.h"
#include "pk-repo-signature-required.h"
#include "pk-require-restart.h"
#include "pk-results.h"
#include "pk-task.h"
#include "pk-task-sync.h"
#include "pk-transaction-past.h"
#include "pk-transaction-list.h"
#include "pk-update-detail.h"
#include "pk-version.h"

#ifdef PK_COMPILATION
#include "pk-debug.h"
#endif

#undef __PACKAGEKIT_H_INSIDE__

#endif /* __PACKAGEKIT_H__ */
