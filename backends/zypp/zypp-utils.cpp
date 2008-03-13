#include <sstream>
#include <stdlib.h>
#include <glib.h>
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
#include <zypp/parser/ParseException.h>
#include <zypp/base/Algorithm.h>
#include <zypp/Pathname.h>
#include <zypp/Patch.h>
#include <zypp/Package.h>
#include <zypp/sat/Pool.h>
#include <zypp/target/rpm/RpmDb.h>
#include <zypp/target/rpm/RpmHeader.h>

#include <pk-backend.h>

#include "zypp-utils.h"

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

	zypp = zypp::ZYppFactory::instance ().getZYpp ();
	
	// TODO: Make this threadsafe
	if (initialized == FALSE) {
		zypp::filesystem::Pathname pathname("/");
		zypp->initializeTarget (pathname);

		initialized = TRUE;
	}

	return zypp;
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
                if (zypp::sat::Pool::instance().reposFind( zypp::sat::Pool::systemRepoName() ) == zypp::Repository::noRepository)
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
		repos = manager.knownRepositories ();
		for (std::list<zypp::RepoInfo>::iterator it = repos.begin(); it != repos.end (); it++) {
			zypp::RepoInfo repo (*it);

			// skip disabled repos
			if (repo.enabled () == false)
				continue;
                        // skip not cached repos
                        if (manager.isCached (repo) == false) {
                                pk_warning ("%s is not cached! Do a refresh", repo.alias ().c_str ());
                                continue;
                        }
                        //FIXME see above, skip already cached repos
                        if (zypp::sat::Pool::instance().reposFind( repo.alias ()) == zypp::Repository::noRepository)
                                manager.loadFromCache (repo);
		}
	} catch (const zypp::repo::RepoNoAliasException &ex) {
                pk_error ("Can't figure an alias to look in cache");
        } catch (const zypp::repo::RepoNotCachedException &ex) {
                pk_error ("The repo has to be cached at first: %s", ex.asUserString ().c_str ());
	} catch (const zypp::Exception &ex) {
                pk_error ("TODO: Handle exceptions: %s", ex.asUserString ().c_str ());
	}

	return zypp->pool ();
}

zypp::ResPool
zypp_build_local_pool ()
{
        zypp::sat::Pool pool = zypp::sat::Pool::instance ();

        for (zypp::detail::RepositoryIterator it = pool.reposBegin (); it != pool.reposEnd (); it++){
                if (! pool.reposEmpty ())
                        pool.reposErase(it->name ());
        }

        zypp::ZYpp::Ptr zypp = get_zypp ();
        if (zypp::sat::Pool::instance().reposFind( zypp::sat::Pool::systemRepoName() ) == zypp::Repository::noRepository)
        {
                // Add local resolvables
                zypp::Target_Ptr target = zypp->target ();
                target->load ();
        }

        return zypp->pool ();

}

zypp::target::rpm::RpmDb&
zypp_get_rpmDb()
{
        zypp::ZYpp::Ptr zypp = get_zypp ();
        zypp::Target_Ptr target = zypp->target ();

        zypp::target::rpm::RpmDb &rpm = target->rpmDb ();

        return rpm;
}

gchar*
zypp_get_group (zypp::sat::Solvable item, zypp::target::rpm::RpmDb &rpm)
{
        std::string group;

        if (item.isSystem ()) {

                zypp::target::rpm::RpmHeader::constPtr rpmHeader;
                rpm.getData (item.name (), item.edition (), rpmHeader);
                group = rpmHeader->tag_group ();

        }else{
                group = item.lookupStrAttribute (zypp::sat::SolvAttr::group);
        }
        std::transform(group.begin(), group.end(), group.begin(), tolower);
        return (gchar*)group.c_str ();
}

