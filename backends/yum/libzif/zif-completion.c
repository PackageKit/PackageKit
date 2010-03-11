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

/**
 * SECTION:zif-completion
 * @short_description: A #ZifCompletion object allows progress reporting
 *
 * Objects can use zif_completion_set_percentage() if the absolute percentage
 * is known. Percentages should always go up, not down.
 *
 * Modules usually set the number of steps that are expected using
 * zif_completion_set_number_steps() and then after each section is completed,
 * the zif_completion_done() function should be called. This will automatically
 * call zif_completion_set_percentage() with the correct values.
 *
 * #ZifCompletion allows sub-modules to be "chained up" to the parent module
 * so that as the sub-module progresses, so does the parent.
 * The child can be reused for each section, and chains can be deep.
 *
 * To get a child object, you should use zif_completion_get_child() and then
 * use the result in any sub-process. You should ensure that the child object
 * is not re-used without calling zif_completion_done().
 *
 * There are a few nice touches in this module, so that if a module only has
 * one progress step, the child progress is used for updates.
 *
 *
 * <example>
 *   <title>Using a #ZifCompletion.</title>
 *   <programlisting>
 * static void
 * _do_something (ZifCompletion *completion)
 * {
 *	ZifCompletion *completion_local;
 *
 *	// setup correct number of steps
 *	zif_completion_set_number_steps (completion, 2);
 *
 *	// run a sub function
 *	completion_local = zif_completion_get_child (completion);
 *	_do_something_else1 (completion_local);
 *
 *	// this section done
 *	zif_completion_done (completion);
 *
 *	// run another sub function
 *	completion_local = zif_completion_get_child (completion);
 *	_do_something_else2 (completion_local);
 *
 *	// this section done (all complete)
 *	zif_completion_done (completion);
 * }
 *   </programlisting>
 * </example>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-utils.h"
#include "zif-completion.h"

#include "egg-debug.h"

#define ZIF_COMPLETION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_COMPLETION, ZifCompletionPrivate))

struct _ZifCompletionPrivate
{
	guint			 steps;
	guint			 current;
	guint			 last_percentage;
	ZifCompletion		*child;
	gulong			 percentage_child_id;
	gulong			 subpercentage_child_id;
};

enum {
	SIGNAL_PERCENTAGE_CHANGED,
	SIGNAL_SUBPERCENTAGE_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (ZifCompletion, zif_completion, G_TYPE_OBJECT)

/**
 * zif_completion_discrete_to_percent:
 * @discrete: The discrete level
 * @steps: The number of discrete steps
 *
 * We have to be carefull when converting from discrete->%.
 *
 * Return value: The percentage for this discrete value.
 **/
static gfloat
zif_completion_discrete_to_percent (guint discrete, guint steps)
{
	/* check we are in range */
	if (discrete > steps)
		return 100;
	if (steps == 0) {
		egg_warning ("steps is 0!");
		return 0;
	}
	return ((gfloat) discrete * (100.0f / (gfloat) (steps)));
}

/**
 * zif_completion_set_percentage:
 * @completion: the #ZifCompletion object
 * @percentage: A manual percentage value
 *
 * Set a percentage manually.
 * NOTE: this must be above what was previously set, or it will be rejected.
 *
 * Return value: %TRUE if the signal was propagated, %FALSE for failure
 *
 * Since: 0.0.1
 **/
gboolean
zif_completion_set_percentage (ZifCompletion *completion, guint percentage)
{
	/* is it the same */
	if (percentage == completion->priv->last_percentage)
		goto out;

	/* is it less */
	if (percentage < completion->priv->last_percentage) {
		egg_warning ("percentage cannot go down from %i to %i on %p!", completion->priv->last_percentage, percentage, completion);
		return FALSE;
	}

	/* emit and save */
	g_signal_emit (completion, signals [SIGNAL_PERCENTAGE_CHANGED], 0, percentage);
	completion->priv->last_percentage = percentage;
out:
	return TRUE;
}

/**
 * zif_completion_get_percentage:
 * @completion: the #ZifCompletion object
 *
 * Get the percentage completion.
 *
 * Return value: A percentage value, or G_MAXUINT for error
 *
 * Since: 0.0.1
 **/
guint
zif_completion_get_percentage (ZifCompletion *completion)
{
	return completion->priv->last_percentage;
}

