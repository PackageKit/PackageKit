#ifndef __KATJA_PKGTOOLS_HPP
#define __KATJA_PKGTOOLS_HPP

#include <string>

namespace Katja
{
	class Pkgtools
	{
	public:
		virtual const std::string getName() const noexcept = 0;
		virtual const std::string getMirror() const noexcept = 0;
		virtual const unsigned short getOrder() const noexcept = 0;
		virtual bool download(PkBackendJob *job, std::string dest, std::string pkg) = 0;
	};
}

#endif /* __KATJA_PKGTOOLS_HPP */
