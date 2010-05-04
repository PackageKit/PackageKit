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
#include <zypp/sat/Pool.h>
#include <zypp/target/rpm/RpmDb.h>
#include <zypp/target/rpm/RpmHeader.h>
#include <zypp/target/rpm/librpmDb.h>
#include <zypp/base/LogControl.h>
#include <zypp/base/String.h>

#include <zypp/base/Logger.h>

#include <pk-backend.h>
#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

#include "zypp-utils.h"

gchar * _repoName;
gboolean _updating_self = FALSE;
/**
 * Collect items, select best edition.  This is used to find the best
 * available or installed.  The name of the class is a bit misleading though ...
 */
class LookForArchUpdate : public zypp::resfilter::PoolItemFilterFunctor
{
	public:
		zypp::PoolItem best;

	bool operator() (zypp::PoolItem provider)
	{
		if ((provider.status ().isLocked () == FALSE) && (!best || best->edition ().compare (provider->edition ()) < 0)) {
			best = provider;
		}

		return true;
	}
};

/**
 * Initialize Zypp (Factory method)
 */
zypp::ZYpp::Ptr
get_zypp ()
{
	static gboolean initialized = FALSE;
        zypp::ZYpp::Ptr zypp = NULL;

        try {
	        zypp = zypp::ZYppFactory::instance ().getZYpp ();

	        // TODO: Make this threadsafe
	        if (initialized == FALSE) {
		        zypp::filesystem::Pathname pathname("/");
		        zypp->initializeTarget (pathname);

		        initialized = TRUE;
	        }
        } catch (const zypp::Exception &ex) {
		egg_error ("%s", ex.asUserString ().c_str ());
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

	zypp::base::LogControl::instance ().logfile(file);

	g_free (file);
	g_free (file_old);

	return TRUE;
}

gboolean
zypp_is_changeable_media (const zypp::Url &url)
{
	gboolean is_cd = false;
	try {
		zypp::media::MediaManager mm;
		zypp::media::MediaAccessId id = mm.open (url);
		is_cd = mm.isChangeable (id);
		mm.close (id);
	} catch (const zypp::media::MediaException &e) {
		// TODO: Do anything about this?
	}

	return is_cd;
}

zypp::ResPool
zypp_build_pool (gboolean include_local)
{
	zypp::ZYpp::Ptr zypp = get_zypp ();

	if (include_local == TRUE) {
		//FIXME have to wait for fix in zypp (repeated loading of target)
		if (zypp::sat::Pool::instance().reposFind( zypp::sat::Pool::systemRepoAlias() ).solvablesEmpty ())
		{
			// Add local resolvables
			zypp::Target_Ptr target = zypp->target ();
			target->load ();
		}
	}

	// Add resolvables from enabled repos
	zypp::RepoManager manager;
	std::list<zypp::RepoInfo> repos;
	try {
		repos = std::list<zypp::RepoInfo>(manager.repoBegin(),manager.repoEnd());
		for (std::list<zypp::RepoInfo>::iterator it = repos.begin(); it != repos.end (); it++) {
			zypp::RepoInfo repo (*it);

			// skip disabled repos
			if (repo.enabled () == false)
				continue;
                        // skip not cached repos
                        if (manager.isCached (repo) == false) {
                                egg_warning ("%s is not cached! Do a refresh", repo.alias ().c_str ());
                                continue;
                        }
                        //FIXME see above, skip already cached repos
                        if (zypp::sat::Pool::instance().reposFind( repo.alias ()) == zypp::Repository::noRepository)
                                manager.loadFromCache (repo);
		}
	} catch (const zypp::repo::RepoNoAliasException &ex) {
                egg_error ("Can't figure an alias to look in cache");
        } catch (const zypp::repo::RepoNotCachedException &ex) {
                egg_error ("The repo has to be cached at first: %s", ex.asUserString ().c_str ());
	} catch (const zypp::Exception &ex) {
                egg_error ("TODO: Handle exceptions: %s", ex.asUserString ().c_str ());
	}

	return zypp->pool ();
}

void
warn_outdated_repos(PkBackend *backend, const zypp::ResPool & pool)
{
	zypp::Repository repoobj;
	zypp::ResPool::repository_iterator it;
	for ( it = pool.knownRepositoriesBegin();
		it != pool.knownRepositoriesEnd();
		++it )
	{
		zypp::Repository repo(*it);
		if ( repo.maybeOutdated() )
		{
			// warn the user
			pk_backend_message (backend,
					PK_MESSAGE_ENUM_BROKEN_MIRROR,
					zypp::str::form("The repository %s seems to be outdated. You may want to try another mirror.",
					repo.alias().c_str()).c_str() );
		}
	}
}

zypp::ResPool
zypp_build_local_pool ()
{
	zypp::sat::Pool pool = zypp::sat::Pool::instance ();
	zypp::ZYpp::Ptr zypp = get_zypp ();

	try {
		for (zypp::detail::RepositoryIterator it = pool.reposBegin (); it != pool.reposEnd (); it++){
			if (! it->isSystemRepo ())
				pool.reposErase(it->alias ());
		}

		if (zypp::sat::Pool::instance().reposFind( zypp::sat::Pool::systemRepoAlias() ).solvablesEmpty ())
                {
		        // Add local resolvables
		        zypp::Target_Ptr target = zypp->target ();
		        target->load ();
                }

	} catch (const zypp::Exception &ex) {
		egg_error ("%s", ex.asUserString ().c_str ());
	}

        return zypp->pool ();

}

zypp::target::rpm::RpmHeader::constPtr
zypp_get_rpmHeader (std::string name, zypp::Edition edition)
{
	zypp::target::rpm::librpmDb::db_const_iterator it;
	zypp::target::rpm::RpmHeader::constPtr result = new zypp::target::rpm::RpmHeader ();

	for (it.findPackage (name, edition); *it; ++it) {
		result = *it;
	}

	return result;
}


std::string
zypp_get_group (zypp::sat::Solvable item)
{
        std::string group;

        if (item.isSystem ()) {

                zypp::target::rpm::RpmHeader::constPtr rpmHeader = zypp_get_rpmHeader (item.name (), item.edition ());
                group = rpmHeader->tag_group ();

        } else {
                group = item.lookupStrAttribute (zypp::sat::SolvAttr::group);
        }
        std::transform(group.begin(), group.end(), group.begin(), tolower);
        return group;
}

PkGroupEnum
get_enum_group (std::string group)
{

        // TODO Look for a faster and nice way to do this conversion
        std::transform(group.begin(), group.end(), group.begin(), tolower);

        if (group.find ("amusements") != std::string::npos) {
                return PK_GROUP_ENUM_GAMES;
        } else if (group.find ("development") != std::string::npos) {
                return PK_GROUP_ENUM_PROGRAMMING;
        } else if (group.find ("hardware") != std::string::npos) {
                return PK_GROUP_ENUM_SYSTEM;
        } else if (group.find ("archiving") != std::string::npos
                  || group.find("clustering") != std::string::npos
                  || group.find("system/monitoring") != std::string::npos
                  || group.find("databases") != std::string::npos
                  || group.find("system/management") != std::string::npos) {
                return PK_GROUP_ENUM_ADMIN_TOOLS;
        } else if (group.find ("graphics") != std::string::npos) {
                return PK_GROUP_ENUM_GRAPHICS;
        } else if (group.find ("multimedia") != std::string::npos) {
                return PK_GROUP_ENUM_MULTIMEDIA;
        } else if (group.find ("network") != std::string::npos) {
                return PK_GROUP_ENUM_NETWORK;
        } else if (group.find ("office") != std::string::npos
                  || group.find("text") != std::string::npos
                  || group.find("editors") != std::string::npos) {
                return PK_GROUP_ENUM_OFFICE;
        } else if (group.find ("publishing") != std::string::npos) {
                return PK_GROUP_ENUM_PUBLISHING;
        } else if (group.find ("security") != std::string::npos) {
                return PK_GROUP_ENUM_SECURITY;
        } else if (group.find ("telephony") != std::string::npos) {
                return PK_GROUP_ENUM_COMMUNICATION;
        } else if (group.find ("gnome") != std::string::npos) {
                return PK_GROUP_ENUM_DESKTOP_GNOME;
        } else if (group.find ("kde") != std::string::npos) {
                return PK_GROUP_ENUM_DESKTOP_KDE;
        } else if (group.find ("xfce") != std::string::npos) {
                return PK_GROUP_ENUM_DESKTOP_XFCE;
        } else if (group.find ("gui/other") != std::string::npos) {
                return PK_GROUP_ENUM_DESKTOP_OTHER;
        } else if (group.find ("localization") != std::string::npos) {
                return PK_GROUP_ENUM_LOCALIZATION;
        } else if (group.find ("system") != std::string::npos) {
                return PK_GROUP_ENUM_SYSTEM;
        } else if (group.find ("scientific") != std::string::npos) {
                return PK_GROUP_ENUM_EDUCATION;
        }

        return PK_GROUP_ENUM_UNKNOWN;
}

std::vector<zypp::sat::Solvable> *
zypp_get_packages_by_name (const gchar *package_name, const zypp::ResKind kind, gboolean include_local)
{
	std::vector<zypp::sat::Solvable> *v = new std::vector<zypp::sat::Solvable> ();

	zypp::ResPool pool = zypp_build_pool (include_local);

        for (zypp::ResPool::byIdent_iterator it = pool.byIdentBegin (kind, package_name);
                        it != pool.byIdentEnd (kind, package_name); it++) {
                v->push_back (it->satSolvable ());
        }

	return v;
}

std::vector<zypp::sat::Solvable> *
zypp_get_packages_by_file (const gchar *search_file)
{
        std::vector<zypp::sat::Solvable> *v = new std::vector<zypp::sat::Solvable> ();

        zypp::ResPool pool = zypp_build_pool (TRUE);

        std::string file (search_file);

	zypp::target::rpm::librpmDb::db_const_iterator it;
	zypp::target::rpm::RpmHeader::constPtr result = new zypp::target::rpm::RpmHeader ();

	for (it.findByFile (search_file); *it; ++it) {
		for (zypp::ResPool::byName_iterator it2 = pool.byNameBegin (it->tag_name ()); it2 != pool.byNameEnd (it->tag_name ()); it2++) {
			if ((*it2)->isSystem ())
				v->push_back ((*it2)->satSolvable ());
		}
	}

	if (v->empty ()) {
		zypp::Capability cap (search_file);
		zypp::sat::WhatProvides prov (cap);

		for(zypp::sat::WhatProvides::const_iterator it = prov.begin (); it != prov.end (); it++) {
			v->push_back (*it);
		}
	}

	return v;
}

zypp::sat::Solvable
zypp_get_package_by_id (const gchar *package_id)
{
	if (!pk_package_id_check(package_id)) {
		// TODO: Do we need to do something more for this error?
		return zypp::sat::Solvable::noSolvable;
	}

	gchar **id_parts = pk_package_id_split(package_id);
	std::vector<zypp::sat::Solvable> *v = zypp_get_packages_by_name (id_parts[PK_PACKAGE_ID_NAME], zypp::ResKind::package, TRUE);
	std::vector<zypp::sat::Solvable> *v2 = zypp_get_packages_by_name (id_parts[PK_PACKAGE_ID_NAME], zypp::ResKind::patch, TRUE);

	v->insert (v->end (), v2->begin (), v2->end ());

	if (v == NULL)
		return zypp::sat::Solvable::noSolvable;

	zypp::sat::Solvable package;

	for (std::vector<zypp::sat::Solvable>::iterator it = v->begin ();
			it != v->end (); it++) {
		if (zypp_ver_and_arch_equal (*it, id_parts[PK_PACKAGE_ID_VERSION],
					     id_parts[PK_PACKAGE_ID_ARCH])) {
			package = *it;
			break;
		}
	}

	delete (v);
	delete (v2);
	g_strfreev (id_parts);
	return package;
}

zypp::RepoInfo
zypp_get_Repository (PkBackend *backend, const gchar *alias)
{
	zypp::RepoInfo info;

	try {
		zypp::RepoManager manager;
		info = manager.getRepositoryInfo (alias);
	} catch (const zypp::repo::RepoNotFoundException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
		return zypp::RepoInfo ();
	}

	return info;
}

gboolean
zypp_signature_required (PkBackend *backend, const zypp::PublicKey &key)
{
	gboolean ok = FALSE;

	if (std::find (_signatures[backend]->begin (), _signatures[backend]->end (), key.id ()) == _signatures[backend]->end ()) {
		zypp::RepoInfo info = zypp_get_Repository (backend, _repoName);
		if (info.type () == zypp::repo::RepoType::NONE) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Repository unknown");
			return FALSE;
		}

        	pk_backend_repo_signature_required (backend,
				"dummy;0.0.1;i386;data",
	                        _repoName,
        	                info.baseUrlsBegin ()->asString ().c_str (),
                	        key.name ().c_str (),
                        	key.id ().c_str (),
	                        key.fingerprint ().c_str (),
        	                key.created ().asString ().c_str (),
                	        PK_SIGTYPE_ENUM_GPG);
		pk_backend_error_code (backend, PK_ERROR_ENUM_GPG_FAILURE, "Signature verification for Repository %s failed", _repoName);
	} else {
		ok = TRUE;
	}

        return ok;
}

