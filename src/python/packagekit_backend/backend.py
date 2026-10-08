# Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Base class and request dispatcher for spawned backends."""

import os
import signal
import sys
import traceback
from types import SimpleNamespace

from . import enums
from .protocol import Connection, ProtocolError

PROTOCOL_VERSION = 1

# The arguments of each role, in the order they are passed to the backend
# method implementing it (the method is the role name with '-' as '_').
ROLE_ARGS = {
    'search-name': ('filters', 'values'),
    'search-details': ('filters', 'values'),
    'search-group': ('filters', 'values'),
    'search-file': ('filters', 'values'),
    'what-provides': ('filters', 'values'),
    'resolve': ('filters', 'values'),
    'get-packages': ('filters',),
    'get-updates': ('filters',),
    'get-repo-list': ('filters',),
    'depends-on': ('filters', 'package_ids', 'recursive'),
    'required-by': ('filters', 'package_ids', 'recursive'),
    'get-details': ('package_ids',),
    'get-files': ('package_ids',),
    'get-update-detail': ('package_ids',),
    'get-details-local': ('files',),
    'get-files-local': ('files',),
    'get-distro-upgrades': (),
    'download-packages': ('package_ids', 'directory'),
    'install-packages': ('flags', 'package_ids'),
    'update-packages': ('flags', 'package_ids'),
    'install-files': ('flags', 'files'),
    'remove-packages': ('flags', 'package_ids', 'allow_deps', 'autoremove'),
    'install-signature': ('sig_type', 'key_id', 'package_id'),
    'refresh-cache': ('force',),
    'repo-enable': ('repo_id', 'enabled'),
    'repo-set-data': ('repo_id', 'parameter', 'value'),
    'repo-remove': ('flags', 'repo_id', 'autoremove'),
    'upgrade-system': ('flags', 'distro_id', 'upgrade_kind'),
    'repair-system': ('flags',),
}

_CTX_DEFAULTS = {
    'locale': None,
    'uid': 0,
    'network': False,
    'background': False,
    'interactive': False,
    'cache_age': None,
    'details_with_deps_size': False,
    'frontend_socket': None,
    'proxy': {},
    'accepted_eulas': [],
}

# ctx.proxy key -> (environment variable, URI scheme added when none is given)
_PROXY_ENV = {
    'http': ('http_proxy', 'http://'),
    'https': ('https_proxy', 'http://'),
    'ftp': ('ftp_proxy', 'http://'),
    'socks': ('all_proxy', 'socks://'),
    'no_proxy': ('no_proxy', None),
}


class PkError(Exception):
    """Raise from a role method to fail the job with this error code."""

    def __init__(self, code, details=''):
        super().__init__(code, details)
        self.code = code
        self.details = details

    def __str__(self):
        return '%s: %s' % (self.code, self.details)


class CancelledError(PkError):
    """The daemon asked to cancel the running job."""

    def __init__(self, details='Transaction was cancelled'):
        super().__init__(enums.ERROR_TRANSACTION_CANCELLED, details)


def get_package_id(name, version, arch, origin, data=''):
    """Join the five fields into a package id."""
    return ';'.join((name, version, arch, origin, data))


def split_package_id(package_id):
    """Split a package id into its name, version, arch, origin and data fields."""
    return package_id.split(';', 4)


