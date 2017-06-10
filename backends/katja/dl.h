#ifndef __KATJA_DL_H
#define __KATJA_DL_H

#include "binary.h"

G_BEGIN_DECLS

#define KATJA_TYPE_DL katja_dl_get_type()
G_DECLARE_FINAL_TYPE(KatjaDl, katja_dl, KATJA, DL, KatjaBinary)

struct _KatjaDlClass
{
	KatjaBinaryClass parent_class;
};

/* Constructors */
KatjaDl *katja_dl_new(gchar *name,
                      gchar *mirror,
                      gushort order,
                      gchar *blacklist,
                      gchar *index_file);

/* Implementations */
GSList *katja_dl_real_collect_cache_info(KatjaPkgtools *pkgtools,
                                         const gchar   *tmpl);

void katja_dl_real_generate_cache(KatjaPkgtools *pkgtools,
                                  PkBackendJob  *job,
                                  const gchar   *tmpl);

G_END_DECLS

namespace katja
{

class Dl final : public Binary
{
public:
	explicit Dl(KatjaDl* dl) noexcept;

	KatjaPkgtools* data() const noexcept;

private:
	KatjaDl* gObj_;
};

}

#endif /* __KATJA_DL_H */
