/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Tests of the helper's pure logic; they need neither poldek data nor a daemon. */

#include <string.h>

#include <gio/gio.h>
#include <pk-package-id.h>

#include <poldek/pkgdir/pkgdir.h>
#include <poldek/poldek.h>

#include "errors.h"
#include "filters.h"
#include "groups.h"
#include "job.h"
#include "pkgid.h"
#include "progress.h"
#include "protocol.h"

static void
pk_poldek_test_line_reader_func (void)
{
	const gchar *stream = "{\"op\":\"hello\"}\n\n{\"op\":\"exit\"}\npartial";
	const gchar *expected[] = { "{\"op\":\"hello\"}", "", "{\"op\":\"exit\"}" };
	gsize stream_len = strlen (stream);

	/* every split of the stream into two chunks gives the same lines */
	for (gsize split = 0; split <= stream_len; split++) {
		PkPoldekLineReader reader;
		g_autoptr(GPtrArray) lines = g_ptr_array_new_with_free_func (g_free);
		gboolean too_long;
		gchar *line;

		pk_poldek_line_reader_init (&reader);
		pk_poldek_line_reader_feed (&reader, stream, split);
		while ((line = pk_poldek_line_reader_next (&reader, &too_long)) != NULL)
			g_ptr_array_add (lines, line);
		g_assert_false (too_long);
		pk_poldek_line_reader_feed (&reader, stream + split, stream_len - split);
		while ((line = pk_poldek_line_reader_next (&reader, &too_long)) != NULL)
			g_ptr_array_add (lines, line);
		g_assert_false (too_long);

		g_assert_cmpuint (lines->len, ==, G_N_ELEMENTS (expected));
		for (guint i = 0; i < lines->len; i++)
			g_assert_cmpstr (g_ptr_array_index (lines, i), ==, expected[i]);
		g_assert_cmpstr (reader.buf->str, ==, "partial");
		pk_poldek_line_reader_clear (&reader);
	}
}

static void
pk_poldek_test_line_reader_limit_func (void)
{
	PkPoldekLineReader reader;
	g_autofree gchar *chunk = g_malloc (1024 * 1024);
	gboolean too_long;
	gchar *line;

	memset (chunk, 'x', 1024 * 1024);
	pk_poldek_line_reader_init (&reader);

	/* exactly at the limit is still fine */
	for (guint i = 0; i < PK_POLDEK_PROTOCOL_MAX_LINE / (1024 * 1024); i++)
		pk_poldek_line_reader_feed (&reader, chunk, 1024 * 1024);
	g_assert_null (pk_poldek_line_reader_next (&reader, &too_long));
	g_assert_false (too_long);

	pk_poldek_line_reader_feed (&reader, "x", 1);
	g_assert_null (pk_poldek_line_reader_next (&reader, &too_long));
	g_assert_true (too_long);

	/* a newline ends the line regardless of how much was scanned before */
	pk_poldek_line_reader_feed (&reader, "\n", 1);
	line = pk_poldek_line_reader_next (&reader, &too_long);
	g_assert_nonnull (line);
	g_assert_cmpuint (strlen (line), ==, PK_POLDEK_PROTOCOL_MAX_LINE + 1);
	g_free (line);
	pk_poldek_line_reader_clear (&reader);
}

static void
pk_poldek_test_parse_request_func (void)
{
	const gchar *invalid[] = {
		"",
		"not json",
		"[\"op\"]",
		"{}",
		"{\"op\":1}",
		"{\"op\":\"run\",\"job\":7}",
		"{\"op\":\"run\",\"op\":\"exit\"}",
	};
	json_t *msg;

	msg = pk_poldek_protocol_parse_request (
	    "{\"op\":\"run\",\"job\":\"1\",\"role\":\"resolve\",\"x\":null}",
	    NULL);
	g_assert_nonnull (msg);
	g_assert_cmpstr (json_string_value (json_object_get (msg, "job")), ==, "1");
	json_decref (msg);

	msg = pk_poldek_protocol_parse_request ("{\"op\":\"exit\"}", NULL);
	g_assert_nonnull (msg);
	g_assert_null (json_object_get (msg, "job"));
	json_decref (msg);

	for (guint i = 0; i < G_N_ELEMENTS (invalid); i++) {
		g_autoptr(GError) error = NULL;

		msg = pk_poldek_protocol_parse_request (invalid[i], &error);
		g_assert_null (msg);
		g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	}
}

