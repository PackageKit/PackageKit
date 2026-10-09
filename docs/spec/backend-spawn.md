Title: Spawned Backends

## Introduction

If there is no C or C++ binding for a package manager, PackageKit can
run the backend as a separate helper program written in any language.
The daemon starts the helper once, keeps it running between transactions, and exchanges
JSON Lines messages with it over a dedicated socket. The helper's standard output and
standard error are captured as log output.

A spawned backend consists of a directory `$libdir/packagekit/backends/<name>/` holding a
keyfile manifest, `backend.conf`, and the helper program it names:

```ini
[Backend]
Exec=portageBackend.py
Description=Portage
Author=Jane Example <jane@example.org>
AllowSigkill=true
```

When `DefaultBackend=<name>` names a backend for which no
`libpk_backend_<name>.so` exists but such a manifest does, the daemon
loads its generic `spawn` module for it. Everything else about the
backend, including the roles, filters and groups it supports, is
reported by the helper itself in the handshake.

## Writing a helper

Python helpers use the `packagekit_backend` library that PackageKit
installs privately for them: subclass `Backend`, implement the roles
you support as methods named after them, and call `main()`. The
library speaks the protocol, exports the transaction context to the
environment, turns exceptions into errors, and handles cancellation.

Helpers in other languages read the protocol descriptor number from
the `PK_BACKEND_PROTOCOL_FD` environment variable and implement the
protocol directly. It is specified in full in
[the spawned backend protocol](backend-spawn-protocol.html).
