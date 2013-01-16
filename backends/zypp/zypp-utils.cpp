/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2007 Novell, Inc.
 * Copyright (c) 2007 Boyd Timothy <btimothy@gmail.com>
 * Copyright (c) 2007-2008 Stefan Haas <shaas@suse.de>
 * Copyright (c) 2007-2008 Scott Reeves <sreeves@novell.com>
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

#include <sstream>
#include <stdlib.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <zypp/ZYpp.h>
#include <zypp/ZYppFactory.h>
#include <zypp/RepoManager.h>
#include <zypp/media/MediaManager.h>
#include <zypp/Resolvable.h>
#include <zypp/ResPool.h>
#include <zypp/Repository.h>
#include <zypp/RepoManager.h>
#include <zypp/RepoInfo.h>
#include <zypp/repo/RepoException.h>
#include <zypp/target/rpm/RpmException.h>
#include <zypp/parser/ParseException.h>
#include <zypp/base/Algorithm.h>
#include <zypp/Pathname.h>
#include <zypp/Patch.h>
#include <zypp/Package.h>
#include <zypp/ui/Selectable.h>
#include <zypp/sat/Pool.h>
#include <zypp/target/rpm/RpmDb.h>
#include <zypp/target/rpm/RpmHeader.h>
#include <zypp/target/rpm/librpmDb.h>
#include <zypp/base/LogControl.h>
#include <zypp/base/String.h>
#include <zypp/parser/IniDict.h>
#include <zypp/PathInfo.h>

#include <zypp/base/Logger.h>

#include <pk-backend.h>
#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

#include "pk-backend-zypp-private.h"
#include "zypp-utils.h"

gchar * _repoName;
gboolean _updating_self = FALSE;

using namespace std;
using namespace zypp;
using zypp::filesystem::PathInfo;

extern PkBackendZYppPrivate *priv;

ZyppJob::ZyppJob(PkBackendJob *job) 
{
	if (priv->currentJob) {
		g_error("currentjob is already defined");
	}

	priv->currentJob = job;
	priv->eventDirector.setJob(job);
}

ZyppJob::~ZyppJob()
{
	priv->currentJob = 0;
	priv->eventDirector.setJob(0);
}

/**
 * Initialize Zypp (Factory method)
 */
ZYpp::Ptr
ZyppJob::get_zypp()
{
	static gboolean initialized = FALSE;
	ZYpp::Ptr zypp = NULL;

	try {
		zypp = ZYppFactory::instance ().getZYpp ();

		/* TODO: we need to lifecycle manage this, detect changes
		   in the requested 'root' etc. */
		if (!initialized) {
			filesystem::Pathname pathname("/");
			zypp->initializeTarget (pathname);

			initialized = TRUE;
		}
	} catch (const ZYppFactoryException &ex) {
		pk_backend_job_error_code (priv->currentJob, PK_ERROR_ENUM_FAILED_INITIALIZATION, ex.asUserString().c_str() );
		return NULL;
	} catch (const Exception &ex) {
		pk_backend_job_error_code (priv->currentJob, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str() );
		return NULL;
	}

	return zypp;
}

/**
  * Enable and rotate zypp logging
  */
gboolean
zypp_logging ()
{
	gchar *file = g_strdup ("/var/log/pk_backend_zypp");
	gchar *file_old = g_strdup ("/var/log/pk_backend_zypp-1");

	if (g_file_test (file, G_FILE_TEST_EXISTS)) {
		struct stat buffer;
		g_stat (file, &buffer);
		// if the file is bigger than 10 MB rotate
		if ((guint)buffer.st_size > 10485760) {
			if (g_file_test (file_old, G_FILE_TEST_EXISTS))
				g_remove (file_old);
			g_rename (file, file_old);
		}
	}

	base::LogControl::instance ().logfile(file);

	g_free (file);
	g_free (file_old);

	return TRUE;
}

gboolean
zypp_is_changeable_media (PkBackend *backend, const Url &url)
{
	gboolean is_cd = false;
	try {
		media::MediaManager mm;
		media::MediaAccessId id = mm.open (url);
		is_cd = mm.isChangeable (id);
		mm.close (id);
	} catch (const media::MediaException &e) {
		// TODO: Do anything about this?
	}

	return is_cd;
}

namespace {
	/// Helper finding pattern at end or embedded in name.
	/// E.g '-debug' in 'repo-debug' or 'repo-debug-update'
	inline bool
	name_ends_or_contains( const std::string & name_r, const std::string & pattern_r, const char sepchar_r = '-' )
	{
		if ( ! pattern_r.empty() )
		{
			for ( std::string::size_type pos = name_r.find( pattern_r );
			      pos != std::string::npos;
			      pos = name_r.find( pattern_r, pos + pattern_r.size() ) )
			{
				if ( pos + pattern_r.size() == name_r.size()		// at end
				  || name_r[pos + pattern_r.size()] == sepchar_r )	// embedded
					return true;
			}
		}
		return false;
	}
}

