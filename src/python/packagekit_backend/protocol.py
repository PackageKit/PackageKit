# Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

"""JSON Lines transport between a spawned backend and packagekitd."""

import json
import os
import select

PROTOCOL_FD_ENV = 'PK_BACKEND_PROTOCOL_FD'


class ProtocolError(Exception):
    """The daemon sent something that is not a protocol request."""


class Connection:
    """One protocol channel: a file descriptor carrying one JSON object per line.

    Without an explicit descriptor, the one named by the ``PK_BACKEND_PROTOCOL_FD``
    environment variable is used, which is how the daemon hands it over.
    """

    def __init__(self, fd=None):
        if fd is None:
            try:
                fd = int(os.environ[PROTOCOL_FD_ENV])
            except KeyError:
                raise RuntimeError(
                    '%s is not set: this program must be started by packagekitd' % PROTOCOL_FD_ENV
                ) from None
        self._fd = fd
        self._buf = b''
        self._eof = False

    def send(self, msg):
        """Write one message and flush it."""
        data = (json.dumps(msg, separators=(',', ':'), ensure_ascii=False) + '\n').encode()
        while data:
            data = data[os.write(self._fd, data) :]

    def receive(self, timeout=None):
        """Return the next request as a dict.

        Returns None at end of file, or, with a timeout in seconds, when no
        complete line arrived in time. Raises ProtocolError for a line that is
        not a JSON object with an ``op`` field.
        """
        while b'\n' not in self._buf:
            if self._eof or not self._fill(timeout):
                return None
        line, _, self._buf = self._buf.partition(b'\n')
        try:
            msg = json.loads(line)
        except ValueError as e:
            raise ProtocolError('invalid JSON: %s' % e) from None
        if not isinstance(msg, dict) or 'op' not in msg:
            raise ProtocolError('not a request: %.200r' % line)
        return msg

    def _fill(self, timeout):
        if timeout is not None and not select.select([self._fd], [], [], timeout)[0]:
            return False
        chunk = os.read(self._fd, 65536)
        if not chunk:
            self._eof = True
            return False
        self._buf += chunk
        return True