static void
pk_poldek_test_groups_func (void)
{
	struct
	{
		const gchar *rpm;
		PkGroupEnum group;
	} cases[] = {
		{ "Applications/Mail", PK_GROUP_ENUM_INTERNET },
		/* whole path components only */
		{ "Applications/Mailers", PK_GROUP_ENUM_UTILITIES },
		{ "X11/Applications/Games/Strategy", PK_GROUP_ENUM_GAMES },
		{ "Development/Debug", PK_GROUP_ENUM_DEBUG },
		{ "Development/Debuggers", PK_GROUP_ENUM_PROGRAMMING },
		{ "Development/Libraries/Libc", PK_GROUP_ENUM_DEVEL },
		{ "Base/Kernel", PK_GROUP_ENUM_SYSTEM },
		{ "Libraries/Python", PK_GROUP_ENUM_LANG_PYTHON },
		{ "Unheard/Of", PK_GROUP_ENUM_OTHER },
		{ "", PK_GROUP_ENUM_OTHER },
		{ NULL, PK_GROUP_ENUM_OTHER },
	};
	PkBitfield used = pk_poldek_groups_used ();

	for (guint i = 0; i < G_N_ELEMENTS (cases); i++) {
		PkGroupEnum group = pk_poldek_group_from_rpm (cases[i].rpm);

		g_assert_cmpstr (pk_group_enum_to_string (group),
				 ==,
				 pk_group_enum_to_string (cases[i].group));
		g_assert_true (pk_bitfield_contain (used, group));
	}
	/* the daemon rejects details with it */
	g_assert_false (pk_bitfield_contain (used, PK_GROUP_ENUM_UNKNOWN));
}

static struct pkg *
pk_poldek_test_pkg_new (const gchar *name,
			gint32 epoch,
			const gchar *version,
			const gchar *arch,
			struct pkgdir *pkgdir)
{
	struct pkg *
	    pkg = pkg_new_ext (NULL, name, epoch, version, "1", arch, "linux", NULL, NULL, 0, 0, 0);

	pkg->pkgdir = pkgdir;
	return pkg;
}

static void
pk_poldek_test_pkgid_func (void)
{
	struct pkgdir pkgdir = { .name = (gchar *) "repo-a" };
	struct pkg *pkg = pk_poldek_test_pkg_new ("foo", 2, "1.0", "x86_64", &pkgdir);
	struct pkg *other = pk_poldek_test_pkg_new ("foo", 0, "1.0", "x86_64", &pkgdir);
	struct
	{
		PkPoldekPkgKind kind;
		const gchar *id;
	} kinds[] = {
		{ PK_POLDEK_PKG_AVAILABLE, "foo;2:1.0-1;x86_64;repo-a;" },
		{ PK_POLDEK_PKG_INSTALLED, "foo;2:1.0-1;x86_64;;installed" },
	};
	const gchar *invalid[] = {
		"foo",
		"foo;1.0-1;x86_64;repo-a",
		";1.0-1;x86_64;repo-a;",
		"foo;;x86_64;repo-a;",
		"foo;1.0-1;;repo-a;",
		/* installed without the marker, or with both an origin and data */
		"foo;1.0-1;x86_64;;",
		"foo;1.0-1;x86_64;repo-a;installed",
		"foo;1.0-1;x86_64;local;x",
	};

	for (guint i = 0; i < G_N_ELEMENTS (kinds); i++) {
		g_autofree gchar *id = pk_poldek_pkgid_build (pkg, kinds[i].kind);
		PkPoldekPkgId parsed = { 0 };

		g_assert_cmpstr (id, ==, kinds[i].id);
		g_assert_true (pk_poldek_pkgid_parse (id, &parsed, NULL));
		g_assert_cmpint (parsed.kind, ==, kinds[i].kind);
		g_assert_true (pk_poldek_pkgid_matches (&parsed, pkg));
		/* the epoch is part of the version */
		g_assert_false (pk_poldek_pkgid_matches (&parsed, other));
		pk_poldek_pkgid_clear (&parsed);
	}
	for (guint i = 0; i < G_N_ELEMENTS (invalid); i++) {
		g_autoptr(GError) error = NULL;
		PkPoldekPkgId parsed = { 0 };

		g_assert_false (pk_poldek_pkgid_parse (invalid[i], &parsed, &error));
		g_assert_error (error, PK_POLDEK_ERROR, PK_ERROR_ENUM_PACKAGE_ID_INVALID);
		g_assert_null (parsed.split);
	}

	/* a package file of install-files has origin local, as any id from a source named local */
	{
		struct pkgdir local = { .name = (gchar *) PK_POLDEK_LOCAL_SOURCE };
		struct pkg *file = pk_poldek_test_pkg_new ("foo", 2, "1.0", "x86_64", &local);
		g_autofree gchar *id = pk_poldek_pkgid_build (file, PK_POLDEK_PKG_AVAILABLE);
		PkPoldekPkgId parsed = { 0 };

		g_assert_cmpstr (id, ==, "foo;2:1.0-1;x86_64;local;");
		g_assert_true (pk_poldek_pkgid_parse (id, &parsed, NULL));
		g_assert_cmpint (parsed.kind, ==, PK_POLDEK_PKG_AVAILABLE);
		pk_poldek_pkgid_clear (&parsed);
		pkg_free (file);
	}
	pkg_free (pkg);
	pkg_free (other);
}