gboolean
zypp_is_development_repo (PkBackend *backend, RepoInfo repo)
{
	return ( name_ends_or_contains( repo.alias(), "-debuginfo" )
	      || name_ends_or_contains( repo.alias(), "-debug" )
	      || name_ends_or_contains( repo.alias(), "-source" )
	      || name_ends_or_contains( repo.alias(), "-development" ) );
}

gboolean
zypp_is_valid_repo (PkBackendJob *job, RepoInfo repo)
{

	if (repo.alias().empty()){
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR, "Repository has no or invalid repo name defined.\n", repo.alias ().c_str ());
		return FALSE;
	}

	if (!repo.url().isValid()){
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR, "%s: Repository has no or invalid url defined.\n", repo.alias ().c_str ());
		return FALSE;
	}

	return TRUE;
}

ResPool
zypp_build_pool (ZYpp::Ptr zypp, gboolean include_local)
{
	static gboolean repos_loaded = FALSE;

	// the target is loaded or unloaded on request
	if (include_local) {
		// FIXME have to wait for fix in zypp (repeated loading of target)
		if (sat::Pool::instance().reposFind( sat::Pool::systemRepoAlias() ).solvablesEmpty ())
		{
			// Add local resolvables
			Target_Ptr target = zypp->target ();
			target->load ();
		}
	} else {
		if (!sat::Pool::instance().reposFind( sat::Pool::systemRepoAlias() ).solvablesEmpty ())
		{
			// Remove local resolvables
			Repository repository = sat::Pool::instance ().reposFind (sat::Pool::systemRepoAlias());
			repository.eraseFromPool ();
		}
	}

	// we only load repositories once.
	if (repos_loaded)
		return zypp->pool();

	// Add resolvables from enabled repos
	RepoManager manager;
	try {
		for (RepoManager::RepoConstIterator it = manager.repoBegin(); it != manager.repoEnd(); ++it) {
			RepoInfo repo (*it);

			// skip disabled repos
			if (repo.enabled () == false)
				continue;
			// skip not cached repos
			if (manager.isCached (repo) == false) {
				g_warning ("%s is not cached! Do a refresh", repo.alias ().c_str ());
				continue;
			}
			//FIXME see above, skip already cached repos
			if (sat::Pool::instance().reposFind( repo.alias ()) == Repository::noRepository)
				manager.loadFromCache (repo);

		}
		repos_loaded = true;
	} catch (const repo::RepoNoAliasException &ex) {
		g_error ("Can't figure an alias to look in cache");
	} catch (const repo::RepoNotCachedException &ex) {
		g_error ("The repo has to be cached at first: %s", ex.asUserString ().c_str ());
	} catch (const Exception &ex) {
		g_error ("TODO: Handle exceptions: %s", ex.asUserString ().c_str ());
	}

	return zypp->pool ();
}

void
warn_outdated_repos(PkBackendJob *job, const ResPool & pool)
{
	Repository repoobj;
	ResPool::repository_iterator it;
	for ( it = pool.knownRepositoriesBegin();
		it != pool.knownRepositoriesEnd();
		++it )
	{
		Repository repo(*it);
		if ( repo.maybeOutdated() )
		{
			// warn the user
			pk_backend_job_message (job,
					PK_MESSAGE_ENUM_BROKEN_MIRROR,
					str::form("The repository %s seems to be outdated. You may want to try another mirror.",
					repo.alias().c_str()).c_str() );
		}
	}
}

target::rpm::RpmHeader::constPtr
zypp_get_rpmHeader (const string &name, Edition edition)
{
	target::rpm::librpmDb::db_const_iterator it;
	target::rpm::RpmHeader::constPtr result = new target::rpm::RpmHeader ();

	for (it.findPackage (name, edition); *it; ++it) {
		result = *it;
	}

	return result;
}