/**
 * zif_completion_set_subpercentage:
 **/
static gboolean
zif_completion_set_subpercentage (ZifCompletion *completion, guint percentage)
{
	/* just emit */
	g_signal_emit (completion, signals [SIGNAL_SUBPERCENTAGE_CHANGED], 0, percentage);
	return TRUE;
}

/**
 * zif_completion_child_percentage_changed_cb:
 **/
static void
zif_completion_child_percentage_changed_cb (ZifCompletion *child, guint percentage, ZifCompletion *completion)
{
	gfloat offset;
	gfloat range;
	gfloat extra;

	/* propagate up the stack if ZifCompletion has only one step */
	if (completion->priv->steps == 1) {
		zif_completion_set_percentage (completion, percentage);
		return;
	}

	/* did we call done on a completion that did not have a size set? */
	if (completion->priv->steps == 0) {
		egg_warning ("done on a completion %p that did not have a size set!", completion);
		zif_debug_crash ();
		return;
	}

	/* always provide two levels of signals */
	zif_completion_set_subpercentage (completion, percentage);

	/* already at >= 100% */
	if (completion->priv->current >= completion->priv->steps) {
		egg_warning ("already at %i/%i steps on %p", completion->priv->current, completion->priv->steps, completion);
		return;
	}

	/* get the offset */
	offset = zif_completion_discrete_to_percent (completion->priv->current, completion->priv->steps);

	/* get the range between the parent step and the next parent step */
	range = zif_completion_discrete_to_percent (completion->priv->current+1, completion->priv->steps) - offset;
	if (range < 0.01) {
		egg_warning ("range=%f (from %i to %i), should be impossible", range, completion->priv->current+1, completion->priv->steps);
		return;
	}

	/* get the extra contributed by the child */
	extra = ((gfloat) percentage / 100.0f) * range;

	/* emit from the parent */
	zif_completion_set_percentage (completion, (guint) (offset + extra));
}

/**
 * zif_completion_child_subpercentage_changed_cb:
 **/
static void
zif_completion_child_subpercentage_changed_cb (ZifCompletion *child, guint percentage, ZifCompletion *completion)
{
	/* discard this, unless the ZifCompletion has only one step */
	if (completion->priv->steps != 1)
		return;

	/* propagate up the stack as if the parent didn't exist */
	zif_completion_set_subpercentage (completion, percentage);
}

/**
 * zif_completion_reset:
 * @completion: the #ZifCompletion object
 *
 * Resets the #ZifCompletion object to unset
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.0.1
 **/
gboolean
zif_completion_reset (ZifCompletion *completion)
{
	g_return_val_if_fail (ZIF_IS_COMPLETION (completion), FALSE);

	/* reset values */
	completion->priv->steps = 0;
	completion->priv->current = 0;
	completion->priv->last_percentage = 0;

	/* disconnect client */
	if (completion->priv->percentage_child_id != 0) {
		g_signal_handler_disconnect (completion->priv->child, completion->priv->percentage_child_id);
		completion->priv->percentage_child_id = 0;
	}
	if (completion->priv->subpercentage_child_id != 0) {
		g_signal_handler_disconnect (completion->priv->child, completion->priv->subpercentage_child_id);
		completion->priv->subpercentage_child_id = 0;
	}

	/* unref child */
	if (completion->priv->child != NULL) {
		g_object_unref (completion->priv->child);
		completion->priv->child = NULL;
	}

	return TRUE;
}

/**
 * zif_completion_get_child:
 * @completion: the #ZifCompletion object
 *
 * Monitor a child completion and proxy back up to the parent completion.
 * Yo udo not have to g_object_unref() this value.
 *
 * Return value: a new %ZifCompletion or %NULL for failure
 *
 * Since: 0.0.1
 **/