gboolean
zypp_signature_required (PkBackend *backend, const std::string &file, const std::string &id)
{
        gboolean ok = FALSE;

	if (std::find (_signatures[backend]->begin (), _signatures[backend]->end (), id) == _signatures[backend]->end ()) {
		zypp::RepoInfo info = zypp_get_Repository (backend, _repoName);
		if (info.type () == zypp::repo::RepoType::NONE) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Repository unknown");
			return FALSE;
		}

		pk_backend_repo_signature_required (backend,
				"dummy;0.0.1;i386;data",
	                        _repoName,
        	                info.baseUrlsBegin ()->asString ().c_str (),
                	        id.c_str (),
                        	id.c_str (),
	                        "UNKNOWN",
        	                "UNKNOWN",
                	        PK_SIGTYPE_ENUM_GPG);
		pk_backend_error_code (backend, PK_ERROR_ENUM_GPG_FAILURE, "Signature verification for Repository %s failed", _repoName);
	} else {
		ok = TRUE;
	}

        return ok;
}

gboolean
zypp_signature_required (PkBackend *backend, const std::string &file)
{
	gboolean ok = FALSE;

	if (std::find (_signatures[backend]->begin (), _signatures[backend]->end (), file) == _signatures[backend]->end ()) {
        	zypp::RepoInfo info = zypp_get_Repository (backend, _repoName);
		if (info.type () == zypp::repo::RepoType::NONE) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Repository unknown");
			return FALSE;
		}

		pk_backend_repo_signature_required (backend,
				"dummy;0.0.1;i386;data",
	                        _repoName,
        	                info.baseUrlsBegin ()->asString ().c_str (),
	                        "UNKNOWN",
        	                file.c_str (),
                	        "UNKNOWN",
                        	"UNKNOWN",
	                        PK_SIGTYPE_ENUM_GPG);
		pk_backend_error_code (backend, PK_ERROR_ENUM_GPG_FAILURE, "Signature verification for Repository %s failed", _repoName);
	} else {
		ok = TRUE;
	}

        return ok;
}

