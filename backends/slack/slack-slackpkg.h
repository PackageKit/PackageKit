#ifndef __SLACK_SLACKPKG_H
#define __SLACK_SLACKPKG_H

#include <glib-object.h>

#ifdef __cplusplus
extern "C" {
#endif

G_BEGIN_DECLS

#define SLACK_TYPE_SLACKPKG slack_slackpkg_get_type()
G_DECLARE_FINAL_TYPE(SlackSlackpkg, slack_slackpkg, SLACK, SLACKPKG, GObject)

/* Public static members */
extern GHashTable *slack_slackpkg_cat_map;

#define SLACK_BINARY_MAX_BUF_SIZE 8192

SlackSlackpkg *slack_slackpkg_new(const gchar *name,
                                  const gchar *mirror,
                                  guint8 order,
                                  const gchar *blacklist,
                                  gchar **priority);

const gchar *slack_slackpkg_get_name(SlackSlackpkg *slackpkg);
const gchar *slack_slackpkg_get_mirror(SlackSlackpkg *slackpkg);
guint8 slack_slackpkg_get_order(SlackSlackpkg *slackpkg);
gboolean slack_slackpkg_is_blacklisted(SlackSlackpkg *slackpkg,
                                       const gchar *pkg);

G_END_DECLS

#ifdef __cplusplus
}
#endif

#endif /* __SLACK_SLACKPKG_H */
