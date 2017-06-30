#ifndef __KATJA_BINARY_H
#define __KATJA_BINARY_H

#include <sqlite3.h>
#include "pkgtools.h"
#include "utils.h"

G_BEGIN_DECLS

#define KATJA_TYPE_BINARY katja_binary_get_type()
G_DECLARE_DERIVABLE_TYPE(KatjaBinary, katja_binary, KATJA, BINARY, GObject)

typedef struct
{
	GObjectClass parent_class;

	sqlite3 *db;
	CURL *curl;
} PkBackendKatjaJobData;

struct _KatjaBinaryClass
{
	GObjectClass parent_class;
};

G_END_DECLS

namespace katja
{

class Binary : public Pkgtools
{
public:
	static const std::size_t maxBufSize;

	virtual bool download(PkBackendJob* job,
	                      gchar* dest_dir_name,
	                      gchar* pkg_name);
	virtual void install(PkBackendJob* job, gchar* pkg_name);

	/**
	 * @job:      a #PkBackendJob.
	 * @tmpl:     temporary directory.
	 * @filename: manifest filename
	 *
	 * Parse the manifest file and save the file list in the database.
	 **/
	void manifest(PkBackendJob* job,
	              const gchar* tmpl,
	              gchar* filename);
};

}

#endif /* __KATJA_BINARY_H */
