#ifndef _ZYPP_UTILS_H_
#define _ZYPP_UTILS_H_

#include <stdlib.h>
#include <glib.h>
#include <zypp/RepoManager.h>
#include <zypp/media/MediaManager.h>
#include <zypp/Resolvable.h>
#include <zypp/ResPool.h>
#include <zypp/ResFilters.h>

#include <list>
#include <set>

// some typedefs and functions to shorten Zypp names
typedef zypp::ResPoolProxy ZyppPool;
//inline ZyppPool zyppPool() { return zypp::getZYpp()->poolProxy(); }
typedef zypp::ui::Selectable::Ptr ZyppSelectable;
typedef zypp::ui::Selectable*		ZyppSelectablePtr;
typedef zypp::ResObject::constPtr	ZyppObject;
typedef zypp::Package::constPtr		ZyppPackage;
//typedef zypp::Patch::constPtr		ZyppPatch;
//typedef zypp::Pattern::constPtr		ZyppPattern;
//inline ZyppPackage tryCastToZyppPkg (ZyppObject obj)
//	{ return zypp::dynamic_pointer_cast <const zypp::Package> (obj); }

typedef enum {
        INSTALL,
        REMOVE,
        UPDATE
} PerformType;

zypp::ZYpp::Ptr get_zypp ();

gboolean zypp_is_changeable_media (const zypp::Url &url);

/**
 * Build and return a ResPool that contains all local resolvables
 * and ones found in the enabled repositories.
 */
zypp::ResPool zypp_build_pool (gboolean include_local);

/**
 * Build and return a ResPool that contains only the local resolvables.
 */
zypp::ResPool zypp_build_local_pool ();

/**
  * Return the rpm-Database
  */
zypp::target::rpm::RpmDb& zypp_get_rpmDb();

/**
  * Return the gchar of the given PoolItem.
  */
gchar* zypp_get_group (zypp::sat::Solvable item, zypp::target::rpm::RpmDb &rpm);

/**
  * Return the PkEnumGroup of the given PoolItem.
  */
PkGroupEnum get_enum_group (zypp::sat::Solvable item);

/**
 * Returns a list of packages that match the specified package_name.
 */
std::vector<zypp::sat::Solvable> * zypp_get_packages_by_name (const gchar *package_name, gboolean include_local);

/**
 * Returns a list of packages that match the specified term in its name or description.
 */
std::vector<zypp::sat::Solvable> * zypp_get_packages_by_details (const gchar *search_term, gboolean include_local);

/**
 * Returns a list of packages that owns the specified file.
 */
std::vector<zypp::sat::Solvable> * zypp_get_packages_by_file (const gchar *search_file);

/**
 * Returns the Resolvable for the specified package_id.
 */
zypp::sat::Solvable zypp_get_package_by_id (const gchar *package_id);

/**
 * Build a package_id from the specified resolvable.  The returned
 * gchar * should be freed with g_free ().
 */
gchar * zypp_build_package_id_from_resolvable (zypp::sat::Solvable resolvable);

/**
  * Ask the User if it is OK to import an GPG-Key for a repo
  */
gboolean zypp_signature_required (PkBackend *backend, const zypp::PublicKey &key);

/**
  * Ask the User if it is OK to refresh the Repo while we don't know the key
  */
gboolean zypp_signature_required (PkBackend *backend, const std::string &file);

/**
  * Ask the User if it is OK to refresh the Repo while we don't know the key, only its id which was never seen before
  */
gboolean zypp_signature_required (PkBackend *backend, const std::string &file, const std::string &id);

/**
  * Find best (according to edition) uninstalled item with the same kind/name/arch as item.
  */
zypp::PoolItem zypp_find_arch_update_item (const zypp::ResPool & pool, zypp::PoolItem item);

/**
  * Returns a set of all packages the could be updated
  */
std::set<zypp::PoolItem> * zypp_get_updates ();

/**
  * Returns a set of all patches the could be installed
  */
std::set<zypp::ui::Selectable::Ptr> * zypp_get_patches ();

/**
  * perform changes in pool to the system
  */
gboolean zypp_perform_execution (PkBackend *backend, PerformType type);

void zypp_emit_packages_in_list (PkBackend *backend, std::vector<zypp::sat::Solvable> *v);
#endif // _ZYPP_UTILS_H_

