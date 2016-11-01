#ifndef __KATJA_SLACKPKG_HPP
#define __KATJA_SLACKPKG_HPP

#include  <vector>
#include "binary.hpp"

namespace Katja
{
	class Slackpkg : public Binary
	{
	public:
		Slackpkg(gchar *repoName, gchar *repoMirror, unsigned short repoOrder, gchar **repoPriority);
		const std::vector<std::string> getPriority() const noexcept;

	private:
		std::vector<std::string> priority;
	};
}

#endif /* __KATJA_SLACKPKG_HPP */
