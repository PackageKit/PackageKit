#ifndef _ZYPP_UTILS_H_
#define _ZYPP_UTILS_H_

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
#include <zypp/Pathname.h>
#include <zypp/Patch.h>
#include <zypp/Package.h>

#include <pk-backend.h>

#include "zypp-utils.h"

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
		// Add local resolvables
		zypp::Target_Ptr target = zypp->target ();
		zypp->addResolvables (target->resolvables (), TRUE);
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

			zypp::Repository repository = manager.createFromCache (repo);
			zypp->addResolvables (repository.resolvables ());
		}
//	} catch (const zypp::repo::RepoNoAliasException &ex) {
//	} catch (const zypp::repo::RepoNotCachedException &ex) {
	} catch (const zypp::Exception &ex) {
fprintf (stderr, "TODO: Handle exceptions: %s\n", ex.asUserString ().c_str ());
	}

	return zypp->pool ();
}

std::vector<zypp::PoolItem> *
zypp_get_packages_by_name (const gchar *package_name, gboolean include_local)
{
	std::vector<zypp::PoolItem> *v = new std::vector<zypp::PoolItem> ();

	zypp::ResPool pool = zypp_build_pool (include_local);

	std::string name (package_name);
	for (zypp::ResPool::byName_iterator it = pool.byNameBegin (name);
			it != pool.byNameEnd (name); it++) {
		zypp::PoolItem item = (*it);
		v->push_back (item);
	}

	return v;
}

/**
 * Build a package_id from the specified resolvable.  The returned
 * gchar * should be freed with g_free ().
 */
gchar *
zypp_build_package_id_from_resolvable (zypp::Resolvable::constPtr resolvable)
{
	gchar *package_id;
	
	package_id = pk_package_id_build (resolvable->name ().c_str (),
					  resolvable->edition ().asString ().c_str (),
					  resolvable->arch ().asString ().c_str (),
					  "opensuse");
	// TODO: Figure out how to check if resolvable is really a ResObject and then cast it to a ResObject and pull of the repository alias for our "data" part in the package id
//					  ((zypp::ResObject::constPtr)resolvable)->repository ().info ().alias ().c_str ());

	return package_id;
}

void
zypp_emit_packages_in_list (PkBackend *backend, std::vector<zypp::PoolItem> *v)
{
	for (std::vector<zypp::PoolItem>::iterator it = v->begin ();
			it != v->end (); it++) {
		zypp::ResObject::constPtr pkg = (*it);

		// TODO: Determine whether this package is installed or not
		gchar *package_id = zypp_build_package_id_from_resolvable (pkg);
		pk_backend_package (backend,
			    it->status().isInstalled() == true ?
				PK_INFO_ENUM_INSTALLED :
				PK_INFO_ENUM_AVAILABLE,
			    package_id,
			    pkg->description ().c_str ());
		g_free (package_id);
	}
}

#endif // _ZYPP_UTILS_H_

