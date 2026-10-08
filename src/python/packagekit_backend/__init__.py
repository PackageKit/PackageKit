# Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Library for writing PackageKit spawned backends in Python.

A backend subclasses :class:`Backend`, implements the roles it supports as
methods named after the role (``search_name``, ``install_packages``, ...),
and calls :meth:`Backend.main` to serve requests from the daemon.
"""

from .enums import *  # noqa: F401,F403
from .backend import (  # noqa: F401
    PROTOCOL_VERSION,
    ROLE_ARGS,
    Backend,
    CancelledError,
    PkError,
    get_package_id,
    split_package_id,
)
from .protocol import Connection, ProtocolError  # noqa: F401
