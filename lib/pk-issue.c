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

/**
 * PkIssue:
 *
 * One issue reference from an update.
 * It contains a title and a URL to a page describing the issue.
 *
 * Since: 2.0.0
 */

#include "config.h"

#include "pk-issue.h"

struct _PkIssue
{
	PkIssueKindEnum kind;
	gchar *url;
	gchar *id;
	gchar *title;
};

G_DEFINE_BOXED_TYPE (PkIssue, pk_issue, pk_issue_copy, pk_issue_free)

/**
 * pk_issue_new:
 * @kind: the issue kind
 * @url: where a user can read about the issue
 * @id: (nullable): a short identifier for display, e.g. `1234567` or `CVE-2026-0001`
 * @title: (nullable): a one-line summary of the issue
 *
 * Creates a new issue reference.
 *
 * Returns: (transfer full): a new #PkIssue
 *
 * Since: 2.0.0
 */
PkIssue *
pk_issue_new (PkIssueKindEnum kind, const gchar *url, const gchar *id, const gchar *title)
{
	PkIssue *issue;

	g_return_val_if_fail (url != NULL, NULL);

	issue = g_new0 (PkIssue, 1);
	issue->kind = kind;
	issue->url = g_strdup (url);
	if (id != NULL && id[0] != '\0')
		issue->id = g_strdup (id);
	if (title != NULL && title[0] != '\0')
		issue->title = g_strdup (title);
	return issue;
}

/**
 * pk_issue_copy:
 * @issue: a #PkIssue
 *
 * Returns: (transfer full): a copy of @issue
 *
 * Since: 2.0.0
 */
PkIssue *
pk_issue_copy (PkIssue *issue)
{
	g_return_val_if_fail (issue != NULL, NULL);
	return pk_issue_new (issue->kind, issue->url, issue->id, issue->title);
}

/**
 * pk_issue_free:
 * @issue: a #PkIssue
 *
 * Frees @issue.
 *
 * Since: 2.0.0
 */
void
pk_issue_free (PkIssue *issue)
{
	if (issue == NULL)
		return;
	g_free (issue->url);
	g_free (issue->id);
	g_free (issue->title);
	g_free (issue);
}

/**
 * pk_issue_get_kind:
 * @issue: a #PkIssue
 *
 * Returns: the kind of issue being referenced
 *
 * Since: 2.0.0
 */
PkIssueKindEnum
pk_issue_get_kind (PkIssue *issue)
{
	g_return_val_if_fail (issue != NULL, PK_ISSUE_KIND_ENUM_UNKNOWN);
	return issue->kind;
}

/**
 * pk_issue_get_url:
 * @issue: a #PkIssue
 *
 * Returns: the URL where a user can read about the issue
 *
 * Since: 2.0.0
 */
const gchar *
pk_issue_get_url (PkIssue *issue)
{
	g_return_val_if_fail (issue != NULL, NULL);
	return issue->url;
}

/**
 * pk_issue_get_id:
 * @issue: a #PkIssue
 *
 * Returns: (nullable): the short identifier of the issue, for display
 *
 * Since: 2.0.0
 */
const gchar *
pk_issue_get_id (PkIssue *issue)
{
	g_return_val_if_fail (issue != NULL, NULL);
	return issue->id;
}

/**
 * pk_issue_get_title:
 * @issue: a #PkIssue
 *
 * Returns: (nullable): the one-line summary of the issue
 *
 * Since: 2.0.0
 */
const gchar *
pk_issue_get_title (PkIssue *issue)
{
	g_return_val_if_fail (issue != NULL, NULL);
	return issue->title;
}
