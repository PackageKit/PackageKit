#ifndef __KATJA_SLACKPKG_H
#define __KATJA_SLACKPKG_H

#include "binary.h"

#include <unordered_map>

G_BEGIN_DECLS

#define KATJA_TYPE_SLACKPKG katja_slackpkg_get_type()
G_DECLARE_FINAL_TYPE(KatjaSlackpkg, katja_slackpkg, KATJA, SLACKPKG, KatjaBinary)

G_END_DECLS

namespace katja
{

class Slackpkg final : public Binary
{
public:
	/**
	 * @name: repository name.
	 * @mirror: repository mirror.
	 * @order: repository order.
	 * @blacklist: repository blacklist.
	 * @priority: groups priority.
	 *
	 * Constructor.
	 **/
	explicit Slackpkg(const std::string& name,
	                  const std::string& mirror,
	                  std::uint8_t order,
	                  const gchar* blacklist,
	                  gchar** priority);
	~Slackpkg();

	GSList* collectCacheInfo(const gchar* tmpl);
	void generateCache(PkBackendJob* job, const gchar* tmpl);

private:
	gchar** priority_;

	static const std::unordered_map<std::string, std::string> categoryMap;
};

}

#endif /* __KATJA_SLACKPKG_H */