gboolean
system_and_package_are_x86 (zypp::sat::Solvable item)
{
	// i586, i686, ... all should be considered the same arch for our comparison
	return (!strcmp (item.arch ().asString ().c_str (), "i586") &&
			!strcmp (zypp::ZConfig::defaultSystemArchitecture ().asString().c_str(), "i686"));
}

/* should we filter out this package ? */
gboolean
zypp_filter_solvable (PkBitfield filters, const zypp::sat::Solvable &item)
{
	// iterate through the given filters
	if (!filters)
		return FALSE;

	//const gchar * myarch = zypp::ZConfig::defaultSystemArchitecture().asString().c_str();

	for (guint i = 0; i < PK_FILTER_ENUM_LAST; i++) {
		if ((filters & pk_bitfield_value (i)) == 0)
			continue;
		if (i == PK_FILTER_ENUM_INSTALLED && !(item.isSystem ()))
			return TRUE;
		if (i == PK_FILTER_ENUM_NOT_INSTALLED && item.isSystem ())
			return TRUE;
		if (i == PK_FILTER_ENUM_ARCH) {
			if (item.arch () != zypp::ZConfig::defaultSystemArchitecture () &&
			    item.arch () != zypp::Arch_noarch &&
			    ! system_and_package_are_x86 (item))
				return TRUE;
		}
		if (i == PK_FILTER_ENUM_NOT_ARCH) {
			if (item.arch () == zypp::ZConfig::defaultSystemArchitecture () ||
			    system_and_package_are_x86 (item))
				return TRUE;
		}
		if (i == PK_FILTER_ENUM_SOURCE && !(zypp::isKind<zypp::SrcPackage>(item))) {
			return TRUE;
		}
		if (i == PK_FILTER_ENUM_NOT_SOURCE && zypp::isKind<zypp::SrcPackage>(item)) {
			return TRUE;
		}

		// FIXME: add enums
		// PK_FILTER_ENUM_DEVELOPMENT,
		// PK_FILTER_ENUM_NOT_DEVELOPMENT,

	}

	return FALSE;
}

