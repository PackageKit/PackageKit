#ifndef __KATJA_DL_H
#define __KATJA_DL_H

#include "binary.h"

namespace katja
{

class Dl final : public Binary
{
public:
	/**
	 * @name: repository name.
	 * @mirror: repository mirror.
	 * @order: repository order.
	 * @blacklist: repository blacklist.
	 * @index_file: the index file URL.
	 *
	 * Constructor.
	 **/
	explicit Dl(const std::string& name,
	            const std::string& mirror,
	            std::uint8_t order,
	            const gchar* blacklist,
	            const std::string& indexFile);
	~Dl();

	GSList* collectCacheInfo(const gchar* tmpl);
	void generateCache(PkBackendJob* job, const gchar* tmpl);

private:
	std::string indexFile_;
};

}

#endif /* __KATJA_DL_H */
