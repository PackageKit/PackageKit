// RPM plugin to notify PackageKit that the system changed
// Copyright (C) 2026 Gordon Messmer <gordon.messmer@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Based on https://github.com/rpm-software-management/rpm/blob/master/plugins/dbus_announce.c
// Copyright (C) 2021 by Red Hat, Inc.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// and backends/dnf/notify_packagekit.cpp
// Copyright (C) 2024 Alessandro Astone <ales.astone@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <rpm/rpmlog.h>
#include <rpm/rpmstring.h>
#include <rpm/rpmts.h>
#include <rpm/rpmplugin.h>

#include <sdbus-c++/sdbus-c++.h>

#include <cstdlib>
#include <memory>

using namespace std::literals;

namespace {

constexpr const char * PLUGIN_NAME = "notify_packagekit";

struct NotifyPackagekitData {
    std::unique_ptr<sdbus::IConnection> connection{nullptr};
    std::unique_ptr<sdbus::IProxy> proxy{nullptr};

    void close_bus() noexcept {
        proxy.reset();
        connection.reset();
    }

    rpmRC open_dbus(rpmPlugin plugin, rpmts ts) noexcept {
        // Already open
        if (connection) {
            return RPMRC_OK;
        }

        // ...don't notify on test transactions
        if (rpmtsFlags(ts) & (RPMTRANS_FLAG_TEST | RPMTRANS_FLAG_BUILD_PROBS)) {
            return RPMRC_OK;
        }

        // ...don't notify on chroot transactions
        if (!rstreq(rpmtsRootDir(ts), "/")) {
            return RPMRC_OK;
        }

        try {
#if SDBUSCPP_VERSION_MAJOR >= 2
            auto serviceName = sdbus::ServiceName{"org.freedesktop.PackageKit"};
            auto objectPath = sdbus::ObjectPath{"/org/freedesktop/PackageKit"};
#else
            auto serviceName = "org.freedesktop.PackageKit"s;
            auto objectPath = "/org/freedesktop/PackageKit"s;
#endif

            connection = sdbus::createSystemBusConnection();
            proxy = sdbus::createProxy(
                std::move(connection),
                std::move(serviceName),
                std::move(objectPath),
                sdbus::dont_run_event_loop_thread);
        } catch (const sdbus::Error & e) {
            rpmlog(RPMLOG_DEBUG,
                   "%s plugin: Error connecting to dbus (%s)\n",
                   PLUGIN_NAME, e.what());
            connection.reset();
            proxy.reset();
        }

        return RPMRC_OK;
    }

    rpmRC send_state_changed() noexcept {
        if (!proxy) {
            return RPMRC_OK;
        }

        try {
#if SDBUSCPP_VERSION_MAJOR >= 2
            auto interfaceName = sdbus::InterfaceName{"org.freedesktop.PackageKit"};
            auto methodName = sdbus::MethodName{"StateHasChanged"};
#else
            auto interfaceName = "org.freedesktop.PackageKit"s;
            auto methodName = "StateHasChanged"s;
#endif

            auto method = proxy->createMethodCall(std::move(interfaceName), std::move(methodName));
            method << "posttrans";

#if SDBUSCPP_VERSION_MAJOR >= 2
            proxy->callMethodAsync(method, sdbus::with_future);
#else
            proxy->callMethod(method, sdbus::with_future);
#endif
        } catch (const sdbus::Error & e) {
            rpmlog(RPMLOG_WARNING,
                   "%s plugin: Error sending message (%s)\n",
                   PLUGIN_NAME, e.what());
        }

        return RPMRC_OK;
    }
};

}  // namespace

// C linkage for RPM plugin interface
extern "C" {

static rpmRC notify_packagekit_init(rpmPlugin plugin, rpmts ts) {
    auto * state = new (std::nothrow) NotifyPackagekitData();
    if (state == nullptr) {
        return RPMRC_FAIL;
    }
    rpmPluginSetData(plugin, state);
    return RPMRC_OK;
}

static void notify_packagekit_cleanup(rpmPlugin plugin) {
    auto * state = static_cast<NotifyPackagekitData *>(rpmPluginGetData(plugin));
    delete state;
}

static rpmRC notify_packagekit_tsm_pre(rpmPlugin plugin, rpmts ts) {
    auto * state = static_cast<NotifyPackagekitData *>(rpmPluginGetData(plugin));
    return state->open_dbus(plugin, ts);
}

static rpmRC notify_packagekit_tsm_post(rpmPlugin plugin, rpmts ts, int res) {
    auto * state = static_cast<NotifyPackagekitData *>(rpmPluginGetData(plugin));
    return state->send_state_changed();
}

struct rpmPluginHooks_s notify_packagekit_hooks = {
    .init = notify_packagekit_init,
    .cleanup = notify_packagekit_cleanup,
    .tsm_pre = notify_packagekit_tsm_pre,
    .tsm_post = notify_packagekit_tsm_post,
    .psm_pre = NULL,
    .psm_post = NULL,
    .scriptlet_pre = NULL,
    .scriptlet_fork_post = NULL,
    .scriptlet_post = NULL,
    .fsm_file_pre = NULL,
    .fsm_file_post = NULL,
    .fsm_file_prepare = NULL,
};

}  // extern "C"
