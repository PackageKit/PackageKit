#include "katja-pkgtools.h"

G_DEFINE_INTERFACE(KatjaPkgtools, katja_pkgtools, G_TYPE_OBJECT)

static void
katja_pkgtools_default_init(KatjaPkgtoolsInterface *iface)
{
	GParamSpec *spec;

	spec = g_param_spec_string("name",
	                           "Name",
	                           "Repository name",
	                           NULL,
	                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
	g_object_interface_install_property(iface, spec);

	spec = g_param_spec_string("mirror",
	                           "Mirror",
	                           "Repository mirror",
	                           NULL,
	                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
	g_object_interface_install_property(iface, spec);

	spec = g_param_spec_uint("order",
	                         "Order",
	                         "Repository order",
	                         0,
	                         G_MAXUSHORT,
	                         0,
	                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
	g_object_interface_install_property(iface, spec);

	spec = g_param_spec_boxed("blacklist",
	                          "Blacklist",
	                          "Repository blacklist",
	                          G_TYPE_REGEX,
	                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_interface_install_property(iface, spec);
}