static void
pk_poldek_test_newest_func (void)
{
	struct pkgdir pkgdir = { .name = (gchar *) "repo-a" };
	struct pkg *pkgs[] = {
		pk_poldek_test_pkg_new ("foo", 0, "1.0", "x86_64", &pkgdir),
		pk_poldek_test_pkg_new ("bar", 0, "1", "noarch", &pkgdir),
		pk_poldek_test_pkg_new ("foo", 0, "2.0", "x86_64", &pkgdir),
		pk_poldek_test_pkg_new ("foo", 0, "1.5", "i686", &pkgdir),
		/* an epoch beats any version */
		pk_poldek_test_pkg_new ("baz", 1, "0.1", "noarch", &pkgdir),
		pk_poldek_test_pkg_new ("baz", 0, "9.9", "noarch", &pkgdir),
	};
	g_autoptr(GPtrArray) array = g_ptr_array_new ();
	g_autoptr(GPtrArray) older = g_ptr_array_new ();

	for (guint i = 0; i < G_N_ELEMENTS (pkgs); i++) {
		g_ptr_array_add (array, pkgs[i]);
		g_ptr_array_add (older, pkgs[i]);
	}
	pk_poldek_filter_newest (array, TRUE);
	g_assert_cmpuint (array->len, ==, 4);
	/* the order of what is kept does not change */
	g_assert_true (g_ptr_array_index (array, 0) == pkgs[1]);
	g_assert_true (g_ptr_array_index (array, 1) == pkgs[2]);
	g_assert_true (g_ptr_array_index (array, 2) == pkgs[3]);
	g_assert_true (g_ptr_array_index (array, 3) == pkgs[4]);
	/* ~newest: the rest */
	pk_poldek_filter_newest (older, FALSE);
	g_assert_cmpuint (older->len, ==, 2);
	g_assert_true (g_ptr_array_index (older, 0) == pkgs[0]);
	g_assert_true (g_ptr_array_index (older, 1) == pkgs[5]);
	for (guint i = 0; i < G_N_ELEMENTS (pkgs); i++)
		pkg_free (pkgs[i]);
}

static void
pk_poldek_test_devel_func (void)
{
	const gchar *devel[] = { "foo-devel", "foo-static", "foo-debuginfo", "foo-debugsource" };
	const gchar *not_devel[] = { "foo", "develop", "foo-devel-doc" };

	for (guint i = 0; i < G_N_ELEMENTS (devel); i++) {
		struct pkg *pkg = pk_poldek_test_pkg_new (devel[i], 0, "1", "noarch", NULL);

		g_assert_true (pk_poldek_pkg_is_devel (pkg));
		pkg_free (pkg);
	}
	for (guint i = 0; i < G_N_ELEMENTS (not_devel); i++) {
		struct pkg *pkg = pk_poldek_test_pkg_new (not_devel[i], 0, "1", "noarch", NULL);

		g_assert_false (pk_poldek_pkg_is_devel (pkg));
		pkg_free (pkg);
	}
}