ZifCompletion *
zif_completion_get_child (ZifCompletion *completion)
{
	ZifCompletion *child = NULL;

	g_return_val_if_fail (ZIF_IS_COMPLETION (completion), FALSE);

	/* already set child */
	if (completion->priv->child != NULL) {
		g_signal_handler_disconnect (completion->priv->child, completion->priv->percentage_child_id);
		g_signal_handler_disconnect (completion->priv->child, completion->priv->subpercentage_child_id);
		g_object_unref (completion->priv->child);
	}

	/* connect up signals */
	child = zif_completion_new ();
	completion->priv->child = g_object_ref (child);
	completion->priv->percentage_child_id =
		g_signal_connect (child, "percentage-changed", G_CALLBACK (zif_completion_child_percentage_changed_cb), completion);
	completion->priv->subpercentage_child_id =
		g_signal_connect (child, "subpercentage-changed", G_CALLBACK (zif_completion_child_subpercentage_changed_cb), completion);

	/* reset child */
	child->priv->current = 0;
	child->priv->last_percentage = 0;

	return child;
}

/**
 * zif_completion_set_number_steps:
 * @completion: the #ZifCompletion object
 * @steps: The number of sub-tasks in this transaction
 *
 * Sets the number of sub-tasks, i.e. how many times the zif_completion_done()
 * function will be called in the loop.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.0.1
 **/
gboolean
zif_completion_set_number_steps (ZifCompletion *completion, guint steps)
{
	g_return_val_if_fail (ZIF_IS_COMPLETION (completion), FALSE);
	g_return_val_if_fail (steps != 0, FALSE);

	/* did we call done on a completion that did not have a size set? */
	if (completion->priv->steps != 0) {
		egg_warning ("steps already set (%i)!", completion->priv->steps);
		zif_debug_crash ();
		return FALSE;
	}

	/* imply reset */
	zif_completion_reset (completion);

	/* set steps */
	completion->priv->steps = steps;

	return TRUE;
}

/**
 * zif_completion_done:
 * @completion: the #ZifCompletion object
 *
 * Called when the current sub-task has finished.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.0.1
 **/
gboolean
zif_completion_done (ZifCompletion *completion)
{
	gfloat percentage;

	g_return_val_if_fail (ZIF_IS_COMPLETION (completion), FALSE);

	/* did we call done on a completion that did not have a size set? */
	if (completion->priv->steps == 0) {
		egg_warning ("done on a completion %p that did not have a size set!", completion);
		zif_debug_crash ();
		return FALSE;
	}

	/* is already at 100%? */
	if (completion->priv->current == completion->priv->steps) {
		egg_warning ("already at 100%% completion");
		return FALSE;
	}

	/* another */
	completion->priv->current++;

	/* find new percentage */
	percentage = zif_completion_discrete_to_percent (completion->priv->current, completion->priv->steps);
	zif_completion_set_percentage (completion, (guint) percentage);

	/* reset child if it exists */
	if (completion->priv->child != NULL)
		zif_completion_reset (completion->priv->child);

	return TRUE;
}

/**
 * zif_completion_finalize:
 **/
static void
zif_completion_finalize (GObject *object)
{
	ZifCompletion *completion;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_COMPLETION (object));
	completion = ZIF_COMPLETION (object);

	/* unref child too */
	zif_completion_reset (completion);

	G_OBJECT_CLASS (zif_completion_parent_class)->finalize (object);
}

/**
 * zif_completion_class_init:
 **/
