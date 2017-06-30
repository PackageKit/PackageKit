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

G_DEFINE_INTERFACE(KatjaPkgtools, katja_pkgtools, G_TYPE_OBJECT)

static void
katja_pkgtools_default_init(KatjaPkgtoolsInterface *iface)
{
	GParamSpec *spec;

	spec = g_param_spec_string("name",
	                           "Name",
	                           "Repository name",
	                           NULL,
	                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_interface_install_property(iface, spec);

	spec = g_param_spec_string("mirror",
	                           "Mirror",
	                           "Repository mirror",
	                           NULL,
	                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_interface_install_property(iface, spec);

	spec = g_param_spec_uint("order",
	                         "Order",
	                         "Repository order",
	                         0,
	                         G_MAXUSHORT,
	                         0,
	                         static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_interface_install_property(iface, spec);

	spec = g_param_spec_boxed("blacklist",
	                          "Blacklist",
	                          "Repository blacklist",
	                          G_TYPE_REGEX,
	                          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_interface_install_property(iface, spec);
}