void
zypp_emit_filtered_packages_in_list (PkBackend *backend, const std::vector<zypp::sat::Solvable> &v)
{
	PkBitfield filters = (PkBitfield) pk_backend_get_uint (backend, "filters");

	for (std::vector<zypp::sat::Solvable>::const_iterator it = v.begin ();
			it != v.end (); it++) {

		if (zypp_filter_solvable (filters, *it))
			continue;
		zypp_backend_package (backend, 
				      it->isSystem() == true ?
				      PK_INFO_ENUM_INSTALLED :
				      PK_INFO_ENUM_AVAILABLE,
				      *it,
				      it->lookupStrAttribute (zypp::sat::SolvAttr::summary).c_str ());
	}
}

/**
 * The following method was taken directly from zypper code
 *
 * Find best (according to edition) uninstalled item
 * with the same kind/name/arch as item.
 * Similar to zypp::solver::detail::Helper::findUpdateItem
 * but that allows changing the arch (#222140).
 */
zypp::PoolItem
zypp_find_arch_update_item (const zypp::ResPool & pool, zypp::PoolItem item)
{
	LookForArchUpdate info;

	invokeOnEach (pool.byIdentBegin (item),
			pool.byIdentEnd (item),
			// get uninstalled, equal kind and arch, better edition
			zypp::functor::chain (
				zypp::functor::chain (
					zypp::functor::chain (
						zypp::resfilter::ByUninstalled (),
						zypp::resfilter::ByKind (item->kind ())),
					zypp::resfilter::byArch<zypp::CompareByEQ<zypp::Arch> > (item->arch ())),
				zypp::resfilter::byEdition<zypp::CompareByGT<zypp::Edition> > (item->edition ())),
			zypp::functor::functorRef<bool,zypp::PoolItem> (info));

	return info.best;
}

