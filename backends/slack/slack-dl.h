#ifndef __SLACK_DL_H
#define __SLACK_DL_H

#include <glib-object.h>

G_BEGIN_DECLS

#define SLACK_TYPE_DL slack_dl_get_type()
G_DECLARE_FINAL_TYPE(SlackDl, slack_dl, SLACK, DL, GObject)

SlackDl *slack_dl_new(const gchar *name,
                      const gchar *mirror,
                      guint8 order,
                      const gchar *blacklist,
                      gchar *index_file);

const gchar *slack_dl_get_name(SlackDl *dl);
const gchar *slack_dl_get_mirror(SlackDl *dl);
guint8 slack_dl_get_order(SlackDl *dl);
gboolean slack_dl_is_blacklisted(SlackDl *dl, const gchar *pkg);

G_END_DECLS

#endif /* __SLACK_DL_H */
