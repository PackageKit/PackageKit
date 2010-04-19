/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#if !defined (__ZIF_H_INSIDE__) && !defined (ZIF_COMPILATION)
#error "Only <zif.h> can be included directly."
#endif

#ifndef __ZIF_COMPLETION_H
#define __ZIF_COMPLETION_H

#include <glib-object.h>

G_BEGIN_DECLS

#define ZIF_TYPE_COMPLETION		(zif_completion_get_type ())
#define ZIF_COMPLETION(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_COMPLETION, ZifCompletion))
#define ZIF_COMPLETION_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_COMPLETION, ZifCompletionClass))
#define ZIF_IS_COMPLETION(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_COMPLETION))
#define ZIF_IS_COMPLETION_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_COMPLETION))
#define ZIF_COMPLETION_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_COMPLETION, ZifCompletionClass))

typedef struct _ZifCompletion		ZifCompletion;
typedef struct _ZifCompletionPrivate	ZifCompletionPrivate;
typedef struct _ZifCompletionClass	ZifCompletionClass;

struct _ZifCompletion
{
	GObject			 parent;
	ZifCompletionPrivate	*priv;
};

struct _ZifCompletionClass
{
	GObjectClass	 parent_class;
	/* Signals */
	void		(* percentage_changed)		(ZifCompletion	*completion,
							 guint		 value);
	void		(* subpercentage_changed)	(ZifCompletion	*completion,
							 guint		 value);
	/* Padding for future expansion */
	void (*_zif_reserved1) (void);
	void (*_zif_reserved2) (void);
	void (*_zif_reserved3) (void);
	void (*_zif_reserved4) (void);
};

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define zif_completion_done(completion)				zif_completion_done_real (completion, __func__, __LINE__)
#define zif_completion_finished(completion)			zif_completion_finished_real (completion, __func__, __LINE__)
#define zif_completion_set_number_steps(completion, steps)	zif_completion_set_number_steps_real (completion, steps, __func__, __LINE__)
#elif defined(__GNUC__) && __GNUC__ >= 3
#define zif_completion_done(completion)				zif_completion_done_real (completion, __FUNCTION__, __LINE__)
#define zif_completion_finished(completion)			zif_completion_finished_real (completion, __FUNCTION__, __LINE__)
#define zif_completion_set_number_steps(completion, steps)	zif_completion_set_number_steps_real (completion, steps, __FUNCTION__, __LINE__)
#else
#define zif_completion_done(completion)
#define zif_completion_finished(completion)
#define zif_completion_set_number_steps(completion, steps)
#endif

GType		 zif_completion_get_type		(void);
ZifCompletion	*zif_completion_new			(void);
ZifCompletion	*zif_completion_get_child		(ZifCompletion		*completion);
gboolean	 zif_completion_set_number_steps_real	(ZifCompletion		*completion,
							 guint			 steps,
							 const gchar		*function_name,
							 gint			 function_line);
gboolean	 zif_completion_set_percentage		(ZifCompletion		*completion,
							 guint			 percentage);
guint		 zif_completion_get_percentage		(ZifCompletion		*completion);
gboolean	 zif_completion_done_real		(ZifCompletion		*completion,
							 const gchar		*function_name,
							 gint			 function_line);
gboolean	 zif_completion_finished_real		(ZifCompletion		*completion,
							 const gchar		*function_name,
							 gint			 function_line);
gboolean	 zif_completion_reset			(ZifCompletion		*completion);

G_END_DECLS

#endif /* __ZIF_COMPLETION_H */

