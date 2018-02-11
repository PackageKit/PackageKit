#ifndef __SLACK_PKGTOOLS_H
#define __SLACK_PKGTOOLS_H

#include <glib-object.h>
#include <pk-backend.h>

class SlackPkgtools
{
public:
	const gchar *get_name () noexcept;
	const gchar *get_mirror () noexcept;
	guint8 get_order () noexcept;
	gboolean is_blacklisted (const gchar *pkg) noexcept;

	virtual ~SlackPkgtools () noexcept;

	gboolean download (PkBackendJob *job,
			gchar *dest_dir_name, gchar *pkg_name) noexcept;
	void install (PkBackendJob *job, gchar *pkg_name) noexcept;

	virtual GSList *collect_cache_info (const gchar *tmpl) noexcept = 0;
	virtual void generate_cache (PkBackendJob *job,
			const gchar *tmpl) noexcept = 0;

protected:
	gchar *name = NULL;
	gchar *mirror = NULL;
	guint8 order;
	GRegex *blacklist = NULL;
};

#endif /* __SLACK_PKGTOOLS_H */
