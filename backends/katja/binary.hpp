#ifndef __KATJA_BINARY_HPP
#define __KATJA_BINARY_HPP

#include <glib.h>
#include <pk-backend.h>
#include "pkgtools.hpp"

namespace Katja
{
	class Binary : public Pkgtools
	{
	public:
		Binary(gchar *repoName, gchar *repoMirror, unsigned short repoOrder);
		const std::string getName() const noexcept override;
		const std::string getMirror() const noexcept override;
		const unsigned short getOrder() const noexcept override;
		bool download(PkBackendJob *job, std::string dest, std::string pkg) override;

	protected:
		std::string name;
		std::string mirror;
		unsigned short order;
	};
}

#endif /* __KATJA_BINARY_HPP */
