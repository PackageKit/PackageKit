/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include "pk-debug.h"
#include "pk-common.h"
#include "pk-transaction-id.h"
#define PK_TRANSACTION_ID_COUNT_FILE		LOCALSTATEDIR "/run/PackageKit/job_count.dat"

/**
 * pk_transaction_id_get_random_hex_string:
 **/
static gchar *
pk_transaction_id_get_random_hex_string (guint length)
{
	GRand *rand;
	gint32 num;
	gchar *string;
	guint i;

	rand = g_rand_new ();

	/* allocate a string with the correct size */
	string = g_strnfill (length, 'x');
	for (i=0; i<length; i++) {
		num = g_rand_int_range (rand, (gint32) 'a', (gint32) 'f');
		/* assign a random number as a char */
		string[i] = (gchar) num;
	}
	g_rand_free (rand);
	return string;
}

/**
 * pk_transaction_id_load_job_count:
 **/
static guint
pk_transaction_id_load_job_count (void)
{
	gboolean ret;
	gchar *contents;
	guint job_count;
	ret = g_file_get_contents (PK_TRANSACTION_ID_COUNT_FILE, &contents, NULL, NULL);
	if (ret == FALSE) {
		pk_warning ("failed to get last job");
		return FALSE;
	}

	/* convert */
	ret = pk_strtouint (contents, &job_count);
	if (ret == FALSE) {
		pk_warning ("failed to convert");
	}

	/* check we got a sane number */
	if (job_count > 10240) {
		pk_warning ("invalid job count!");
		job_count = 0;
	}

	pk_debug ("job=%i", job_count);
	g_free (contents);
	return job_count;
}

/**
 * pk_transaction_id_save_job_count:
 **/
static gboolean
pk_transaction_id_save_job_count (guint job_count)
{
	gboolean ret;
	gchar *contents;

	pk_debug ("saving %i", job_count);
	contents = g_strdup_printf ("%i", job_count);
	ret = g_file_set_contents (PK_TRANSACTION_ID_COUNT_FILE, contents, -1, NULL);
	g_free (contents);
	if (ret == FALSE) {
		pk_warning ("failed to set last job");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_transaction_id_equal:
 **/
gboolean
pk_transaction_id_equal (const gchar *tid1, const gchar *tid2)
{
	return pk_strcmp_sections (tid1, tid2, 3, 2);
}

/**
 * pk_transaction_id_generate:
 **/
gchar *
pk_transaction_id_generate (void)
{
	gchar *rand_str;
	gchar *job;
	gchar *tid;
	guint job_count;

	/* load from file */
	job_count = pk_transaction_id_load_job_count ();
	rand_str = pk_transaction_id_get_random_hex_string (8);
	job = g_strdup_printf ("%i", job_count++);

	/* save the new value */
	pk_transaction_id_save_job_count (job_count);

	/* make the tid */
	tid = g_strjoin (";", job, rand_str, "data", NULL);

	g_free (rand_str);
	g_free (job);
	return tid;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_transaction_id (LibSelfTest *test)
{
	gchar *tid;
	gboolean ret;

	if (libst_start (test, "PkTransactionId", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************
	 ****************          IDENT           ******************
	 ************************************************************/
	libst_title (test, "get an tid object");
	tid = pk_transaction_id_generate ();
	if (tid != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}
	g_free (tid);

	/************************************************************/
	libst_title (test, "tid equal pass (same)");
	ret = pk_transaction_id_equal ("34;1234def;r23", "34;1234def;r23");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "tid equal pass (different)");
	ret = pk_transaction_id_equal ("34;1234def;unknown", "34;1234def;r23");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "tid equal fail 1");
	ret = pk_transaction_id_equal ("34;1234def;r23", "35;1234def;r23");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "tid equal fail 2");
	ret = pk_transaction_id_equal ("34;1234def;r23", "34;1234dff;r23");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_end (test);
}
#endif

