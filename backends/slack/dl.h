#ifndef __SLACK_DL_H
#define __SLACK_DL_H

#include <glib-object.h>
#include "pkgtools.h"

G_BEGIN_DECLS

#define SLACK_TYPE_DL slack_dl_get_type()
G_DECLARE_FINAL_TYPE(SlackDl, slack_dl, SLACK, DL, GObject)

SlackDl *slack_dl_new(const gchar *name,
                      const gchar *mirror,
                      guint8 order,
                      const gchar *blacklist,
                      gchar *index_file);

class _SlackDl final : public SlackPkgtools
{
	GObject parent;
};

G_END_DECLS

#endif /* __SLACK_DL_H */