std::set<zypp::PoolItem> *
zypp_get_updates (std::string repo)
{
        std::set<zypp::PoolItem> *pks = new std::set<zypp::PoolItem> ();
        zypp::ResPool pool = zypp::ResPool::instance ();

        zypp::ResObject::Kind kind = zypp::ResTraits<zypp::Package>::kind;
        zypp::ResPool::byKind_iterator it = pool.byKindBegin (kind);
        zypp::ResPool::byKind_iterator e = pool.byKindEnd (kind);

        for (; it != e; ++it) {
                if (it->status ().isUninstalled ())
                        continue;
                zypp::PoolItem candidate =  zypp_find_arch_update_item (pool, *it);
                if (!candidate.resolvable ())
                        continue;
		if (repo.empty ()) {
	                pks->insert (candidate);
		} else {
			if (candidate->repoInfo ().alias ().compare (repo) != 0)
				pks->insert (candidate);
		}
        }

        return pks;
}

std::set<zypp::PoolItem> *
zypp_get_patches ()
{
        std::set<zypp::PoolItem> *patches = new std::set<zypp::PoolItem> ();
	_updating_self = FALSE;

        zypp::ZYpp::Ptr zypp;
        zypp = get_zypp ();

	zypp->resolver ()->resolvePool ();

        for (zypp::ResPoolProxy::const_iterator it = zypp->poolProxy ().byKindBegin<zypp::Patch>();
                        it != zypp->poolProxy ().byKindEnd<zypp::Patch>(); it ++) {
                // check if patch is needed
                if((*it)->isNeeded()) {
			zypp::Patch::constPtr patch = zypp::asKind<zypp::Patch>((*it)->candidateObj ().resolvable ());
			if (_updating_self) {
				if (patch->restartSuggested ())
					patches->insert ((*it)->candidateObj ());
			}
			else
				patches->insert ((*it)->candidateObj ());

			// check if the patch updates libzypp or packageKit and show only these
			if (!_updating_self && patch->restartSuggested ()) {
				_updating_self = TRUE;
				patches->clear ();
				patches->insert ((*it)->candidateObj ());
			}
		}

        }

        return patches;

}

