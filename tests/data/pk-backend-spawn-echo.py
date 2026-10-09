#!/usr/bin/env python3
#
# Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Test helper for PkBackendSpawn: echoes every line received on the
# protocol socket back with an "echo:" prefix, exits cleanly on "exit",
# and writes to stdout and stderr to prove those are kept apart from the
# protocol channel.

import os
import sys

fd = int(os.environ["PK_BACKEND_PROTOCOL_FD"])
proto_in = os.fdopen(fd, "r", buffering=1, closefd=False)
proto_out = os.fdopen(fd, "w", buffering=1, closefd=False)

# chatty output on stdout must never reach the protocol socket
for i in range(200):
    print("stdout noise line %d" % i)
sys.stdout.flush()
print("helper started, PATH=%s" % os.environ.get("PATH", "<unset>"), file=sys.stderr)
sys.stderr.flush()

for line in proto_in:
    line = line.rstrip("\n")
    if line == "exit":
        break
    if line == "die":
        # simulate an unexpected failure
        sys.exit(3)
    proto_out.write("echo:" + line + "\n")
    proto_out.flush()

sys.exit(0)
