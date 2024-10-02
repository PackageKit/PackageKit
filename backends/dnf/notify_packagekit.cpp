// Copyright (C) 2024 Alessandro Astone <ales.astone@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <libdnf5/base/base.hpp>
#include <libdnf5/common/exception.hpp>
#include <libdnf5/plugin/iplugin.hpp>

#include <sdbus-c++/sdbus-c++.h>

#include <algorithm>
#include <cstdlib>

#include <packagekit-glib2/pk-version.h>

using namespace libdnf5;

namespace {

constexpr const char * PLUGIN_NAME{"notify_packagekit"};

constexpr plugin::Version PLUGIN_VERSION{.major = PK_MAJOR_VERSION, .minor = PK_MINOR_VERSION, .micro = PK_MICRO_VERSION};

constexpr const char * attrs[]{"author.name", "author.email", "description", nullptr};
constexpr const char * attrs_value[]{"Alessandro Astone", "ales.astone@gmail.com",
        "Notify packagekitd when packages are installed, updated, or removed."};

class NotifyPackagekitPlugin : public plugin::IPlugin {
public:
#if LIBDNF5_VERSION_MAJOR >= 5 && LIBDNF5_VERSION_MINOR >= 2
    NotifyPackagekitPlugin(libdnf5::plugin::IPluginData & data, libdnf5::ConfigParser &) : IPlugin(data) {}
#else
    NotifyPackagekitPlugin(libdnf5::Base & base, libdnf5::ConfigParser &) : IPlugin(base) {}
#endif

    const char * get_name() const noexcept override { return PLUGIN_NAME; }

    plugin::Version get_version() const noexcept override { return PLUGIN_VERSION; }

    PluginAPIVersion get_api_version() const noexcept override { return PLUGIN_API_VERSION; }

    /// Add custom attributes, such as information about yourself and a description of the plugin.
    /// These can be used to query plugin-specific data through the API.
    /// Optional to override.
    const char * const * get_attributes() const noexcept override { return attrs; }
    const char * get_attribute(const char * attribute) const noexcept override {
        for (size_t i = 0; attrs[i]; ++i) {
            if (std::strcmp(attribute, attrs[i]) == 0) {
                return attrs_value[i];
            }
        }
        return nullptr;
    }

    void post_transaction(const libdnf5::base::Transaction & transaction) override;
};

void NotifyPackagekitPlugin::post_transaction(const libdnf5::base::Transaction & transaction) {
    auto packagekitProxy = sdbus::createProxy("org.freedesktop.PackageKit", "/org/freedesktop/PackageKit");
    auto method = packagekitProxy->createMethodCall("org.freedesktop.PackageKit", "StateHasChanged");
    method << "posttrans";
    packagekitProxy->callMethod(method);
}

}  // namespace

/// Below is a block of functions with C linkage used for loading the plugin binaries from disk.
/// All of these are MANDATORY to implement.

/// Return plugin's API version.
PluginAPIVersion libdnf_plugin_get_api_version(void) {
    return PLUGIN_API_VERSION;
}

/// Return plugin's name.
const char * libdnf_plugin_get_name(void) {
    return PLUGIN_NAME;
}

/// Return plugin's version.
plugin::Version libdnf_plugin_get_version(void) {
    return PLUGIN_VERSION;
}

/// Return the instance of the implemented plugin.
plugin::IPlugin * libdnf_plugin_new_instance(
    [[maybe_unused]] LibraryVersion library_version,
#if LIBDNF5_VERSION_MAJOR >= 5 && LIBDNF5_VERSION_MINOR >= 2
    libdnf5::plugin::IPluginData & base,
#else
    libdnf5::Base & base,
#endif
    libdnf5::ConfigParser & parser) try {
    return new NotifyPackagekitPlugin(base, parser);
} catch (...) {
    return nullptr;
}

/// Delete the plugin instance.
void libdnf_plugin_delete_instance(plugin::IPlugin * plugin_object) {
    delete plugin_object;
}
