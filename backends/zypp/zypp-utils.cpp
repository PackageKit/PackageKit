#ifndef _ZYPP_UTILS_H_
#define _ZYPP_UTILS_H_

#include <stdlib.h>
#include <glib.h>
#include <zypp/RepoManager.h>
#include <zypp/media/MediaManager.h>
#include <zypp/Resolvable.h>

#include "zypp-utils.h"

gboolean
is_changeable_media (const zypp::Url &url)
{
	gboolean is_cd = false;
	try {
		zypp::media::MediaManager mm;
		zypp::media::MediaAccessId id = mm.open (url);
		is_cd = mm.isChangeable (id);
		mm.close (id);
	} catch (const zypp::media::MediaException &e) {
		// TODO: Do anything about this?
	}

	return is_cd;
}

#endif // _ZYPP_UTILS_H_