PkGroupEnum
get_enum_group (const string &group_)
{
	string group(str::toLower(group_));

	if (group.find ("amusements") != string::npos) {
		return PK_GROUP_ENUM_GAMES;
	} else if (group.find ("development") != string::npos) {
		return PK_GROUP_ENUM_PROGRAMMING;
	} else if (group.find ("hardware") != string::npos) {
		return PK_GROUP_ENUM_SYSTEM;
	} else if (group.find ("archiving") != string::npos
		  || group.find("clustering") != string::npos
		  || group.find("system/monitoring") != string::npos
		  || group.find("databases") != string::npos
		  || group.find("system/management") != string::npos) {
		return PK_GROUP_ENUM_ADMIN_TOOLS;
	} else if (group.find ("graphics") != string::npos) {
		return PK_GROUP_ENUM_GRAPHICS;
	} else if (group.find ("multimedia") != string::npos) {
		return PK_GROUP_ENUM_MULTIMEDIA;
	} else if (group.find ("network") != string::npos) {
		return PK_GROUP_ENUM_NETWORK;
	} else if (group.find ("office") != string::npos
		  || group.find("text") != string::npos
		  || group.find("editors") != string::npos) {
		return PK_GROUP_ENUM_OFFICE;
	} else if (group.find ("publishing") != string::npos) {
		return PK_GROUP_ENUM_PUBLISHING;
	} else if (group.find ("security") != string::npos) {
		return PK_GROUP_ENUM_SECURITY;
	} else if (group.find ("telephony") != string::npos) {
		return PK_GROUP_ENUM_COMMUNICATION;
	} else if (group.find ("gnome") != string::npos) {
		return PK_GROUP_ENUM_DESKTOP_GNOME;
	} else if (group.find ("kde") != string::npos) {
		return PK_GROUP_ENUM_DESKTOP_KDE;
	} else if (group.find ("xfce") != string::npos) {
		return PK_GROUP_ENUM_DESKTOP_XFCE;
	} else if (group.find ("gui/other") != string::npos) {
		return PK_GROUP_ENUM_DESKTOP_OTHER;
	} else if (group.find ("localization") != string::npos) {
		return PK_GROUP_ENUM_LOCALIZATION;
	} else if (group.find ("system") != string::npos) {
		return PK_GROUP_ENUM_SYSTEM;
	} else if (group.find ("scientific") != string::npos) {
		return PK_GROUP_ENUM_EDUCATION;
	}

	return PK_GROUP_ENUM_UNKNOWN;
}

void
zypp_get_packages_by_name (PkBackend *backend,
			   const gchar *package_name,
			   const ResKind kind,
			   vector<sat::Solvable> &result,
			   gboolean include_local)
{
	ui::Selectable::Ptr sel( ui::Selectable::get( kind, package_name ) );
	if ( sel ) {
		if ( ! sel->installedEmpty() ) {
			for_( it, sel->installedBegin(), sel->installedEnd() )
				result.push_back( (*it).satSolvable() );
		}
		if ( ! sel->availableEmpty() ) {
			for_( it, sel->availableBegin(), sel->availableEnd() )
				result.push_back( (*it).satSolvable() );
		}
	}
}

void
zypp_get_packages_by_file (ZYpp::Ptr zypp,
			   const gchar *search_file,
			   vector<sat::Solvable> &ret)
{
	ResPool pool = zypp_build_pool (zypp, TRUE);

	string file (search_file);

	target::rpm::librpmDb::db_const_iterator it;
	target::rpm::RpmHeader::constPtr result = new target::rpm::RpmHeader ();

	for (it.findByFile (search_file); *it; ++it) {
		for (ResPool::byName_iterator it2 = pool.byNameBegin (it->tag_name ()); it2 != pool.byNameEnd (it->tag_name ()); it2++) {
			if ((*it2)->isSystem ())
				ret.push_back ((*it2)->satSolvable ());
		}
	}

	if (ret.empty ()) {
		Capability cap (search_file);
		sat::WhatProvides prov (cap);

		for(sat::WhatProvides::const_iterator it = prov.begin (); it != prov.end (); ++it) {
			ret.push_back (*it);
		}
	}
}

sat::Solvable
zypp_get_package_by_id (PkBackend *backend, const gchar *package_id)
{
	if (!pk_package_id_check(package_id)) {
		// TODO: Do we need to do something more for this error?
		return sat::Solvable::noSolvable;
	}

	gchar **id_parts = pk_package_id_split(package_id);
	vector<sat::Solvable> v;
	vector<sat::Solvable> v2;

	zypp_get_packages_by_name (backend, id_parts[PK_PACKAGE_ID_NAME], ResKind::package, v);
	zypp_get_packages_by_name (backend, id_parts[PK_PACKAGE_ID_NAME], ResKind::patch, v2);

	v.insert (v.end (), v2.begin (), v2.end ());

	if (v.empty())
		return sat::Solvable::noSolvable;

	sat::Solvable package;

	for (vector<sat::Solvable>::iterator it = v.begin ();
			it != v.end (); ++it) {
		if (zypp_ver_and_arch_equal (*it, id_parts[PK_PACKAGE_ID_VERSION],
					     id_parts[PK_PACKAGE_ID_ARCH])) {
			package = *it;
			break;
		}
	}

	g_strfreev (id_parts);
	return package;
}

RepoInfo
zypp_get_Repository (PkBackendJob *job, const gchar *alias)
{
	RepoInfo info;

	try {
		RepoManager manager;
		info = manager.getRepositoryInfo (alias);
	} catch (const repo::RepoNotFoundException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
		return RepoInfo ();
	}

	return info;
}

