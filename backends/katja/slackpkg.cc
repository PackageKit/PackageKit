#include "slackpkg.hpp"

namespace Katja
{
	Slackpkg::Slackpkg(gchar *repoName, gchar *repoMirror, unsigned short repoOrder, gchar **repoPriority)
		: Binary(repoName, repoMirror, repoOrder)
	{
		for (auto cur_priority = repoPriority; *cur_priority; ++cur_priority)
		{
			priority.push_back(std::string(*cur_priority));
		}
	}

	const std::vector<std::string> Slackpkg::getPriority() const noexcept
	{
		return this->priority;
	}
}
