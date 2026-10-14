/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(__PACKAGEKIT_H_INSIDE__) && !defined(PK_COMPILATION)
#error "Only <packagekit-glib2/packagekit.h> can be included directly."
#endif

#ifndef __PK_ISSUE_H
#define __PK_ISSUE_H

#include <glib-object.h>

#include "pk-enum.h"

G_BEGIN_DECLS

#define PK_TYPE_ISSUE (pk_issue_get_type ())

typedef struct _PkIssue PkIssue;

GType			pk_issue_get_type (void);
PkIssue		       *pk_issue_new (PkIssueKindEnum kind,
				      const gchar    *url,
				      const gchar    *id,
				      const gchar    *title);
PkIssue		       *pk_issue_copy (PkIssue *issue);
void			pk_issue_free (PkIssue *issue);

PkIssueKindEnum		pk_issue_get_kind (PkIssue *issue);
const gchar	       *pk_issue_get_url (PkIssue *issue);
const gchar	       *pk_issue_get_id (PkIssue *issue);
const gchar	       *pk_issue_get_title (PkIssue *issue);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PkIssue, pk_issue_free)

G_END_DECLS

#endif /* __PK_ISSUE_H */