/*
 * PK requires a transaction (backend method call) to abort immediately
 * after an error is set. Unfortunately, zypp's 'refresh' methods call
 * these signature methods on errors - and provide no way to signal an
 * abort - instead the refresh continuing on with the next repository.
 * PK (pk_backend_error_timeout_delay_cb) uses this as an excuse to
 * abort the (still running) transaction, and to start another - which
 * leads to multi-threaded use of zypp and hence sudden, random death.
 *
 * To cure this, we throw this custom exception across zypp and catch
 * it outside (hopefully) the only entry point (zypp_refresh_meta_and_cache)
 * that can cause these (zypp_signature_required) methods to be called.
 *
 */
class AbortTransactionException {
 public:
	AbortTransactionException() {}
};

gboolean
zypp_refresh_meta_and_cache (RepoManager &manager, RepoInfo &repo, bool force)
{
	try {
		if (manager.checkIfToRefreshMetadata (repo, repo.url(),
					RepoManager::RefreshIfNeededIgnoreDelay)
					!= RepoManager::REFRESH_NEEDED)
			return TRUE;

		sat::Pool pool = sat::Pool::instance ();
		// Erase old solv file
		pool.reposErase (repo.alias ());
		manager.refreshMetadata (repo, force ?
					 RepoManager::RefreshForced :
					 RepoManager::RefreshIfNeededIgnoreDelay);
		manager.buildCache (repo, force ?
				    RepoManager::BuildForced :
				    RepoManager::BuildIfNeeded);
		manager.loadFromCache (repo);
		return TRUE;
	} catch (const AbortTransactionException &ex) {
		return FALSE;
	}
}

gboolean
zypp_signature_required (PkBackendJob *job, const PublicKey &key)
{
	gboolean ok = FALSE;

	if (find (priv->signatures.begin (), priv->signatures.end (), key.id ()) == priv->signatures.end ()) {
		RepoInfo info = zypp_get_Repository (job, _repoName);
		if (info.type () == repo::RepoType::NONE)
			pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR,
					       "Repository unknown");
		else {
			pk_backend_job_repo_signature_required (job,
				"dummy;0.0.1;i386;data",
				_repoName,
				info.baseUrlsBegin ()->asString ().c_str (),
				key.name ().c_str (),
				key.id ().c_str (),
				key.fingerprint ().c_str (),
				key.created ().asString ().c_str (),
				PK_SIGTYPE_ENUM_GPG);
			pk_backend_job_error_code (job, PK_ERROR_ENUM_GPG_FAILURE,
					       "Signature verification for Repository %s failed", _repoName);
		}
		throw AbortTransactionException();
	} else
		ok = TRUE;

	return ok;
}

gboolean
zypp_signature_required (PkBackendJob *job, const string &file, const string &id)
{
	gboolean ok = FALSE;

	if (find (priv->signatures.begin (), priv->signatures.end (), id) == priv->signatures.end ()) {
		RepoInfo info = zypp_get_Repository (job, _repoName);
		if (info.type () == repo::RepoType::NONE)
			pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR,
					       "Repository unknown");
		else {
			pk_backend_job_repo_signature_required (job,
				"dummy;0.0.1;i386;data",
				_repoName,
				info.baseUrlsBegin ()->asString ().c_str (),
				id.c_str (),
				id.c_str (),
				"UNKNOWN",
				"UNKNOWN",
				PK_SIGTYPE_ENUM_GPG);
			pk_backend_job_error_code (job, PK_ERROR_ENUM_GPG_FAILURE,
					       "Signature verification for Repository %s failed", _repoName);
		}
		throw AbortTransactionException();
	} else
		ok = TRUE;

	return ok;
}

gboolean
zypp_signature_required (PkBackendJob *job, const string &file)
{
	gboolean ok = FALSE;

	if (find (priv->signatures.begin (), priv->signatures.end (), file) == priv->signatures.end ()) {
		RepoInfo info = zypp_get_Repository (job, _repoName);
		if (info.type () == repo::RepoType::NONE)
			pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR,
					       "Repository unknown");
		else {
			pk_backend_job_repo_signature_required (job,
				"dummy;0.0.1;i386;data",
				_repoName,
				info.baseUrlsBegin ()->asString ().c_str (),
				"UNKNOWN",
				file.c_str (),
				"UNKNOWN",
				"UNKNOWN",
				PK_SIGTYPE_ENUM_GPG);
			pk_backend_job_error_code (job, PK_ERROR_ENUM_GPG_FAILURE,
					       "Signature verification for Repository %s failed", _repoName);
		}
		throw AbortTransactionException();
	} else
		ok = TRUE;

	return ok;
}

gboolean
system_and_package_are_x86 (sat::Solvable item)
{
	// i586, i686, ... all should be considered the same arch for our comparison
	return ( item.arch() == Arch_i586 && ZConfig::defaultSystemArchitecture() == Arch_i686 );
}

