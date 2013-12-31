/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "hif-sack.h"
#include "hif-utils.h"

/**
 * hif_sack_add_source:
 */
gboolean
hif_sack_add_source (HySack sack,
		     HifSource *src,
		     HifSackAddFlags flags,
		     HifState *state,
		     GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	gint rc;
	HifState *state_local;
	int flags_hy = HY_BUILD_CACHE;

	/* set state */
	ret = hif_state_set_steps (state, error,
				   5, /* check repo */
				   95, /* load solv */
				   -1);
	if (!ret)
		goto out;

	/* check repo */
	state_local = hif_state_get_child (state);
	ret = hif_source_check (src, state_local, &error_local);
	if (!ret) {
		g_debug ("failed to check, attempting update: %s",
			 error_local->message);
		g_clear_error (&error_local);
		hif_state_reset (state_local);
		ret = hif_source_update (src,
					 HIF_SOURCE_UPDATE_FLAG_FORCE,
					 state_local,
					 &error_local);
		if (!ret) {
			if (g_error_matches (error_local,
					     HIF_ERROR,
					     PK_ERROR_ENUM_CANNOT_FETCH_SOURCES)) {
				ret = TRUE;
				g_warning ("Skipping refresh of %s: %s",
					   hif_source_get_id (src),
					   error_local->message);
				g_error_free (error_local);
			} else {
				g_propagate_error (error, error_local);
			}
			goto out;
		}
	}

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* only load what's required */
	if ((flags & HIF_SACK_ADD_FLAG_FILELISTS) > 0)
		flags_hy |= HY_LOAD_FILELISTS;
	if ((flags & HIF_SACK_ADD_FLAG_UPDATEINFO) > 0)
		flags_hy |= HY_LOAD_UPDATEINFO;

	/* load solv */
	g_debug ("Loading repo %s", hif_source_get_id (src));
	hif_state_action_start (state, PK_STATUS_ENUM_LOADING_CACHE, NULL);
	rc = hy_sack_load_yum_repo (sack, hif_source_get_repo (src), flags_hy);
	ret = hif_rc_to_gerror (rc, error);
	if (!ret) {
		g_prefix_error (error, "Failed to load repo %s: ",
				hif_source_get_id (src));
		goto out;
	}

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * hif_sack_add_sources:
 */
gboolean
hif_sack_add_sources (HySack sack,
		      GPtrArray *sources,
		      HifSackAddFlags flags,
		      HifState *state,
		      GError **error)
{
	gboolean ret = TRUE;
	guint cnt = 0;
	guint i;
	HifSource *src;
	HifState *state_local;

	/* count the enabled sources */
	for (i = 0; i < sources->len; i++) {
		src = g_ptr_array_index (sources, i);
		if (hif_source_get_enabled (src))
			cnt++;
	}

	/* add each repo */
	hif_state_set_number_steps (state, cnt);
	for (i = 0; i < sources->len; i++) {
		src = g_ptr_array_index (sources, i);
		if (!hif_source_get_enabled (src))
			continue;

		state_local = hif_state_get_child (state);
		ret = hif_sack_add_source (sack, src, flags, state_local, error);
		if (!ret)
			goto out;

		/* done */
		ret = hif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* success */
	ret = TRUE;
out:
	return ret;
}