gboolean
zypp_get_restart (PkRestartEnum &restart, zypp::Patch::constPtr patch)
{
	// set the restart flag if a restart is needed
	if (restart != PK_RESTART_ENUM_SYSTEM && (patch->reloginSuggested () ||
						  patch->restartSuggested () ||
						  patch->rebootSuggested ())) {
			if(patch->reloginSuggested () || patch->restartSuggested ())
				restart = PK_RESTART_ENUM_SESSION;
			if(patch->rebootSuggested ())
				restart = PK_RESTART_ENUM_SYSTEM;
	}
	return true;
}

gboolean
zypp_perform_execution (PkBackend *backend, PerformType type, gboolean force)
{
        try {
                zypp::ZYpp::Ptr zypp = get_zypp ();

                if (force)
                        zypp->resolver ()->setForceResolve (force);
		if (type == UPDATE) {
			//zypp->resolver ()->setOnlyRequires (TRUE);
			zypp->resolver ()->setIgnoreAlreadyRecommended (TRUE);
		}

                // Gather up any dependencies
		pk_backend_set_status (backend, PK_STATUS_ENUM_DEP_RESOLVE);
		if (zypp->resolver ()->resolvePool () == FALSE) {
                       // Manual intervention required to resolve dependencies
                       // TODO: Figure out what we need to do with PackageKit
                       // to pull off interactive problem solving.

			zypp::ResolverProblemList problems = zypp->resolver ()->problems ();
			gchar * emsg = NULL, * tempmsg = NULL;

			for (zypp::ResolverProblemList::iterator it = problems.begin (); it != problems.end (); it++){
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
			zypp::ResPool pool = zypp::ResPool::instance ();
			for (zypp::ResPool::const_iterator it = pool.begin (); it != pool.end (); it++) {
				if (it->status ().isToBeInstalled ())
					it->statusReset ();
			}

			pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, emsg);
			g_free (emsg);
			zypp->resolver ()->setForceResolve (FALSE);
			return FALSE;
		}

                switch (type) {
                        case INSTALL:
                                pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
                                break;
                        case REMOVE:
                                pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
                                break;
                        case UPDATE:
                                pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
                                break;
                };

		// look for licenses to confirm

		zypp::ResPool pool = zypp::ResPool::instance ();
		for (zypp::ResPool::const_iterator it = pool.begin (); it != pool.end (); it++) {
			if (it->status ().isToBeInstalled () && !(it->satSolvable ().lookupStrAttribute (zypp::sat::SolvAttr::eula).empty ())) {
				gchar *eula_id = g_strdup ((*it)->name ().c_str ());
				gboolean has_eula = pk_backend_is_eula_valid (backend, eula_id);
				if (!has_eula) {
					gchar *package_id = zypp_build_package_id_from_resolvable (it->satSolvable ());
					pk_backend_eula_required (backend,
							eula_id,
							package_id,
							(*it)->vendor ().c_str (),
							it->satSolvable ().lookupStrAttribute (zypp::sat::SolvAttr::eula).c_str ());
					pk_backend_error_code (backend, PK_ERROR_ENUM_NO_LICENSE_AGREEMENT, "You've to agree/decline a license");
					g_free (package_id);
					g_free (eula_id);
					return FALSE;
				}
				g_free (eula_id);
			}
		}

                // Perform the installation
                zypp::ZYppCommitPolicy policy;
                policy.restrictToMedia (0); // 0 == install all packages regardless to media
		policy.downloadMode (zypp::DownloadInHeaps);
		policy.syncPoolAfterCommit (true);

                zypp::ZYppCommitResult result = zypp->commit (policy);

                if(!result._errors.empty () || !result._remaining.empty () || !result._srcremaining.empty ()){

			zypp::ZYppCommitResult::PoolItemList errors = result._errors;
			gchar *emsg = NULL, *tmpmsg = NULL;

			for (zypp::ZYppCommitResult::PoolItemList::iterator it = errors.begin (); it != errors.end (); it++){
				if (emsg == NULL) {
					emsg = g_strdup ((*it)->name ().c_str ());
				} else {
					tmpmsg = emsg;
					emsg = g_strconcat (emsg, "\n", (*it)->name ().c_str (), NULL);
					g_free (tmpmsg);
				}
			}

			zypp::ZYppCommitResult::PoolItemList remaining = result._remaining;
			for (zypp::ZYppCommitResult::PoolItemList::iterator it = remaining.begin (); it != remaining.end (); it++){
				if (emsg == NULL) {
					emsg = g_strdup ((*it)->name ().c_str ());
				} else {
					tmpmsg = emsg;
					emsg = g_strconcat (emsg, "\n", (*it)->name ().c_str (), NULL);
					g_free (tmpmsg);
				}
			}

			zypp::ZYppCommitResult::PoolItemList srcremaining = result._srcremaining;
			for (zypp::ZYppCommitResult::PoolItemList::iterator it = srcremaining.begin (); it != srcremaining.end (); it++){
				if (emsg == NULL) {
					emsg = g_strdup ((*it)->name ().c_str ());
				} else {
					tmpmsg = emsg;
					emsg = g_strconcat (emsg, "\n", (*it)->name ().c_str (), NULL);
					g_free (tmpmsg);
				}
			}

                        pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR,
					"Transaction could not be completed.\n Theses packages could not be installed: %s",
					emsg);

			g_free (emsg);
			zypp->resolver ()->setForceResolve (FALSE);
                        return FALSE;
                }

		zypp->resolver ()->setForceResolve (FALSE);
		if (type == UPDATE) {
			zypp->resolver ()->setIgnoreAlreadyRecommended (FALSE);
		}

        } catch (const zypp::repo::RepoNotFoundException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
		return FALSE;
	} catch (const zypp::target::rpm::RpmException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED, ex.asUserString().c_str () );
		return FALSE;
	} catch (const zypp::Exception &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str() );
		return FALSE;
	}

        return TRUE;
}