static gboolean
zypp_package_is_devel (const sat::Solvable &item)
{
	const string &name = item.name();
	const char *cstr = name.c_str();

	return ( g_str_has_suffix (cstr, "-debuginfo") ||
		 g_str_has_suffix (cstr, "-debugsource") ||
		 g_str_has_suffix (cstr, "-devel") );
}

/* should we filter out this package ? */
gboolean
zypp_filter_solvable (PkBitfield filters, const sat::Solvable &item)
{
	// iterate through the given filters
	if (!filters)
		return FALSE;

	for (guint i = 0; i < PK_FILTER_ENUM_LAST; i++) {
		if ((filters & pk_bitfield_value (i)) == 0)
			continue;
		if (i == PK_FILTER_ENUM_INSTALLED && !(item.isSystem ()))
			return TRUE;
		if (i == PK_FILTER_ENUM_NOT_INSTALLED && item.isSystem ())
			return TRUE;
		if (i == PK_FILTER_ENUM_ARCH) {
			if (item.arch () != ZConfig::defaultSystemArchitecture () &&
			    item.arch () != Arch_noarch &&
			    ! system_and_package_are_x86 (item))
				return TRUE;
		}
		if (i == PK_FILTER_ENUM_NOT_ARCH) {
			if (item.arch () == ZConfig::defaultSystemArchitecture () ||
			    system_and_package_are_x86 (item))
				return TRUE;
		}
		if (i == PK_FILTER_ENUM_SOURCE && !(isKind<SrcPackage>(item)))
			return TRUE;
		if (i == PK_FILTER_ENUM_NOT_SOURCE && isKind<SrcPackage>(item))
			return TRUE;
		if (i == PK_FILTER_ENUM_DEVELOPMENT && !zypp_package_is_devel (item))
			return TRUE;
		if (i == PK_FILTER_ENUM_NOT_DEVELOPMENT && zypp_package_is_devel (item))
			return TRUE;

		// FIXME: add more enums - cf. libzif logic and pk-enum.h
		// PK_FILTER_ENUM_SUPPORTED,
		// PK_FILTER_ENUM_NOT_SUPPORTED,
	}

	return FALSE;
}

/*
 * Emit signals for the packages, -but- if we have an installed package
 * we don't notify the client that the package is also available, since
 * PK doesn't handle re-installs (by some quirk).
 */
void
zypp_emit_filtered_packages_in_list (PkBackendJob *job, PkBitfield filters, const vector<sat::Solvable> &v)
{
	typedef vector<sat::Solvable>::const_iterator sat_it_t;

	vector<sat::Solvable> installed;

	// always emit system installed packages first
	for (sat_it_t it = v.begin (); it != v.end (); ++it) {
		if (!it->isSystem() ||
		    zypp_filter_solvable (filters, *it))
			continue;

		zypp_backend_package (job, PK_INFO_ENUM_INSTALLED, *it,
				      make<ResObject>(*it)->summary().c_str());
		installed.push_back (*it);
	}

	// then available packages later
	for (sat_it_t it = v.begin (); it != v.end (); ++it) {
		gboolean match;

		if (it->isSystem() ||
		    zypp_filter_solvable (filters, *it))
			continue;

		match = FALSE;
		for (sat_it_t i = installed.begin (); !match && i != installed.end (); i++) {
			match = it->sameNVRA (*i) &&
				!(!isKind<SrcPackage>(*it) ^
				  !isKind<SrcPackage>(*i));
		}
		if (!match) {
			zypp_backend_package (job, PK_INFO_ENUM_AVAILABLE, *it,
					      make<ResObject>(*it)->summary().c_str());
		}
	}
}

void
zypp_backend_package (PkBackendJob *job, PkInfoEnum info,
		      const sat::Solvable &pkg,
		      const char *opt_summary)
{
	gchar *id = zypp_build_package_id_from_resolvable (pkg);
	pk_backend_job_package (job, info, id, opt_summary);
	g_free (id);
}

/**
 * Returns a set of all packages the could be updated
 * (you're able to exclude a single (normally the 'patch' repo)
 */
static void
zypp_get_package_updates (string repo, set<PoolItem> &pks)
{
	ResPool pool = ResPool::instance ();

	ResObject::Kind kind = ResTraits<Package>::kind;
	ResPool::byKind_iterator it = pool.byKindBegin (kind);
	ResPool::byKind_iterator e = pool.byKindEnd (kind);

	getZYpp()->resolver()->doUpdate();
	for (; it != e; ++it)
		if (it->status().isToBeInstalled()) {
			ui::Selectable::constPtr s =
				ui::Selectable::get((*it)->kind(), (*it)->name());
			if (s->hasInstalledObj())
				pks.insert(*it);
		}
}

/**
 * Returns a set of all patches the could be installed
 */