static void
zif_completion_class_init (ZifCompletionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_completion_finalize;

	signals [SIGNAL_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ZifCompletionClass, percentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	signals [SIGNAL_SUBPERCENTAGE_CHANGED] =
		g_signal_new ("subpercentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ZifCompletionClass, subpercentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (ZifCompletionPrivate));
}

/**
 * zif_completion_init:
 **/
static void
zif_completion_init (ZifCompletion *completion)
{
	completion->priv = ZIF_COMPLETION_GET_PRIVATE (completion);
	completion->priv->child = NULL;
	completion->priv->steps = 0;
	completion->priv->current = 0;
	completion->priv->last_percentage = 0;
	completion->priv->percentage_child_id = 0;
	completion->priv->subpercentage_child_id = 0;
}

/**
 * zif_completion_new:
 *
 * Return value: A new #ZifCompletion class instance.
 *
 * Since: 0.0.1
 **/
ZifCompletion *
zif_completion_new (void)
{
	ZifCompletion *completion;
	completion = g_object_new (ZIF_TYPE_COMPLETION, NULL);
	return ZIF_COMPLETION (completion);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static guint _updates = 0;
static guint _last_percent = 0;
static guint _last_subpercent = 0;

static void
zif_completion_test_percentage_changed_cb (ZifCompletion *completion, guint value, gpointer data)
{
	_last_percent = value;
	_updates++;
}

static void
zif_completion_test_subpercentage_changed_cb (ZifCompletion *completion, guint value, gpointer data)
{
	_last_subpercent = value;
}

void
zif_completion_test (EggTest *test)
{
	ZifCompletion *completion;
	ZifCompletion *child;
	gboolean ret;

	if (!egg_test_start (test, "ZifCompletion"))
		return;

	/************************************************************/
	egg_test_title (test, "get completion");
	completion = zif_completion_new ();
	egg_test_assert (test, completion != NULL);
	g_signal_connect (completion, "percentage-changed", G_CALLBACK (zif_completion_test_percentage_changed_cb), NULL);
	g_signal_connect (completion, "subpercentage-changed", G_CALLBACK (zif_completion_test_subpercentage_changed_cb), NULL);

	/************************************************************/
	egg_test_title (test, "set steps");
	ret = zif_completion_set_number_steps (completion, 5);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "done one step");
	ret = zif_completion_done (completion);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "ensure 1 update");
	egg_test_assert (test, (_updates == 1));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 20));

	/************************************************************/
	egg_test_title (test, "done the rest");
	ret = zif_completion_done (completion);
	ret = zif_completion_done (completion);
	ret = zif_completion_done (completion);
	ret = zif_completion_done (completion);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "done one extra");
	ret = zif_completion_done (completion);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "ensure 5 updates");
	egg_test_assert (test, (_updates == 5));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 100));

	g_object_unref (completion);

	/* reset */
	_updates = 0;
	completion = zif_completion_new ();
	zif_completion_set_number_steps (completion, 2);
	g_signal_connect (completion, "percentage-changed", G_CALLBACK (zif_completion_test_percentage_changed_cb), NULL);
	g_signal_connect (completion, "subpercentage-changed", G_CALLBACK (zif_completion_test_subpercentage_changed_cb), NULL);

	// completion: |-----------------------|-----------------------|
	// step1:      |-----------------------|
	// child:                              |-------------|---------|

	/* PARENT UPDATE */
	zif_completion_done (completion);

	/************************************************************/
	egg_test_title (test, "ensure 1 update");
	egg_test_assert (test, (_updates == 1));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 50));

	/* now test with a child */
	child = zif_completion_get_child (completion);
	zif_completion_set_number_steps (child, 2);

	/* CHILD UPDATE */
	zif_completion_done (child);

	/************************************************************/
	egg_test_title (test, "ensure 2 updates");
	egg_test_assert (test, (_updates == 2));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 75));

	/* CHILD UPDATE */
	zif_completion_done (child);

	/************************************************************/
	egg_test_title (test, "ensure 3 updates");
	if (_updates == 3)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %i updates", _updates);

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 100));

	/* PARENT UPDATE */
	zif_completion_done (completion);

	/************************************************************/
	egg_test_title (test, "ensure 3 updates (and we ignored the duplicate)");
	if (_updates == 3)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %i updates", _updates);

	/************************************************************/
	egg_test_title (test, "ensure still correct percent");
	egg_test_assert (test, (_last_percent == 100));

	egg_debug ("unref completion");
	g_object_unref (completion);

	egg_debug ("unref child");
	g_object_unref (child);

	egg_debug ("reset");
	/* reset */
	_updates = 0;
	completion = zif_completion_new ();
	zif_completion_set_number_steps (completion, 1);
	g_signal_connect (completion, "percentage-changed", G_CALLBACK (zif_completion_test_percentage_changed_cb), NULL);
	g_signal_connect (completion, "subpercentage-changed", G_CALLBACK (zif_completion_test_subpercentage_changed_cb), NULL);

	/* now test with a child */
	child = zif_completion_get_child (completion);
	zif_completion_set_number_steps (child, 2);

	/* CHILD SET VALUE */
	zif_completion_set_percentage (child, 33);

	/************************************************************/
	egg_test_title (test, "ensure 1 updates for completion with one step");
	egg_test_assert (test, (_updates == 1));

	/************************************************************/
	egg_test_title (test, "ensure using child value as parent");
	egg_test_assert (test, (_last_percent == 33));

	g_object_unref (completion);
	g_object_unref (child);

	egg_test_end (test);
}
#endif