PkGroupEnum
get_enum_group (zypp::sat::Solvable item)
{
        
        zypp::target::rpm::RpmDb &rpm = zypp_get_rpmDb ();
        rpm.initDatabase ();

        std::string group (zypp_get_group (item, rpm));

        rpm.closeDatabase ();

        PkGroupEnum pkGroup = PK_GROUP_ENUM_UNKNOWN;
        // TODO Look for a faster and nice way to do this conversion

        if (group.find ("amusements") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_GAMES;
        } else if (group.find ("development") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_PROGRAMMING;
        } else if (group.find ("hardware") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_SYSTEM;
        } else if (group.find ("archiving") != std::string::npos 
                  || group.find("clustering") != std::string::npos
                  || group.find("system/monitoring") != std::string::npos
                  || group.find("databases") != std::string::npos
                  || group.find("system/management") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_ADMIN_TOOLS;
        } else if (group.find ("graphics") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_GRAPHICS;
        } else if (group.find ("mulitmedia") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_MULTIMEDIA;
        } else if (group.find ("network") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_NETWORK;
        } else if (group.find ("office") != std::string::npos 
                  || group.find("text") != std::string::npos
                  || group.find("editors") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_OFFICE;
        } else if (group.find ("publishing") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_PUBLISHING;
        } else if (group.find ("security") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_SECURITY;
        } else if (group.find ("telephony") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_COMMUNICATION;
        } else if (group.find ("gnome") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_DESKTOP_GNOME;
        } else if (group.find ("kde") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_DESKTOP_KDE;
        } else if (group.find ("xfce") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_DESKTOP_XFCE;
        } else if (group.find ("gui/other") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_DESKTOP_OTHER;
        } else if (group.find ("localization") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_LOCALIZATION;
        } else if (group.find ("system") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_SYSTEM;
        } else if (group.find ("scientific") != std::string::npos) {
                pkGroup = PK_GROUP_ENUM_EDUCATION;
        }

        return pkGroup;
}

std::vector<zypp::sat::Solvable> *
zypp_get_packages_by_name (const gchar *package_name, gboolean include_local)
{
	std::vector<zypp::sat::Solvable> *v = new std::vector<zypp::sat::Solvable> ();

	zypp::ResPool pool = zypp_build_pool (include_local);

        zypp::Capability cap (package_name, zypp::ResKind::package, zypp::Capability::PARSED);
        zypp::sat::WhatProvides provs (cap);

        for (zypp::sat::WhatProvides::const_iterator it = provs.begin ();
                        it != provs.end (); it++) {
                v->push_back (*it);
        }

	return v;
}

std::vector<zypp::sat::Solvable> *
zypp_get_packages_by_details (const gchar *search_term, gboolean include_local)
{
        std::vector<zypp::sat::Solvable> *v = new std::vector<zypp::sat::Solvable> ();

        zypp::ResPool pool = zypp_build_pool (include_local);

        std::string term (search_term);
        for (zypp::ResPool::byKind_iterator it = pool.byKindBegin (zypp::ResKind::package);
                        it != pool.byKindEnd (zypp::ResKind::package); it++) {
                if ((*it)->name ().find (term) != std::string::npos || (*it)->description ().find (term) != std::string::npos )
                    v->push_back ((*it)->satSolvable ());
        }

        return v;
}

std::vector<zypp::sat::Solvable> * 
zypp_get_packages_by_file (const gchar *search_file)
{
        std::vector<zypp::sat::Solvable> *v = new std::vector<zypp::sat::Solvable> ();

        zypp::ResPool pool = zypp_build_local_pool ();

        std::string file (search_file);

        zypp::ZYpp::Ptr zypp = get_zypp ();
        zypp::Target_Ptr target = zypp->target ();

        zypp::target::rpm::RpmDb &rpm = target->rpmDb ();
        rpm.initDatabase ();
        zypp::target::rpm::RpmHeader::constPtr rpmHeader;

        for (zypp::ResPool::byKind_iterator it = pool.byKindBegin (zypp::ResKind::package);
                        it != pool.byKindEnd (zypp::ResKind::package); it++) {
                rpm.getData ((*it)->name (), (*it)->edition (), rpmHeader);
                std::list<std::string> files = rpmHeader->tag_filenames ();

                if (std::find(files.begin(), files.end(), file) != files.end()) {
                        v->push_back ((*it)->satSolvable ());
                        break;
                }

        }

        return v;
}

zypp::sat::Solvable
zypp_get_package_by_id (const gchar *package_id)
{
	PkPackageId *pi;
	pi = pk_package_id_new_from_string (package_id);
	if (pi == NULL) {
		// TODO: Do we need to do something more for this error?
		return zypp::sat::Solvable::nosolvable;
	}

	std::vector<zypp::sat::Solvable> *v = zypp_get_packages_by_name (pi->name, TRUE);
	if (v == NULL)
		return zypp::sat::Solvable::nosolvable;

	zypp::sat::Solvable package;
	for (std::vector<zypp::sat::Solvable>::iterator it = v->begin ();
			it != v->end (); it++) {
		const char *version = it->edition ().asString ().c_str ();
                const char *arch = it->arch ().c_str ();
		if (strcmp (pi->version, version) == 0 || strcmp (pi->arch, arch) == 0) {
			package = *it;
			break;
		}
	}

	delete (v);
	return package;
}