static void
zypp_get_patches (PkBackendJob *job, ZYpp::Ptr zypp, set<PoolItem> &patches)
{
	_updating_self = FALSE;
	
	zypp->resolver ()->setIgnoreAlreadyRecommended (TRUE);
	zypp->resolver ()->resolvePool ();

	for (ResPoolProxy::const_iterator it = zypp->poolProxy ().byKindBegin<Patch>();
			it != zypp->poolProxy ().byKindEnd<Patch>(); it ++) {
		// check if the patch is needed and not set to taboo
		if((*it)->isNeeded() && !((*it)->candidateObj ().isUnwanted())) {
			Patch::constPtr patch = asKind<Patch>((*it)->candidateObj ().resolvable ());
			if (_updating_self) {
				if (patch->restartSuggested ())
					patches.insert ((*it)->candidateObj ());
			}
			else
				patches.insert ((*it)->candidateObj ());

			// check if the patch updates libzypp or packageKit and show only these
			if (!_updating_self && patch->restartSuggested ()) {
				_updating_self = TRUE;
				patches.clear ();
				patches.insert ((*it)->candidateObj ());
			}
		}

	}
}

void
zypp_get_updates (PkBackendJob *job, ZYpp::Ptr zypp, set<PoolItem> &candidates)
{
	typedef set<PoolItem>::iterator pi_it_t;
	zypp_get_patches (job, zypp, candidates);

	if (!_updating_self) {
		// exclude the patch-repository
		string patchRepo;
		if (!candidates.empty ()) {
			patchRepo = candidates.begin ()->resolvable ()->repoInfo ().alias ();
		}

		bool hidePackages = false;
		if (PathInfo("/etc/PackageKit/ZYpp.conf").isExist()) {
			parser::IniDict vendorConf(InputStream("/etc/PackageKit/ZYpp.conf"));
			if (vendorConf.hasSection("Updates")) {
				for ( parser::IniDict::entry_const_iterator eit = vendorConf.entriesBegin("Updates");
				      eit != vendorConf.entriesEnd("Updates");
				      ++eit )
				{
					if ((*eit).first == "HidePackages" &&
					    str::strToTrue((*eit).second))
						hidePackages = true;
				}
			}
		}

		if (!hidePackages)
		{
			set<PoolItem> packages;
			zypp_get_package_updates(patchRepo, packages);

			pi_it_t cb = candidates.begin (), ce = candidates.end (), ci;
			for (ci = cb; ci != ce; ++ci) {
				if (!isKind<Patch>(ci->resolvable()))
				continue;

				Patch::constPtr patch = asKind<Patch>(ci->resolvable());

				// Remove contained packages from list of packages to add
				sat::SolvableSet::const_iterator pki;
				Patch::Contents content(patch->contents());
				for (pki = content.begin(); pki != content.end(); ++pki) {

					pi_it_t pb = packages.begin (), pe = packages.end (), pi;
					for (pi = pb; pi != pe; ++pi) {
						if (pi->satSolvable() == sat::Solvable::noSolvable)
							continue;

						if (pi->satSolvable().identical (*pki)) {
							packages.erase (pi);
							break;
						}
					}
				}
			}

			// merge into the list
			candidates.insert (packages.begin (), packages.end ());
		}
	}
}

void
zypp_check_restart (PkRestartEnum *restart, Patch::constPtr patch)
{
	if (patch == NULL || restart == NULL)
		return;

	// set the restart flag if a restart is needed
	if (*restart != PK_RESTART_ENUM_SYSTEM &&
	    ( patch->reloginSuggested () ||
	      patch->restartSuggested () ||
	      patch->rebootSuggested ()) ) {
		if (patch->reloginSuggested () || patch->restartSuggested ())
			*restart = PK_RESTART_ENUM_SESSION;
		if (patch->rebootSuggested ())
			*restart = PK_RESTART_ENUM_SYSTEM;
	}
}

