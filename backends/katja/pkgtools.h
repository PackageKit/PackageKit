#ifndef __KATJA_PKGTOOLS_H
#define __KATJA_PKGTOOLS_H

#include <glib-object.h>
#include <pk-backend.h>
#include <string>

G_BEGIN_DECLS

#define KATJA_TYPE_PKGTOOLS katja_pkgtools_get_type()
G_DECLARE_INTERFACE(KatjaPkgtools, katja_pkgtools, KATJA, PKGTOOLS, GObject)

struct _KatjaPkgtoolsInterface
{
	GTypeInterface parent_iface;

	GSList *(*collect_cache_info) (KatjaPkgtools *pkgtools, const gchar *tmpl);
	void (*generate_cache) (KatjaPkgtools *pkgtools, PkBackendJob *job, const gchar *tmpl);
	gboolean (*download) (KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *dest_dir_name, gchar *pkg_name);
	void (*install) (KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *pkg_name);
};

GSList *katja_pkgtools_collect_cache_info(KatjaPkgtools *pkgtools,
                                          const gchar   *tmpl);

void katja_pkgtools_generate_cache(KatjaPkgtools *pkgtools,
                                   PkBackendJob  *job,
                                   const gchar   *tmpl);

gboolean katja_pkgtools_download(KatjaPkgtools *pkgtools,
                                 PkBackendJob  *job,
                                 gchar         *dest_dir_name,
                                 gchar         *pkg_name);

void katja_pkgtools_install(KatjaPkgtools *pkgtools,
                            PkBackendJob  *job,
                            gchar         *pkg_name);

G_END_DECLS

namespace katja
{

class Pkgtools
{
public:
	virtual ~Pkgtools() noexcept;

	virtual KatjaPkgtools* data() const noexcept = 0;
	const std::string& name() const noexcept;

	bool operator==(const gchar* name) const noexcept;
	bool operator!=(const gchar* name) const noexcept;

protected:
	std::string name_;
};

}

#endif /* __KATJA_PKGTOOLS_H */