class Backend:
    """Base class for a spawned backend.

    Class attributes describe the backend to the daemon. The roles it supports
    are discovered from the methods the subclass defines, e.g. ``search_name``
    for the ``search-name`` role; see ROLE_ARGS for the arguments each takes.
    While a role method runs, ``self.ctx`` holds the transaction context.
    Results are emitted with the methods below; ``package``, ``details``,
    ``update_detail``, ``repo_detail`` and ``distro_upgrade`` are collected and
    sent in batches. A role method ends the job by returning, raising PkError
    for a specific error code, or raising anything else for an internal error.
    """

    name = 'unknown'
    description = ''
    author = ''
    filters = ()
    groups = ()
    mime_types = ()
    # items of one kind queued before they are sent as one event
    batch_size = 500

    def __init__(self, connection=None):
        self._conn = connection or Connection()
        self._job = None
        self._cancelled = False
        self._batches = {}
        self.ctx = SimpleNamespace(**_CTX_DEFAULTS)

    #
    # dispatcher
    #

    def roles(self):
        """The roles this backend implements."""
        return [role for role in ROLE_ARGS if hasattr(self, role.replace('-', '_'))]

    def main(self):
        """Serve requests from the daemon until it says exit or hangs up."""
        signal.signal(signal.SIGTERM, self._on_sigterm)
        while True:
            try:
                req = self._conn.receive()
            except ProtocolError as e:
                print('%s: fatal protocol error: %s' % (self.name, e), file=sys.stderr)
                sys.exit(1)
            if req is None or req['op'] == 'exit':
                return
            self._handle(req)

    def _handle(self, req):
        op = req['op']
        if op == 'hello':
            self._conn.send(
                {
                    'ev': 'hello',
                    'protocol': PROTOCOL_VERSION,
                    'name': self.name,
                    'description': self.description,
                    'author': self.author,
                    'roles': self.roles(),
                    'filters': list(self.filters),
                    'groups': list(self.groups),
                    'mime_types': list(self.mime_types),
                    'parallel': False,
                }
            )
        elif op == 'run':
            self._run(req)
        elif op == 'cancel':
            if self._job is not None and req.get('job') == self._job:
                self._cancelled = True
        elif 'job' in req and self._job is None:
            self._job = req['job']
            self.error(enums.ERROR_NOT_SUPPORTED, "unknown request '%s'" % op)
            self._finish()

    def _run(self, req):
        self._job = req['job']
        self._cancelled = False
        self.ctx = SimpleNamespace(**{**_CTX_DEFAULTS, **req.get('ctx', {})})
        self._export_ctx()
        role = req['role']
        method = getattr(self, role.replace('-', '_'), None) if role in ROLE_ARGS else None
        try:
            if method is None:
                raise PkError(enums.ERROR_NOT_SUPPORTED, "role '%s' is not supported" % role)
            method(*(req[key] for key in ROLE_ARGS[role]))
        except PkError as e:
            self.error(e.code, e.details)
        except Exception:  # noqa: BLE001 - the helper must survive any bug in a job
            traceback.print_exc()
            self.error(enums.ERROR_INTERNAL_ERROR, traceback.format_exc())
        finally:
            self._finish()

    def _finish(self):
        self._emit('finished')
        self._job = None

    def _export_ctx(self):
        """Mirror the context into the environment for libraries that read it there."""
        env = {'LANG': self.ctx.locale}
        for key, (var, scheme) in _PROXY_ENV.items():
            value = self.ctx.proxy.get(key)
            if value and scheme and '://' not in value:
                value = scheme + value
            env[var] = value
        for var, value in env.items():
            if value:
                os.environ[var] = value
            else:
                os.environ.pop(var, None)

    def _on_sigterm(self, signum, frame):
        if self._job is None:
            sys.exit(0)
        self._cancelled = True
        raise CancelledError('Terminated by the daemon')

    @property
    def cancelled(self):
        """Whether the daemon asked to cancel the running job."""
        while not self._cancelled:
            req = self._conn.receive(timeout=0)
            if req is None:
                break
            self._handle(req)
        return self._cancelled

    def check_cancelled(self):
        """Raise CancelledError if the daemon asked to cancel the running job.

        Call this at points where the job can be safely abandoned.
        """
        if self.cancelled:
            raise CancelledError()

    #
    # emitters
    #

    def _emit(self, ev, **fields):
        self._flush()
        self._conn.send({'ev': ev, 'job': self._job, **fields})

    def _queue(self, ev, item):
        items = self._batches.setdefault(ev, [])
        items.append(item)
        if len(items) >= self.batch_size:
            self._flush()

    def _flush(self):
        batches, self._batches = self._batches, {}
        for ev, items in batches.items():
            self._conn.send({'ev': ev, 'job': self._job, 'items': items})

    def log(self, level, message):
        """Send a message to the daemon log; level is debug, info, warning or error."""
        self._emit('log', level=level, message=message)

    def status(self, status):
        self._emit('status', status=status)

    def percentage(self, value=None):
        """Report progress; None means no percentage is available."""
        self._emit('percentage', value=value)

    def item_progress(self, package_id, status, value):
        self._emit('item-progress', package_id=package_id, status=status, value=value)

    def speed(self, bytes_per_second):
        self._emit('speed', value=bytes_per_second)

    def download_size_remaining(self, size):
        self._emit('download-size-remaining', value=size)

    def allow_cancel(self, allow=True):
        self._emit('allow-cancel', value=bool(allow))

    def require_restart(self, restart, package_id):
        self._emit('require-restart', restart=restart, package_id=package_id)

    def package(self, package_id, info, summary='', severity=None):
        """Queue a query result."""
        item = {'package_id': package_id, 'info': info, 'summary': summary}
        if severity is not None:
            item['severity'] = severity
        self._queue('packages', item)

    def package_status(self, package_id, info):
        """Report what is currently happening to a package in a transaction."""
        self._emit('package-status', package_id=package_id, info=info)

    def details(
        self,
        package_id,
        summary=None,
        license=None,
        group=enums.GROUP_UNKNOWN,
        description=None,
        url=None,
        size=None,
        download_size=None,
    ):
        """Queue package details; sizes are in bytes, None if unknown."""
        self._queue(
            'details',
            {
                'package_id': package_id,
                'summary': summary,
                'license': license,
                'group': group,
                'description': description,
                'url': url,
                'size': size,
                'download_size': download_size,
            },
        )

    def update_detail(
        self,
        package_id,
        updates=(),
        obsoletes=(),
        vendor_urls=(),
        bugzilla_urls=(),
        cve_urls=(),
        restart=enums.RESTART_NONE,
        update_text=None,
        changelog=None,
        state=enums.UPDATE_STATE_UNKNOWN,
        issued=None,
        updated=None,
    ):
        """Queue update details; issued and updated are ISO 8601 timestamps."""
        self._queue(
            'update-details',
            {
                'package_id': package_id,
                'updates': list(updates),
                'obsoletes': list(obsoletes),
                'vendor_urls': list(vendor_urls),
                'bugzilla_urls': list(bugzilla_urls),
                'cve_urls': list(cve_urls),
                'restart': restart,
                'update_text': update_text,
                'changelog': changelog,
                'state': state,
                'issued': issued,
                'updated': updated,
            },
        )

    def files(self, package_id, files):
        """Report the files of a package, or of a download when package_id is None."""
        self._emit('files', package_id=package_id, files=list(files))

    def repo_detail(self, repo_id, description, enabled):
        self._queue(
            'repo-details', {'repo_id': repo_id, 'description': description, 'enabled': enabled}
        )

    def distro_upgrade(self, type, name, summary):
        self._queue('distro-upgrades', {'type': type, 'name': name, 'summary': summary})

    def error(self, code, details=''):
        """Fail the job with an error code; the job still runs until the method returns."""
        self._emit('error', code=code, details=details)

    def eula_required(self, eula_id, package_id, vendor_name, license_agreement):
        self._emit(
            'eula-required',
            eula_id=eula_id,
            package_id=package_id,
            vendor_name=vendor_name,
            license_agreement=license_agreement,
        )

    def repo_signature_required(
        self,
        package_id,
        repository_name,
        key_url,
        key_userid,
        key_id,
        key_fingerprint,
        key_timestamp,
        type=enums.SIGTYPE_GPG,
    ):
        self._emit(
            'repo-signature-required',
            package_id=package_id,
            repository_name=repository_name,
            key_url=key_url,
            key_userid=key_userid,
            key_id=key_id,
            key_fingerprint=key_fingerprint,
            key_timestamp=key_timestamp,
            type=type,
        )