gchar **
zypp_convert_set_char (std::set<zypp::sat::Solvable> *set)
{
        gchar **array = new gchar* [set->size ()];
        guint i = 0;

        for (std::set<zypp::sat::Solvable>::iterator it = set->begin (); it != set->end (); it++){
                gchar *package_id = zypp_build_package_id_from_resolvable (*it);
                array[i] = g_strdup(package_id);
                i++;
                g_free (package_id);
        }

        return array;
}

gchar *
zypp_build_package_id_capabilities (zypp::Capabilities caps)
{
	gchar * package_ids = new gchar ();

	zypp::sat::WhatProvides provs (caps);

	for (zypp::sat::WhatProvides::const_iterator it = provs.begin (); it != provs.end (); it++) {
		gchar *package_id = zypp_build_package_id_from_resolvable (*it);
		//package_ids = g_strconcat (package_ids, package_id, PK_PACKAGE_IDS_DELIM, (gchar *)NULL);
		if (strlen (package_ids) == 0) {
			package_ids = g_strdup (package_id);
		} else {
			package_ids = g_strconcat (package_ids, PK_PACKAGE_IDS_DELIM, package_id, (gchar *)NULL);
		}
		g_free (package_id);
	}

	return package_ids;
}

gboolean
zypp_refresh_cache (PkBackend *backend, gboolean force)
{
	get_zypp ();  //This call is needed as it calls initializeTarget which appears to properly setup the keyring
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		return FALSE;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_set_percentage (backend, 0);

	zypp::RepoManager manager;
	std::list <zypp::RepoInfo> repos;
	try
	{
		repos = std::list<zypp::RepoInfo>(manager.repoBegin(),manager.repoEnd());
	}
	catch ( const zypp::Exception &e)
	{
		// FIXME: make sure this dumps out the right sring.
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, e.asUserString().c_str() );
		pk_backend_finished (backend);
		return FALSE;
	}

	int i = 1;
	int num_of_repos = repos.size ();
	int percentage_increment = 100 / num_of_repos;
	gchar *repo_messages = NULL;

	for (std::list <zypp::RepoInfo>::iterator it = repos.begin(); it != repos.end(); it++, i++) {
		zypp::RepoInfo repo (*it);

		// skip disabled repos
		if (repo.enabled () == false)
			continue;

		// skip changeable meda (DVDs and CDs).  Without doing this,
		// the disc would be required to be physically present.
		if (zypp_is_changeable_media (*repo.baseUrlsBegin ()) == true)
			continue;

		try {
			// Refreshing metadata
			_repoName = g_strdup (repo.alias ().c_str ());
			manager.refreshMetadata (repo, force == TRUE ?
				zypp::RepoManager::RefreshForced :
				zypp::RepoManager::RefreshIfNeeded);
			manager.buildCache (repo, force == TRUE ?
				zypp::RepoManager::BuildForced :
				zypp::RepoManager::BuildIfNeeded);
		} catch (const zypp::Exception &ex) {
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
		pk_backend_set_percentage (backend, i == num_of_repos ? 100 : i * percentage_increment);
	}
	if (repo_messages != NULL)
		pk_backend_message (backend, PK_MESSAGE_ENUM_CONNECTION_REFUSED, repo_messages);

	g_free (repo_messages);
	return TRUE;
}

