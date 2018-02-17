#ifndef __SLACK_SLACKPKG_H
#define __SLACK_SLACKPKG_H

#include <cstddef>
#include "pkgtools.h"

namespace slack {

class Slackpkg final : public Pkgtools
{
public:
	Slackpkg (const gchar *name, const gchar *mirror,
			guint8 order, const gchar *blacklist, gchar **priority) noexcept;
	~Slackpkg () noexcept;

	GSList *collect_cache_info (const gchar *tmpl) noexcept;
	void generate_cache (PkBackendJob *job, const gchar *tmpl) noexcept;

private:
	static GHashTable *cat_map;
	static const std::size_t max_buf_size = 8192;
	gchar **priority = NULL;

	void manifest (PkBackendJob *job,
			const gchar *tmpl, gchar *filename) noexcept;
};

}

#endif /* __SLACK_SLACKPKG_H */