/* rpm -vh output of an upgrade, as rpm 6.1 writes it */
static const gchar *pk_poldek_test_rpm_output =
    "Verifying...                          ########################################\n"
    "Preparing...                          ########################################\n"
    "Updating / installing...\n"
    "libbar-devel-1.0-1                    ########################################\n"
    "foo-1.1-1                             ########################################\n"
    "Cleaning up / removing...\n"
    "foo-1.0-1                             ########################################\n";

typedef struct
{
	GString *events;
	gchar *bar;
	guint percentage;
} PkPoldekTestRpm;

static void
pk_poldek_test_rpm_flush (PkPoldekTestRpm *rec)
{
	if (rec->bar != NULL)
		g_string_append_printf (rec->events, "%s %u\n", rec->bar, rec->percentage);
	g_clear_pointer (&rec->bar, g_free);
}

static void
pk_poldek_test_rpm_phase_cb (gpointer user_data, PkPoldekRpmPhase phase)
{
	PkPoldekTestRpm *rec = user_data;

	pk_poldek_test_rpm_flush (rec);
	g_string_append_printf (rec->events, "phase %d\n", phase);
}

/* keeps the last value of each bar, which must only grow */
static void
pk_poldek_test_rpm_progress_cb (gpointer user_data,
				PkPoldekRpmPhase phase,
				const gchar *nvr,
				guint percentage)
{
	PkPoldekTestRpm *rec = user_data;
	g_autofree gchar *bar = g_strdup_printf ("%d %s", phase, nvr != NULL ? nvr : "-");

	if (g_strcmp0 (bar, rec->bar) == 0) {
		g_assert_cmpuint (percentage, >, rec->percentage);
	} else {
		pk_poldek_test_rpm_flush (rec);
		rec->bar = g_steal_pointer (&bar);
	}
	g_assert_cmpuint (percentage, <=, 100);
	rec->percentage = percentage;
}

static gchar *
pk_poldek_test_rpm_parse (const gchar *const *chunks)
{
	PkPoldekTestRpm rec = { g_string_new (NULL), NULL, 0 };
	PkPoldekRpmParser parser;

	pk_poldek_rpm_parser_init (&parser,
				   pk_poldek_test_rpm_phase_cb,
				   pk_poldek_test_rpm_progress_cb,
				   &rec);
	for (guint i = 0; chunks[i] != NULL; i++)
		pk_poldek_rpm_parser_feed (&parser, chunks[i]);
	pk_poldek_test_rpm_flush (&rec);
	pk_poldek_rpm_parser_clear (&parser);
	return g_string_free (rec.events, FALSE);
}

static void
pk_poldek_test_rpm_progress_func (void)
{
	const gchar *expected = "phase 0\n0 - 100\n"
				"phase 1\n1 - 100\n"
				"phase 2\n2 libbar-devel-1.0-1 100\n2 foo-1.1-1 100\n"
				"phase 3\n3 foo-1.0-1 100\n";
	gsize len = strlen (pk_poldek_test_rpm_output);
	g_autoptr(GPtrArray) bytes = g_ptr_array_new_with_free_func (g_free);

	/* poldek passes the output on in arbitrary chunks */
	for (gsize split = 0; split <= len; split++) {
		g_autofree gchar *head = g_strndup (pk_poldek_test_rpm_output, split);
		const gchar *chunks[] = { head, pk_poldek_test_rpm_output + split, NULL };
		g_autofree gchar *events = pk_poldek_test_rpm_parse (chunks);

		g_assert_cmpstr (events, ==, expected);
	}
	for (gsize i = 0; i < len; i++)
		g_ptr_array_add (bytes, g_strndup (pk_poldek_test_rpm_output + i, 1));
	g_ptr_array_add (bytes, NULL);
	{
		g_autofree gchar *events = pk_poldek_test_rpm_parse (
		    (const gchar *const *) bytes->pdata);

		g_assert_cmpstr (events, ==, expected);
	}
}

