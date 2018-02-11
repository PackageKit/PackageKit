#ifndef __SLACK_DL_H
#define __SLACK_DL_H

#include "pkgtools.h"

class SlackDl final : public SlackPkgtools
{
public:
	SlackDl (const gchar *name, const gchar *mirror,
		guint8 order, const gchar *blacklist, gchar *index_file) noexcept;
	~SlackDl () noexcept;

	GSList *collect_cache_info (const gchar *tmpl) noexcept;
	void generate_cache (PkBackendJob *job, const gchar *tmpl) noexcept;

private:
	gchar *index_file;
};

#endif /* __SLACK_DL_H */
