#ifndef __KATJA_PKGTOOLS_H
#define __KATJA_PKGTOOLS_H

#include <glib-object.h>
#include <pk-backend.h>
#include <string>

namespace katja
{

class Pkgtools
{
public:
	virtual ~Pkgtools() noexcept;

	const std::string& name() const noexcept;
	const std::string& mirror() const noexcept;
	std::uint8_t order() const noexcept;
	const GRegex* blacklist() const noexcept;

	/**
	 * @tmpl: temporary directory for downloading the files.
	 *
	 * Download files needed to get the information like the list of packages
	 * in available repositories, updates, package descriptions and so on.
	 *
	 * Returns: list of files needed for building the cache.
	 **/
	virtual GSList* collectCacheInfo(const gchar* tmpl) = 0;

	/**
	 * @tmpl: temporary directory for downloading the files.
	 *
	 * Download files needed to get the information like the list of packages
	 * in available repositories, updates, package descriptions and so on.
	 *
	 * Returns: list of files needed for building the cache.
	 **/
	virtual void generateCache(PkBackendJob* job, const gchar* tmpl) = 0;

	/**
	 * @job:           a #PkBackendJob.
	 * @dest_dir_name: destination directory.
	 * @pkg_name:      package name.
	 *
	 * Download a package.
	 *
	 * Returns: TRUE on success, FALSE otherwise.
	 **/
	virtual bool download(PkBackendJob* job,
	                      gchar* dest_dir_name,
	                      gchar* pkg_name) = 0;

	/**
	 * @job:      a #PkBackendJob.
	 * @pkg_name: package name.
	 *
	 * Install a package.
	 **/
	virtual void install(PkBackendJob* job, gchar* pkg_name) = 0;

	bool operator==(const gchar* name) const noexcept;
	bool operator!=(const gchar* name) const noexcept;

protected:
	std::string name_;
	std::string mirror_;
	std::uint8_t order_;
	GRegex* blacklist_;
};

}

#endif /* __KATJA_PKGTOOLS_H */
