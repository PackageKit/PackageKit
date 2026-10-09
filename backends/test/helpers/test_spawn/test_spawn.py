#!/usr/bin/env python3
#
# Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Spawned test backend for the daemon test suite. It emits every event the
# protocol knows, and misbehaves on request: install-packages waits for a
# cancel, remove-packages ignores cancel and SIGTERM, a forced refresh-cache
# exits mid-job, and resolve raises.

import os
import signal
import time

from packagekit_backend import *  # noqa: F403

PACKAGES = {
    'foo': ('foo;1.0;x86_64;main;', INFO_INSTALLED, 'A foo'),
    'bar': ('bar;2.1;x86_64;main;', INFO_AVAILABLE, 'A bar'),
    'baz': ('baz;0.3;noarch;extra;', INFO_AVAILABLE, 'A baz'),
}


class TestSpawnBackend(Backend):
    name = 'test_spawn'
    description = 'Test spawn backend'
    author = 'PackageKit developers'
    filters = (FILTER_INSTALLED, FILTER_DEVELOPMENT)
    groups = (GROUP_SYSTEM, GROUP_OTHER)
    mime_types = ('application/x-test',)
    batch_size = 2

    def search_name(self, filters, values):
        self.status(STATUS_QUERY)
        self.percentage(None)
        self.log(
            'info', 'searching %r with filters %r for uid %d' % (values, filters, self.ctx.uid)
        )
        for name, (package_id, info, summary) in PACKAGES.items():
            if any(value in name for value in values):
                self.package(package_id, info, summary)
        self.percentage(100)

    def get_details(self, package_ids):
        for package_id in package_ids:
            self.details(
                package_id,
                summary='A package',
                license='MIT',
                group=GROUP_SYSTEM,
                description='Long\ndescription',
                url='https://example.org',
                size=1234,
            )

    def get_update_detail(self, package_ids):
        for package_id in package_ids:
            self.update_detail(
                package_id,
                updates=['foo;0.9;x86_64;installed;'],
                cve_urls=['https://www.cve.org/CVERecord?id=CVE-2026-0001'],
                restart=RESTART_SESSION,
                update_text='Fixes things.',
                state=UPDATE_STATE_STABLE,
                issued='2026-01-01T00:00:00Z',
            )

    def get_files(self, package_ids):
        for package_id in package_ids:
            self.files(package_id, ['/usr/bin/foo', '/usr/share/doc/foo'])

    def get_repo_list(self, filters):
        self.repo_detail('main', 'Main repository', True)
        self.repo_detail('extra', 'Extra repository', False)

    def get_distro_upgrades(self):
        self.distro_upgrade(DISTRO_UPGRADE_STABLE, 'test-2', 'Test OS 2')

    def install_packages(self, flags, package_ids):
        self.allow_cancel(True)
        self.status(STATUS_DOWNLOAD)
        for package_id in package_ids:
            self.package_status(package_id, INFO_DOWNLOADING)
            self.item_progress(package_id, STATUS_DOWNLOAD, 50)
            self.speed(1024)
            self.download_size_remaining(4096)
        if TRANSACTION_FLAG_SIMULATE in flags:
            self.require_restart(RESTART_SESSION, package_ids[0])
            return
        # a real install only ends through an in-band cancel
        self.status(STATUS_INSTALL)
        while True:
            self.check_cancelled()
            time.sleep(0.05)

    def remove_packages(self, flags, package_ids, allow_deps, autoremove):
        # ignores cancel and SIGTERM: only SIGKILL ends this job
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        time.sleep(60)

    def refresh_cache(self, force):
        if force:
            os._exit(3)  # pretend to crash mid-job

    def repo_enable(self, repo_id, enabled):
        self.eula_required('eula-1', 'foo;1.0;x86_64;main;', 'Vendor', 'Agree to everything.')

    def repo_set_data(self, repo_id, parameter, value):
        self.repo_signature_required(
            'foo;1.0;x86_64;main;',
            repo_id,
            'https://example.org/key',
            'Key <key@example.org>',
            'ABCDEF',
            'fingerprint',
            '2026-01-01T00:00:00Z',
        )

    def install_signature(self, sig_type, key_id, package_id):
        raise PkError(ERROR_GPG_FAILURE, 'bad key %s' % key_id)

    def resolve(self, filters, values):
        raise ValueError('resolve always fails here')


if __name__ == '__main__':
    TestSpawnBackend().main()
