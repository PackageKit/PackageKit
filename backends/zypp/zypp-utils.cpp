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
zypp_get_packages_by_name (const gchar *package_name)
{
	std::vector<zypp::PoolItem> *v = new std::vector<zypp::PoolItem> ();

	zypp::ResPool pool = zypp_build_pool (FALSE);

	std::string name (package_name);
	for (zypp::ResPool::byName_iterator it = pool.byNameBegin (name);
			it != pool.byNameEnd (name); it++) {
		zypp::PoolItem item = (*it);
		v->push_back (item);
	}

	return v;
}

#endif // _ZYPP_UTILS_H_