gboolean
zypp_perform_execution (PkBackendJob *job, ZYpp::Ptr zypp, PerformType type, gboolean force, PkBitfield transaction_flags)
{
	gboolean ret = FALSE;
	
	PkBackend *backend = PK_BACKEND(pk_backend_job_get_backend(job));
	
	try {
		if (force)
			zypp->resolver ()->setForceResolve (force);

		// Gather up any dependencies
		pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);
		zypp->resolver ()->setIgnoreAlreadyRecommended (TRUE);
		if (!zypp->resolver ()->resolvePool ()) {
			// Manual intervention required to resolve dependencies
			// TODO: Figure out what we need to do with PackageKit
			// to pull off interactive problem solving.

			ResolverProblemList problems = zypp->resolver ()->problems ();
			gchar * emsg = NULL, * tempmsg = NULL;

			for (ResolverProblemList::iterator it = problems.begin (); it != problems.end (); ++it) {
				if (emsg == NULL) {
					emsg = g_strdup ((*it)->description ().c_str ());
				}
				else {
					tempmsg = emsg;
					emsg = g_strconcat (emsg, "\n", (*it)->description ().c_str (), NULL);
					g_free (tempmsg);
				}
			}

			// reset the status of all touched PoolItems
			ResPool pool = ResPool::instance ();
			for (ResPool::const_iterator it = pool.begin (); it != pool.end (); ++it) {
				if (it->status ().isToBeInstalled ())
					it->statusReset ();
			}

			pk_backend_job_error_code (job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, emsg);
			g_free (emsg);

			goto exit;
		}

		switch (type) {
			case INSTALL:
				pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);
				break;
			case REMOVE:
				pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);
				break;
			case UPDATE:
				pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);
				break;
		};

		ResPool pool = ResPool::instance ();
		if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
			ret = TRUE;

			g_debug ("simulating");

			for (ResPool::const_iterator it = pool.begin (); it != pool.end (); ++it) {
				if (type == REMOVE && !(*it)->isSystem ()) {
					it->statusReset ();
					continue;
				}
				if (!zypp_backend_pool_item_notify (job, *it, TRUE))
					ret = FALSE;
				it->statusReset ();
			}
			goto exit;
		}


		// look for licenses to confirm

		for (ResPool::const_iterator it = pool.begin (); it != pool.end (); ++it) {
			if (it->status ().isToBeInstalled () && !(it->resolvable()->licenseToConfirm().empty ())) {
				gchar *eula_id = g_strdup ((*it)->name ().c_str ());
				gboolean has_eula = pk_backend_is_eula_valid (backend, eula_id);
				if (!has_eula) {
					gchar *package_id = zypp_build_package_id_from_resolvable (it->satSolvable ());
					pk_backend_job_eula_required (job,
							eula_id,
							package_id,
							(*it)->vendor ().c_str (),
							it->resolvable()->licenseToConfirm().c_str ());
					pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_LICENSE_AGREEMENT, "You've to agree/decline a license");
					g_free (package_id);
					g_free (eula_id);
					goto exit;
				}
				g_free (eula_id);
			}
		}

		// Perform the installation
		gboolean only_download = pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD);

		ZYppCommitPolicy policy;
		policy.restrictToMedia (0); // 0 == install all packages regardless to media
		if (only_download)
			policy.downloadMode(DownloadOnly);
		else
			policy.downloadMode (DownloadInHeaps);
		
		policy.syncPoolAfterCommit (true);
		if (!pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED))
			policy.rpmNoSignature(true);

		ZYppCommitResult result = zypp->commit (policy);

		bool worked = result.allDone();
		if (only_download)
			worked = result.noError();

		if ( ! worked )
		{
			std::ostringstream todolist;
			char separator = '\0';

			// process all steps not DONE (ERROR and TODO)
			const sat::Transaction & trans( result.transaction() );
			for_( it, trans.actionBegin(~sat::Transaction::STEP_DONE), trans.actionEnd() )
			{
				if ( separator )
					todolist << separator << it->ident();
				else
				{
					todolist << it->ident();
					separator = '\n';
				}
			}

			pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_ERROR,
					"Transaction could not be completed.\n Theses packages could not be installed: %s",
					todolist.str().c_str());

			goto exit;
		}

		ret = TRUE;
	} catch (const repo::RepoNotFoundException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
	} catch (const target::rpm::RpmException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED, ex.asUserString().c_str () );
	} catch (const Exception &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str() );
	}

 exit:
	/* reset the various options */
	try {
		zypp->resolver ()->setForceResolve (FALSE);
	} catch (const Exception &ex) { /* we tried */ }

	return ret;
}

GPtrArray *
zypp_build_package_id_capabilities (Capabilities caps, gboolean terminate)
{
	GPtrArray *package_ids = g_ptr_array_new();

	sat::WhatProvides provs (caps);

	for (sat::WhatProvides::const_iterator it = provs.begin (); it != provs.end (); ++it) {
		gchar *package_id = zypp_build_package_id_from_resolvable (*it);
		g_ptr_array_add(package_ids, package_id);
	}
	if (terminate)
		g_ptr_array_add(package_ids, NULL);
	return package_ids;
}