gchar *
zypp_build_package_id_from_resolvable (zypp::sat::Solvable resolvable)
{
	gchar *package_id;
	
	package_id = pk_package_id_build (resolvable.name ().c_str (),
					  resolvable.edition ().asString ().c_str (),
					  resolvable.arch ().asString ().c_str (),
					  resolvable.vendor ().c_str ());
	// TODO: Figure out how to check if resolvable is really a ResObject and then cast it to a ResObject and pull of the repository alias for our "data" part in the package id
//					  ((zypp::ResObject::constPtr)resolvable)->repository ().info ().alias ().c_str ());

	return package_id;
}

gboolean
zypp_signature_required (PkBackend *backend, const zypp::PublicKey &key)
{
        gboolean ok = pk_backend_repo_signature_required (backend,
                        "TODO: Repo-Name",
                        key.path ().c_str (),
                        key.id ().c_str (),
                        key.id ().c_str (),
                        key.fingerprint ().c_str (),
                        key.created ().asString ().c_str (),
                        PK_SIGTYPE_ENUM_GPG);

        return ok;
}

gboolean
zypp_signature_required (PkBackend *backend, const std::string &file, const std::string &id)
{
        gboolean ok = pk_backend_repo_signature_required (backend,
                        "TODO: Repo-Name",
                        file.c_str (),
                        id.c_str (),
                        id.c_str (),
                        "UNKNOWN",
                        "UNKNOWN",
                        PK_SIGTYPE_ENUM_GPG);

        return ok;
}

gboolean
zypp_signature_required (PkBackend *backend, const std::string &file)
{
        gboolean ok = pk_backend_repo_signature_required (backend,
                        "TODO: Repo-Name",
                        file.c_str (),
                        "UNKNOWN",
                        "UNKNOWN",
                        "UNKNOWN",
                        "UNKNOWN",
                        PK_SIGTYPE_ENUM_GPG);

        return ok;
}

void
zypp_emit_packages_in_list (PkBackend *backend, std::vector<zypp::sat::Solvable> *v)
{
	for (std::vector<zypp::sat::Solvable>::iterator it = v->begin ();
			it != v->end (); it++) {

		// TODO: Determine whether this package is installed or not
		gchar *package_id = zypp_build_package_id_from_resolvable (*it);
		pk_backend_package (backend,
			    it->isSystem() == true ?
				PK_INFO_ENUM_INSTALLED :
				PK_INFO_ENUM_AVAILABLE,
			    package_id,
			    it->lookupStrAttribute (zypp::sat::SolvAttr::description).c_str ());
		g_free (package_id);
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
zypp_get_updates ()
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
                pks->insert (candidate);
        }

        return pks;
}

std::set<zypp::ui::Selectable::Ptr> *
zypp_get_patches ()
{
        std::set<zypp::ui::Selectable::Ptr> *patches = new std::set<zypp::ui::Selectable::Ptr> ();

        zypp::ZYpp::Ptr zypp;
        zypp = get_zypp ();

        for (zypp::ResPoolProxy::const_iterator it = zypp->poolProxy ().byKindBegin<zypp::Patch>();
                        it != zypp->poolProxy ().byKindEnd<zypp::Patch>(); it ++) {
                // check if patch is needed 
                if((*it)->candidatePoolItem ().status ().isNeeded())
                        patches->insert (*it);

        }

        return patches;

}

gboolean
zypp_perform_execution (PkBackend *backend, PerformType type, gboolean force)
{
        try {
                zypp::ZYpp::Ptr zypp = get_zypp ();

                if (force)
                        zypp->resolver ()->setForceResolve (force);

                // Gather up any dependencies
                pk_backend_set_status (backend, PK_STATUS_ENUM_DEP_RESOLVE);
                if (zypp->resolver ()->resolvePool () == FALSE) {
                       // Manual intervention required to resolve dependencies
                       // TODO: Figure out what we need to do with PackageKit
                       // to pull off interactive problem solving.

                        pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Couldn't resolve the package dependencies.");
                        pk_backend_finished (backend);
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

                // Perform the installation
                zypp::ZYppCommitPolicy policy;
                policy.restrictToMedia (0);	// 0 - install all packages regardless to media
                zypp::ZYppCommitResult result = zypp->commit (policy);

                if(!result._errors.empty () || !result._remaining.empty () || !result._srcremaining.empty ()){
                        pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, "There are packages which couldn't be installed");
                        pk_backend_finished (backend);
                        return FALSE;
                }

                zypp->resolver ()->setForceResolve (FALSE);

        } catch (const zypp::repo::RepoNotFoundException &ex) {
                // TODO: make sure this dumps out the right sring.
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
		pk_backend_finished (backend);
		return FALSE;
	} catch (const zypp::Exception &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str() );
		pk_backend_finished (backend);
		return FALSE;
	}       
        
        return TRUE;
}
