#include "binary.hpp"

namespace Katja
{
	Binary::Binary(gchar *repoName, gchar *repoMirror, unsigned short repoOrder)
	{
		this->name = repoName;
		this->mirror = repoMirror;
		this->order  = repoOrder;
	}

	const std::string Binary::getName() const noexcept
	{
		return name;
	}

	const std::string Binary::getMirror() const noexcept
	{
		return mirror;
	}

	const unsigned short Binary::getOrder() const noexcept
	{
		return order;
	}

	bool Binary::download(PkBackendJob *job, std::string dest, std::string pkg)
	{
		return true;
	}
}