gboolean
zypp_backend_finished_error (PkBackend  *backend, PkErrorEnum err_code,
			     const char *format, ...)
{
	va_list args;
	gchar *buffer;

	/* sadly no _va variant for error code setting */
	va_start (args, format);
	buffer = g_strdup_vprintf (format, args);
	va_end (args);

	pk_backend_error_code (backend, err_code, "%s", buffer);
	
	g_free (buffer);

	pk_backend_finished (backend);

	return FALSE;
}

void
zypp_backend_package (PkBackend *backend, PkInfoEnum info,
		      const zypp::sat::Solvable &pkg,
		      const char *opt_summary)
{
	gchar *id = zypp_build_package_id_from_resolvable (pkg);
	pk_backend_package (backend, info, id, opt_summary);
	g_free (id);
}

gchar *
zypp_build_package_id_from_resolvable (const zypp::sat::Solvable &resolvable)
{
	gchar *package_id;
	const char *arch;

	if (zypp::isKind<zypp::SrcPackage>(resolvable))
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
zypp_ver_and_arch_equal (const zypp::sat::Solvable &pkg,
			 const char *name, const char *arch)
{
	const std::string &ver = pkg.edition ().asString();
	if (g_strcmp0 (ver.c_str (), name))
	    return FALSE;

	if (arch && !strcmp (arch, "source")) {
		return zypp::isKind<zypp::SrcPackage>(pkg);
	}

	const zypp::Arch &parch = pkg.arch();
	if (g_strcmp0 (parch.c_str(), arch))
		return FALSE;

	return TRUE;
}
