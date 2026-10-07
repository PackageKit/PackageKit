#!/usr/bin/env python3
#
# Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Test helper for PkBackendProcess: handles SIGTERM by saying goodbye on the
# protocol socket and exiting with status 0, then waits to be terminated.

import os
import signal
import sys
import time

fd = int(os.environ["PK_BACKEND_PROTOCOL_FD"])
proto_out = os.fdopen(fd, "w", buffering=1, closefd=False)


def on_term(signum, frame):
    proto_out.write("bye\n")
    proto_out.flush()
    sys.exit(0)


signal.signal(signal.SIGTERM, on_term)
proto_out.write("ready\n")
proto_out.flush()
while True:
    time.sleep(0.1)