/* rpm names a bar by NEVR, the epoch whenever the package has the tag, cut to 38 columns */
static void
pk_poldek_test_rpm_label_func (void)
{
	const gchar *long_name = "python3-a-rather-long-package-name-here";

	g_assert_true (pk_poldek_rpm_label_names ("foo-1.0-1", "foo", 0, "1.0", "1"));
	g_assert_true (pk_poldek_rpm_label_names ("mc-1:4.8.33-1", "mc", 1, "4.8.33", "1"));
	g_assert_true (pk_poldek_rpm_label_names ("perl-Scalar-List-Utils-0:1.70-5.44.0.1",
						  "perl-Scalar-List-Utils",
						  0,
						  "1.70",
						  "5.44.0.1"));
	g_assert_true (pk_poldek_rpm_label_names ("python3-a-rather-long-package-name-her",
						  long_name,
						  0,
						  "1.0",
						  "1"));
	g_assert_false (pk_poldek_rpm_label_names ("foo-1.0-2", "foo", 0, "1.0", "1"));
	g_assert_false (pk_poldek_rpm_label_names ("mc-2:4.8.33-1", "mc", 1, "4.8.33", "1"));
	g_assert_false (
	    pk_poldek_rpm_label_names ("python3-a-rather-long", long_name, 0, "1.0", "1"));
}

static void
pk_poldek_test_errors_func (void)
{
	struct
	{
		const gchar *messages;
		PkErrorEnum code;
	} cases[] = {
		/* as poldek and rpm write them */
		{ "broken-1.0-1.noarch: req nonexistent-capability not found\n1 unresolved "
		  "dependency",
		  PK_ERROR_ENUM_DEP_RESOLUTION_FAILED },
		{ "qux-1.0-1.noarch (cnfl foo) conflicts with installed foo-1.0-1.noarch\n1 "
		  "conflicts",
		  PK_ERROR_ENUM_PACKAGE_CONFLICTS },
		{ "file /usr/bin/a conflicts between attempted installs of a-1 and b-1",
		  PK_ERROR_ENUM_FILE_CONFLICTS },
		{ "error: can't create transaction lock on /var/lib/rpm/.rpm.lock (Resource "
		  "temporarily unavailable)",
		  PK_ERROR_ENUM_CANNOT_GET_LOCK },
		{ "package foo-1-1.noarch does not verify: no signature",
		  PK_ERROR_ENUM_BAD_GPG_SIGNATURE },
		{ "installing package big-1.0-1.noarch needs 5MB more space on the / filesystem",
		  PK_ERROR_ENUM_NO_SPACE_ON_DEVICE },
		{ "installing package big-1.0-1.noarch needs 12 more inodes on the / filesystem",
		  PK_ERROR_ENUM_NO_SPACE_ON_DEVICE },
		{ "foo 'bar-1.0' needs an epoch (assuming same epoch as baz)",
		  PK_ERROR_ENUM_TRANSACTION_ERROR },
		{ "vfff: Connection refused", PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED },
		{ "something else", PK_ERROR_ENUM_TRANSACTION_ERROR },
		{ NULL, PK_ERROR_ENUM_TRANSACTION_ERROR },
	};

	for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
		g_assert_cmpstr (pk_error_enum_to_string (pk_poldek_error_from_messages (
				     cases[i].messages,
				     PK_ERROR_ENUM_TRANSACTION_ERROR)),
				 ==,
				 pk_error_enum_to_string (cases[i].code));
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	poldeklib_init ();
	g_test_add_func ("/poldek/groups", pk_poldek_test_groups_func);
	g_test_add_func ("/poldek/pkgid", pk_poldek_test_pkgid_func);
	g_test_add_func ("/poldek/newest", pk_poldek_test_newest_func);
	g_test_add_func ("/poldek/devel", pk_poldek_test_devel_func);
	g_test_add_func ("/poldek/rpm-progress", pk_poldek_test_rpm_progress_func);
	g_test_add_func ("/poldek/rpm-label", pk_poldek_test_rpm_label_func);
	g_test_add_func ("/poldek/errors", pk_poldek_test_errors_func);
	g_test_add_func ("/poldek/line-reader", pk_poldek_test_line_reader_func);
	g_test_add_func ("/poldek/line-reader-limit", pk_poldek_test_line_reader_limit_func);
	g_test_add_func ("/poldek/parse-request", pk_poldek_test_parse_request_func);
	return g_test_run ();
}
