/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
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

//#include "pk-backend-apt.h"
#include <pk-backend.h>
#include <apt-pkg/configuration.h>
#include <sqlite3.h>

typedef enum {FIELD_PKG=1,FIELD_VER,FIELD_DEPS,FIELD_ARCH,FIELD_SHORT,FIELD_LONG,FIELD_REPO} Fields;

void apt_build_db(PkBackend * backend, sqlite3 *db)
{
	GMatchInfo *match_info;
	GError *error = NULL;
	gchar *contents = NULL;
	gchar *sdir;
	const gchar *fname;
	GRegex *origin, *suite;
	GDir *dir;
	GHashTable *releases;
	int res;
	sqlite3_stmt *package = NULL;

	pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates(backend);

	sdir = g_build_filename(_config->Find("Dir").c_str(),_config->Find("Dir::State").c_str(),_config->Find("Dir::State::lists").c_str(), NULL);
	dir = g_dir_open(sdir,0,&error);
	if (error!=NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_INTERNAL_ERROR, "can't open %s",dir);
		g_error_free(error);
		goto search_task_cleanup;
	}

	origin = g_regex_new("^Origin: (\\S+)",(GRegexCompileFlags)(G_REGEX_CASELESS|G_REGEX_OPTIMIZE|G_REGEX_MULTILINE),(GRegexMatchFlags)0,NULL);
	suite = g_regex_new("^Suite: (\\S+)",(GRegexCompileFlags)(G_REGEX_CASELESS|G_REGEX_OPTIMIZE|G_REGEX_MULTILINE),(GRegexMatchFlags)0,NULL);

	releases = g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
	while ((fname = g_dir_read_name(dir))!=NULL)
	{
		gchar *temp, *parsed_name;
		gchar** items = g_strsplit(fname,"_",-1);
		guint len = g_strv_length(items);
		if(len<=3) // minimum is <source>_<type>_<group>
		{
			g_strfreev(items);
			continue;
		}

		/* warning: nasty hack with g_strjoinv */
		temp = items[len-2];
		items[len-2] = NULL;
		parsed_name = g_strjoinv("_",items);
		items[len-2] = temp;

		if (g_ascii_strcasecmp(items[len-1],"Release")==0 && g_ascii_strcasecmp(items[len-2],"source")!=0)
		{
			gchar * repo = NULL, *fullname;
			fullname = g_build_filename(sdir,fname,NULL);
			if (g_file_get_contents(fullname,&contents,NULL,NULL) == FALSE)
			{
				pk_backend_error_code(backend, PK_ERROR_ENUM_INTERNAL_ERROR, "error loading %s",fullname);
				goto search_task_cleanup;
			}
			g_free(fullname);

			g_regex_match (origin, contents, (GRegexMatchFlags)0, &match_info);
			if (!g_match_info_matches(match_info))
			{
				pk_backend_error_code(backend, PK_ERROR_ENUM_INTERNAL_ERROR, "origin regex failure in %s",fname);
				goto search_task_cleanup;
			}
			repo = g_match_info_fetch (match_info, 1);

			g_regex_match (suite, contents, (GRegexMatchFlags)0, &match_info);
			if (g_match_info_matches(match_info))
			{
				temp = g_strconcat(repo,"/",g_match_info_fetch (match_info, 1),NULL);
				g_free(repo);
				repo = temp;
			}

			temp = parsed_name;
			parsed_name = g_strconcat(temp,"_",items[len-2],NULL);
			g_free(temp);

			pk_debug("type is %s, group is %s, parsed_name is %s",items[len-2],items[len-1],parsed_name);

			g_hash_table_insert(releases, parsed_name, repo);
			g_free(contents);
			contents = NULL;
		}
		else
			g_free(parsed_name);
		g_strfreev(items);
	}
	g_dir_close(dir);

	/* and then we need to do this again, but this time we're looking for the packages */
	dir = g_dir_open(sdir,0,&error);
	res = sqlite3_prepare_v2(db, "insert or replace into packages values (?,?,?,?,?,?,?)", -1, &package, NULL);
	if (res!=SQLITE_OK)
		pk_error("sqlite error during insert prepare: %s", sqlite3_errmsg(db));
	else
		pk_debug("insert prepare ok for %p",package);
	while ((fname = g_dir_read_name(dir))!=NULL)
	{
		gchar** items = g_strsplit(fname,"_",-1);
		guint len = g_strv_length(items);
		if(len<=3) // minimum is <source>_<type>_<group>
		{
			g_strfreev(items);
			continue;
		}

		if (g_ascii_strcasecmp(items[len-1],"Packages")==0)
		{
			const gchar *repo;
			gchar *temp=NULL, *parsed_name=NULL;
			gchar *fullname= NULL;
			gchar *begin=NULL, *next=NULL, *description = NULL;
			glong count = 0;
			gboolean haspk = FALSE;

			/* warning: nasty hack with g_strjoinv */
			if (g_str_has_prefix(items[len-2],"binary-"))
			{
				temp = items[len-3];
				items[len-3] = NULL;
				parsed_name = g_strjoinv("_",items);
				items[len-3] = temp;
			}
			else
			{
				temp = items[len-1];
				items[len-1] = NULL;
				parsed_name = g_strjoinv("_",items);
				items[len-1] = temp;
			}

			pk_debug("type is %s, group is %s, parsed_name is %s",items[len-2],items[len-1],parsed_name);

			repo = (const gchar *)g_hash_table_lookup(releases,parsed_name);
			if (repo == NULL)
			{
				pk_debug("Can't find repo for %s, marking as \"unknown\"",parsed_name);
				repo = g_strdup("unknown");
				//g_assert(0);
			}
			else
				pk_debug("repo for %s is %s",parsed_name,repo);
			g_free(parsed_name);

			fullname = g_build_filename(sdir,fname,NULL);
			pk_debug("loading %s",fullname);
			if (g_file_get_contents(fullname,&contents,NULL,NULL) == FALSE)
			{
				pk_backend_error_code(backend, PK_ERROR_ENUM_INTERNAL_ERROR, "error loading %s",fullname);
				goto search_task_cleanup;
			}
			/*else
				pk_debug("loaded");*/

			res = sqlite3_bind_text(package,FIELD_REPO,repo,-1,SQLITE_TRANSIENT);
			if (res!=SQLITE_OK)
				pk_error("sqlite error during repo bind: %s", sqlite3_errmsg(db));
			/*else
				pk_debug("repo bind ok");*/

			res = sqlite3_exec(db,"begin",NULL,NULL,NULL);
			g_assert(res == SQLITE_OK);

			begin = contents;

			while (true)
			{
				next = strstr(begin,"\n");
				if (next!=NULL)
				{
					next[0] = '\0';
					next++;
				}

				if (begin[0]=='\0')
				{
					if (haspk)
					{
						if (description!=NULL)
						{
							res=sqlite3_bind_text(package,FIELD_LONG,description,-1,SQLITE_TRANSIENT);
							if (res!=SQLITE_OK)
								pk_error("sqlite error during description bind: %s", sqlite3_errmsg(db));
							g_free(description);
							description = NULL;
						}
						res = sqlite3_step(package);
						if (res!=SQLITE_DONE)
							pk_error("sqlite error during step: %s", sqlite3_errmsg(db));
						sqlite3_reset(package);
						//pk_debug("added package");
						haspk = FALSE;
					}
					//g_assert(0);
				}
				else if (begin[0]==' ')
				{
					if (description == NULL)
						description = g_strdup(&begin[1]);
					else
					{
						gchar *oldval = description;
						description = g_strconcat(oldval, "\n",&begin[1],NULL);
						g_free(oldval);
					}
				}
				else
				{
					gchar *colon = strchr(begin,':');
					g_assert(colon!=NULL);
					colon[0] = '\0';
					colon+=2;
					/*if (strlen(colon)>3000)
						pk_error("strlen(colon) = %d\ncolon = %s",strlen(colon),colon);*/
					//pk_debug("entry = '%s','%s'",begin,colon);
					if (begin[0] == 'P' && g_strcasecmp("Package",begin)==0)
					{
						res=sqlite3_bind_text(package,FIELD_PKG,colon,-1,SQLITE_STATIC);
						haspk = TRUE;
						count++;
						if (count%1000==0)
							pk_debug("Package %ld (%s)",count,colon);
					}
					else if (begin[0] == 'V' && g_strcasecmp("Version",begin)==0)
						res=sqlite3_bind_text(package,FIELD_VER,colon,-1,SQLITE_STATIC);
					else if (begin[0] == 'D' && g_strcasecmp("Depends",begin)==0)
						res=sqlite3_bind_text(package,FIELD_DEPS,colon,-1,SQLITE_STATIC);
					else if (begin[0] == 'A' && g_strcasecmp("Architecture",begin)==0)
						res=sqlite3_bind_text(package,FIELD_ARCH,colon,-1,SQLITE_STATIC);
					else if (begin[0] == 'D' && g_strcasecmp("Description",begin)==0)
						res=sqlite3_bind_text(package,FIELD_SHORT,colon,-1,SQLITE_STATIC);
					if (res!=SQLITE_OK)
						pk_error("sqlite error during %s bind: %s", begin, sqlite3_errmsg(db));
				}
				if (next == NULL)
					break;
				begin = next;
			}
			res = sqlite3_exec(db,"commit",NULL,NULL,NULL);
			if (res!=SQLITE_OK)
				pk_error("sqlite error during commit: %s", sqlite3_errmsg(db));
			res = sqlite3_clear_bindings(package);
			if (res!=SQLITE_OK)
				pk_error("sqlite error during clear: %s", sqlite3_errmsg(db));
			g_free(contents);
			contents = NULL;
		}
	}
	sqlite3_finalize(package);

search_task_cleanup:
	g_dir_close(dir);
	g_free(sdir);
	g_free(contents);
}

