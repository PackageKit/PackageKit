#!/usr/bin/env bash
#
# Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Test helper for PkBackendProcess: ignores SIGTERM so the daemon has to
# escalate to SIGKILL.

trap '' TERM
echo ready >&"$PK_BACKEND_PROTOCOL_FD"
exec sleep 1000
