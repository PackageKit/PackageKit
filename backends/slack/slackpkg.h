#ifndef __SLACK_SLACKPKG_H
#define __SLACK_SLACKPKG_H

#include "pkgtools.h"

/* Public static members */
extern GHashTable *slack_slackpkg_cat_map;

#define SLACK_BINARY_MAX_BUF_SIZE 8192

class SlackSlackpkg final : public SlackPkgtools
{
public:
	SlackSlackpkg (const gchar *name, const gchar *mirror,
			guint8 order, const gchar *blacklist, gchar **priority) noexcept;
	~SlackSlackpkg () noexcept;

	GSList *collect_cache_info (const gchar *tmpl) noexcept;
	void generate_cache (PkBackendJob *job, const gchar *tmpl) noexcept;

private:
	gchar **priority = NULL;

	void manifest (PkBackendJob *job,
			const gchar *tmpl, gchar *filename) noexcept;
};

#endif /* __SLACK_SLACKPKG_H */
