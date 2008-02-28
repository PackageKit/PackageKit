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
#include <zypp/sat/Pool.h>
#include <zypp/target/rpm/RpmDb.h>
#include <zypp/target/rpm/RpmHeader.h>

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
                //FIXME have to wait for fix in zypp (repeated loading of target)
                if (zypp::sat::Pool::instance().reposFind( zypp::sat::Pool::systemRepoName() ) == zypp::Repo::norepo)
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
                        
                        //FIXME see above, skip already cached repos
                        if (zypp::sat::Pool::instance().reposFind( repo.alias ()) == zypp::Repo::norepo)
                                manager.loadFromCache (repo);
		}
	} catch (const zypp::repo::RepoNoAliasException &ex) {
                pk_warning ("Can't figure an alias to look in cache");
	} catch (const zypp::Exception &ex) {
                pk_warning ("TODO: Handle exceptions: %s", ex.asUserString ().c_str ());
	}

	return zypp->pool ();
}

zypp::ResPool
zypp_build_local_pool ()
{
        zypp::sat::Pool pool = zypp::sat::Pool::instance ();

        for (zypp::sat::Pool::RepoIterator it = pool.reposBegin (); it != pool.reposEnd (); it++){
                if (! pool.reposEmpty ())
                        pool.reposErase(it->name ());
        }

        //Add local resolvables
        zypp::ZYpp::Ptr zypp = get_zypp ();
        zypp->target ()->load ();

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
zypp_get_group (zypp::ResObject::constPtr item, zypp::target::rpm::RpmDb &rpm)
{
        std::string group;

        if (item->isSystem ()) {

                zypp::target::rpm::RpmHeader::constPtr rpmHeader;
                rpm.getData (item->name (), item->edition (), rpmHeader);
                group = rpmHeader->tag_group ();

        }else{
                zypp::Package::constPtr pkg = zypp::asKind<zypp::Package>(item);
                group = pkg->group ();
        }
        std::transform(group.begin(), group.end(), group.begin(), tolower);
        return (gchar*)group.c_str ();
}

PkGroupEnum
get_enum_group (zypp::ResObject::constPtr item)
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

std::vector<zypp::PoolItem> *
zypp_get_packages_by_name (const gchar *package_name, gboolean include_local)
{
	std::vector<zypp::PoolItem> *v = new std::vector<zypp::PoolItem> ();

	zypp::ResPool pool = zypp_build_pool (include_local);

	std::string name (package_name);
	for (zypp::ResPool::byIdent_iterator it = pool.byIdentBegin (zypp::ResKind::package, name);
			it != pool.byIdentEnd (zypp::ResKind::package, name); it++) {
		zypp::PoolItem item = (*it);
		v->push_back (item);
	}

	return v;
}

std::vector<zypp::PoolItem> *
zypp_get_packages_by_details (const gchar *search_term, gboolean include_local)
{
        std::vector<zypp::PoolItem> *v = new std::vector<zypp::PoolItem> ();

        zypp::ResPool pool = zypp_build_pool (include_local);

        std::string term (search_term);
        for (zypp::ResPool::byKind_iterator it = pool.byKindBegin (zypp::ResKind::package);
                        it != pool.byKindEnd (zypp::ResKind::package); it++) {
                if ((*it)->name ().find (term) != std::string::npos || (*it)->description ().find (term) != std::string::npos )
                    v->push_back (*it);
        }

        return v;
}

std::vector<zypp::PoolItem> * 
zypp_get_packages_by_file (const gchar *search_file)
{
        std::vector<zypp::PoolItem> *v = new std::vector<zypp::PoolItem> ();

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
                        v->push_back (*it);
                        break;
                }

        }

        return v;
}

zypp::Resolvable::constPtr
zypp_get_package_by_id (const gchar *package_id)
{
	PkPackageId *pi;
	pi = pk_package_id_new_from_string (package_id);
	if (pi == NULL) {
		// TODO: Do we need to do something more for this error?
		return NULL;
	}

	std::vector<zypp::PoolItem> *v = zypp_get_packages_by_name (pi->name, TRUE);
	if (v == NULL)
		return NULL;

	zypp::ResObject::constPtr package = NULL;
	for (std::vector<zypp::PoolItem>::iterator it = v->begin ();
			it != v->end (); it++) {
		zypp::ResObject::constPtr pkg = (*it);
		const char *version = pkg->edition ().asString ().c_str ();
		if (strcmp (pi->version, version) == 0) {
			package = pkg;
			break;
		}
	}

	delete (v);
	return package;
}

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

