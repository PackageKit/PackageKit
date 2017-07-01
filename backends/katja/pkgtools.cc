#include "pkgtools.h"

namespace katja
{

Pkgtools::~Pkgtools() noexcept
{
}

const std::string&
Pkgtools::name() const noexcept
{
	return name_;
}

const std::string&
Pkgtools::mirror() const noexcept
{
	return mirror_;
}

std::uint8_t
Pkgtools::order() const noexcept
{
	return order_;
}

const GRegex*
Pkgtools::blacklist() const noexcept
{
	return blacklist_;
}

bool
Pkgtools::operator==(const gchar* name) const noexcept
{
	return name_.compare(name) == 0;
}

bool
Pkgtools::operator!=(const gchar* name) const noexcept
{
	return name_.compare(name) != 0;
}

}