gboolean
zypp_refresh_cache (PkBackendJob *job, ZYpp::Ptr zypp, gboolean force)
{
	PkBackend *backend = PK_BACKEND(pk_backend_job_get_backend(job));
	// This call is needed as it calls initializeTarget which appears to properly setup the keyring

	if (zypp == NULL)
		return  FALSE;
	filesystem::Pathname pathname("/");
	// This call is needed to refresh system rpmdb status while refresh cache
	zypp->finishTarget ();
	zypp->initializeTarget (pathname);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_job_set_percentage (job, 0);

	RepoManager manager;
	list <RepoInfo> repos;
	try
	{
		repos = list<RepoInfo>(manager.repoBegin(),manager.repoEnd());
	}
	catch ( const Exception &e)
	{
		// FIXME: make sure this dumps out the right sring.
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, e.asUserString().c_str() );
		return FALSE;
	}

	int i = 1;
	int num_of_repos = repos.size ();
	gchar *repo_messages = NULL;

	for (list <RepoInfo>::iterator it = repos.begin(); it != repos.end(); ++it, i++) {
		RepoInfo repo (*it);

		if (!zypp_is_valid_repo (job, repo))
			return FALSE;
		if (pk_backend_job_get_is_error_set (job))
			break;

		// skip disabled repos
		if (repo.enabled () == false)
			continue;

		// skip changeable meda (DVDs and CDs).  Without doing this,
		// the disc would be required to be physically present.
		if (zypp_is_changeable_media (backend, *repo.baseUrlsBegin ()) == true)
			continue;

		try {
			// Refreshing metadata
			g_free (_repoName);
			_repoName = g_strdup (repo.alias ().c_str ());
			zypp_refresh_meta_and_cache (manager, repo, force);
		} catch (const Exception &ex) {
			if (repo_messages == NULL) {
				repo_messages = g_strdup_printf ("%s: %s%s", repo.alias ().c_str (), ex.asUserString ().c_str (), "\n");
			} else {
				repo_messages = g_strdup_printf ("%s%s: %s%s", repo_messages, repo.alias ().c_str (), ex.asUserString ().c_str (), "\n");
			}
			if (repo_messages == NULL || !g_utf8_validate (repo_messages, -1, NULL))
				repo_messages = g_strdup ("A repository could not be refreshed");
			g_strdelimit (repo_messages, "\\\f\r\t", ' ');
			continue;
		}

		// Update the percentage completed
		pk_backend_job_set_percentage (job, i >= num_of_repos ? 100 : (100 * i) / num_of_repos);
	}
	if (repo_messages != NULL)
		pk_backend_job_message (job, PK_MESSAGE_ENUM_CONNECTION_REFUSED, repo_messages);

	g_free (repo_messages);
	return TRUE;
}

void
zypp_backend_finished_error (PkBackendJob  *job, PkErrorEnum err_code,
			     const char *format, ...)
{
	va_list args;
	gchar *buffer;

	/* sadly no _va variant for error code setting */
	va_start (args, format);
	buffer = g_strdup_vprintf (format, args);
	va_end (args);

	pk_backend_job_error_code (job, err_code, "%s", buffer);

	g_free (buffer);

	pk_backend_job_finished (job);
}

gboolean
zypp_backend_pool_item_notify (PkBackendJob  *job,
			       const PoolItem &item,
			       gboolean sanity_check)
{
	PkInfoEnum status = PK_INFO_ENUM_UNKNOWN;

	if (item.status ().isToBeUninstalledDueToUpgrade ()) {
		status = PK_INFO_ENUM_UPDATING;
	} else if (item.status ().isToBeUninstalledDueToObsolete ()) {
		status = PK_INFO_ENUM_OBSOLETING;
	} else if (item.status ().isToBeInstalled ()) {
		status = PK_INFO_ENUM_INSTALLING;
	} else if (item.status ().isToBeUninstalled ()) {
		status = PK_INFO_ENUM_REMOVING;

		const string &name = item.satSolvable().name();
		if (name == "glibc" || name == "PackageKit" ||
		    name == "rpm" || name == "libzypp") {
			pk_backend_job_error_code (job, PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE,
					       "The package %s is essential to correct operation and cannot be removed using this tool.",
					       name.c_str());
			return FALSE;
		}
	}

	// FIXME: do we need more heavy lifting here cf. zypper's
	// Summary.cc (readPool) to generate _DOWNGRADING types ?
	if (status != PK_INFO_ENUM_UNKNOWN) {
		const string &summary = item.resolvable ()->summary ();
		zypp_backend_package (job, status, item.resolvable()->satSolvable(), summary.c_str ());
	}
	return TRUE;
}

gchar *
zypp_build_package_id_from_resolvable (const sat::Solvable &resolvable)
{
	gchar *package_id;
	const char *arch;

	if (isKind<SrcPackage>(resolvable))
		arch = "source";
	else
		arch = resolvable.arch ().asString ().c_str ();

	package_id = pk_package_id_build (
		resolvable.name ().c_str (),
		resolvable.edition ().asString ().c_str (),
		arch, resolvable.repository ().alias().c_str ());

	return package_id;
}

gboolean
zypp_ver_and_arch_equal (const sat::Solvable &pkg,
			 const char *name, const char *arch)
{
	const string &ver = pkg.edition ().asString();
	if (g_strcmp0 (ver.c_str (), name))
		return FALSE;

	if (arch && !strcmp (arch, "source")) {
		return isKind<SrcPackage>(pkg);
	}

	const Arch &parch = pkg.arch();
	if (g_strcmp0 (parch.c_str(), arch))
		return FALSE;

	return TRUE;
}
