Title: Spawned Backend Protocol

This is the protocol between `packagekitd` and a *spawned backend*: a helper
program, in any language, that implements the package management operations
for a distribution. It is the reference for the daemon's generic `spawn`
module, for the `packagekit_backend` Python library, and for helpers written
without that library.

## 1. Overview

- The daemon runs one helper process per backend, started when the
  backend is loaded, stopped after an idle period, restarted on demand.
- Messages are JSON Lines over a Unix stream socket on file descriptor 3.
- The helper's stdout and stderr are treated as log streams
- Every transaction is a job: the daemon sends `run`, the helper emits
  events for it and ends it with `finished`. Only one job runs at a time
  (but every message carries a job id so a later version may allow concurrency).

## 2. Process model

### 2.1 Manifest

A spawned backend is a directory `$libdir/packagekit/backends/<name>/`
holding its helper and a keyfile manifest `backend.conf`. `<name>` is the
backend name used in `DefaultBackend=`. If no `libpk_backend_<name>.so`
exists but the manifest does, the daemon loads its `spawn` module for it.

```ini
[Backend]
# Helper, relative to this directory or absolute. Required.
Exec=my-backend.py
# Send SIGKILL if the helper ignores SIGTERM. Default: false.
AllowSigkill=false
```

Author, Summary, Roles, filters, groups and MIME types are reported by the helper in `hello`.

### 2.2 Starting the helper

The helper runs with no arguments, stdin on `/dev/null`, the daemon end of
an `AF_UNIX`/`SOCK_STREAM` socket pair kept with `O_CLOEXEC` and the other
end on fd 3, and this environment:

| Variable                 | Value                                                                           |
|--------------------------|---------------------------------------------------------------------------------|
| `PK_BACKEND_PROTOCOL_FD` | `3`. Read this, so a helper can be run by hand with the protocol on another fd. |
| `PYTHONPATH`             | `$libdir/packagekit/python`, where `packagekit_backend` is installed.           |
| `PATH`                   | The daemon's `PATH`, or `/usr/bin:/bin` if unset.                               |

### 2.3 Handshake

After starting the helper the daemon sends `hello` and waits for the
helper's `hello` event. If none arrives within **10 seconds**, or the
protocol version does not match, the helper is terminated and the backend
fails to load. The handshake also follows every restart.

### 2.4 Idle exit

After `BackendShutdownTimeout` seconds without a job (default 10 seconds),
the daemon sends `exit`. A helper still alive 5 seconds later gets `SIGTERM`,
then `SIGKILL` if allowed. The next job starts a fresh helper.

### 2.5 Exit status

The helper should only ever exit after `exit`, with status 0. Anything else
is a failure that should never happen:

- Exiting during a job finishes the job with `internal-error` (naming the
  exit status or signal) unless the helper already sent `error`; a helper
  killed by the daemon's own signals finishes the job with `process-kill`.
- Exiting while idle, including with a non-zero status after `exit`, is
  logged as a warning.

## 3. Message format

- One JSON object per line, UTF-8, `\n`-terminated, at most **16 MiB**.
- Requests (daemon → helper) have `op`; events (helper → daemon) have `ev`.
- Messages about a job carry `job`, an opaque string chosen by the daemon
  and unique for the lifetime of the helper process.
- Enum values are the strings from `lib/pk-enum.c` (`"search-name"`, `"installed"`,
  `"~devel"`, `"only-trusted"`, ...). Unknown sizes are `null`.
- Receivers ignore unknown keys.
- From the helper, a line that is not a JSON object with `ev`, or a known
  event with a missing or mistyped field or an unknown enum value, is a
  **fatal protocol error**: the daemon fails the running job with
  `internal-error` and terminates the helper. An unknown `ev` is logged and
  ignored, as is an event for a job that is not the running one.
- A helper that gets an invalid line from the daemon should exit with a
  message on stderr. An unknown `op` with a job id is answered with
  `error` `not-supported` and `finished`; without one it is ignored.

## 4. Requests

### 4.1 `hello`

```json
{"op":"hello","protocol":1,"daemon_version":"2.0.0"}
```

### 4.2 `run`

```json
{"op":"run","job":"42","role":"search-name",
 "filters":["installed","~devel"],"values":["power manager"],
 "ctx":{"locale":"de_DE.UTF-8","uid":1000,"network":true,"background":false,
        "interactive":true,"cache_age":86400,"details_with_deps_size":false,
        "frontend_socket":null,
        "proxy":{"http":"http://proxy.example.org:3128","no_proxy":"localhost"},
        "accepted_eulas":["vendor-eula-1"]}}
```

| Field     | Type            | Meaning                                                  |
|-----------|-----------------|----------------------------------------------------------|
| `job`     | string          | Echoed on every event of this job.                       |
| `role`    | string          | `enum_role`.                                             |
| `filters` | array of string | `enum_filter`, only for roles that take filters.         |
| `flags`   | array of string | `enum_transaction_flag`, only for roles that take flags. |
| `ctx`     | object          | Context, always present, fields below.                   |

| `ctx` field              | Type            | Meaning                                                                                                           |
|--------------------------|-----------------|-------------------------------------------------------------------------------------------------------------------|
| `locale`                 | string          | Client locale, e.g. `"C"`.                                                                                        |
| `uid`                    | integer         | Client's Unix user id.                                                                                            |
| `network`                | boolean         | Whether the daemon believes the system is online.                                                                 |
| `background`             | boolean         | Low-priority transaction. The daemon already lowered the helper's priority if it started it for a background job. |
| `interactive`            | boolean         | A user can answer questions.                                                                                      |
| `cache_age`              | integer or null | Maximum metadata age in seconds, `null` for no limit.                                                             |
| `details_with_deps_size` | boolean         | For `get-details`, include dependency sizes in `download_size`.                                                   |
| `frontend_socket`        | string or null  | Path of the client's frontend socket (debconf), if any.                                                           |
| `proxy`                  | object          | Keys `http`, `https`, `ftp`, `socks`, `no_proxy`, `pac`; absent means unset.                                      |
| `accepted_eulas`         | array of string | EULA ids the user has accepted.                                                                                   |

Role-specific fields (package ids are `name;version;arch;origin;data`):

| Role                                                                                       | Fields                                                                 |
|--------------------------------------------------------------------------------------------|------------------------------------------------------------------------|
| `search-name`, `search-details`, `search-group`, `search-file`, `what-provides`, `resolve` | `filters`, `values` (array of string)                                  |
| `get-packages`, `get-updates`, `get-repo-list`                                             | `filters`                                                              |
| `depends-on`, `required-by`                                                                | `filters`, `package_ids`, `recursive` (boolean)                        |
| `get-details`, `get-files`, `get-update-detail`                                            | `package_ids`                                                          |
| `get-details-local`, `get-files-local`                                                     | `files` (array of absolute paths)                                      |
| `get-distro-upgrades`                                                                      | none                                                                   |
| `download-packages`                                                                        | `package_ids`, `directory`                                             |
| `install-packages`, `update-packages`                                                      | `flags`, `package_ids`                                                 |
| `install-files`                                                                            | `flags`, `files`                                                       |
| `remove-packages`                                                                          | `flags`, `package_ids`, `allow_deps` (boolean), `autoremove` (boolean) |
| `install-signature`                                                                        | `sig_type` (`enum_sig_type`), `key_id`, `package_id`                   |
| `refresh-cache`                                                                            | `force` (boolean)                                                      |
| `repo-enable`                                                                              | `repo_id`, `enabled` (boolean)                                         |
| `repo-set-data`                                                                            | `repo_id`, `parameter`, `value`                                        |
| `repo-remove`                                                                              | `flags`, `repo_id`, `autoremove` (boolean)                             |
| `upgrade-system`                                                                           | `flags`, `distro_id`, `upgrade_kind` (`enum_upgrade_kind`)             |
| `repair-system`                                                                            | `flags`                                                                |

A role the helper did not list in `hello` is answered with `error`
`not-supported`; the daemon does not send such requests normally.

### 4.3 `cancel`

```json
{"op":"cancel","job":"42"}
```

Sent when a client cancels the transaction. The helper should stop at the
next safe point, undo or complete what cannot be left half-done, and emit
`error` `transaction-cancelled` then `finished`.

### 4.4 `exit`

```json
{"op":"exit"}
```

Sent only while idle. The helper releases its resources and exits with 0.

## 5. Events

### 5.1 `hello`

```json
{"ev":"hello","protocol":1,"name":"my-backend","description":"A Spawned Backend",
 "author":"Jane Example <jane@example.org>",
 "roles":["search-name","resolve","install-packages","get-updates"],
 "filters":["installed","devel","free","newest"],"groups":["games","system"],
 "mime_types":["application/x-tar"],"parallel":false}
```

| Field                           | Type             | Meaning                                                   |
|---------------------------------|------------------|-----------------------------------------------------------|
| `protocol`                      | integer          | Must equal the daemon's version.                          |
| `name`, `description`, `author` | string, optional | Shown by clients; override the manifest.                  |
| `roles`                         | array of string  | Roles implemented. `cancel` is implied.                   |
| `filters`, `groups`             | array of string  | Filters honoured (negated forms implied) and groups used. |
| `mime_types`                    | array of string  | Accepted by `install-files`.                              |
| `parallel`                      | boolean          | Reserved, must be `false`.                                |

### 5.2 Progress and state

All carry `job`. `value` percentages are integers 0–100.

| Event                     | Fields                                          | Daemon call                                                                     |
|---------------------------|-------------------------------------------------|---------------------------------------------------------------------------------|
| `status`                  | `status` (`enum_status`)                        | `pk_backend_job_set_status`                                                     |
| `percentage`              | `value` (integer or `null` for "unknown")       | `pk_backend_job_set_percentage`; a value lower than the previous one is ignored |
| `item-progress`           | `package_id`, `status` (`enum_status`), `value` | `pk_backend_job_set_item_progress`                                              |
| `speed`                   | `value` (bytes per second)                      | `pk_backend_job_set_speed`                                                      |
| `download-size-remaining` | `value` (bytes)                                 | `pk_backend_job_set_download_size_remaining`                                    |
| `allow-cancel`            | `value` (boolean)                               | `pk_backend_job_set_allow_cancel`                                               |
| `require-restart`         | `restart` (`enum_restart`), `package_id`        | `pk_backend_job_require_restart`                                                |

```json
{"ev":"status","job":"42","status":"download"}
{"ev":"percentage","job":"42","value":37}
{"ev":"item-progress","job":"42","package_id":"foo;1.0;x86_64;main;","status":"install","value":50}
```

### 5.3 Results

Results are **batched**: emit the items of a query in one event, or a few
for very large sets. One item per event defeats the purpose.

`packages`: query results, `pk_backend_job_packages`. `info` is `enum_info`,
`summary` and `severity` (`enum_info` severity for updates) are optional.
Items already emitted for the job are dropped.

```json
{"ev":"packages","job":"42","items":[
  {"package_id":"foo;1.0;x86_64;main;","info":"installed","summary":"A foo"},
  {"package_id":"bar;2.1;x86_64;main;","info":"available","summary":"A bar","severity":"security"}]}
```

`package-status`: what is happening to one package during a transaction,
`pk_backend_job_package_status`. No summary; `info` is a progress value of
`enum_info` (`downloading`, `installing`, `removing`, ...).

```json
{"ev":"package-status","job":"42","package_id":"foo;1.0;x86_64;main;","info":"installing"}
```

`details`: `pk_backend_job_details` per item. `group` is `enum_group`;
`summary`, `license`, `description`, `url`, `size` (installed bytes) and
`download_size` are optional.

```json
{"ev":"details","job":"42","items":[
  {"package_id":"foo;1.0;x86_64;main;","summary":"A foo","license":"GPL-2.0-or-later",
   "group":"system","description":"Long text\nwith newlines.","url":"https://example.org",
   "size":123456,"download_size":45678}]}
```

`update-details`: `pk_backend_job_update_details`. `restart` is
`enum_restart`, `state` is `enum_update_state`; the arrays, `update_text`,
`changelog` and the ISO 8601 timestamps `issued` and `updated` are optional.

```json
{"ev":"update-details","job":"42","items":[
  {"package_id":"foo;1.1;x86_64;main;","updates":["foo;1.0;x86_64;installed;"],"obsoletes":[],
   "vendor_urls":["https://example.org/advisory/1"],"bugzilla_urls":[],
   "cve_urls":["https://www.cve.org/CVERecord?id=CVE-2026-0001"],
   "restart":"none","update_text":"Fixes a crash.","changelog":"* 1.1: fix crash",
   "state":"stable","issued":"2026-09-01T10:00:00Z","updated":"2026-09-02T12:30:00Z"}]}
```

`files`: `pk_backend_job_files`. For `download-packages`, `package_id` may
be `null` and `files` lists the downloaded paths.

```json
{"ev":"files","job":"42","package_id":"foo;1.0;x86_64;main;","files":["/usr/bin/foo","/usr/share/doc/foo"]}
```

`repo-details`: `pk_backend_job_repo_detail` per item; `description` is
optional.

```json
{"ev":"repo-details","job":"42","items":[{"repo_id":"main","description":"Main repository","enabled":true}]}
```

`distro-upgrades`: `pk_backend_job_distro_upgrade` per item; `type` is
`enum_upgrade`, `summary` is optional.

```json
{"ev":"distro-upgrades","job":"42","items":[{"type":"stable","name":"example-27","summary":"Example OS 27"}]}
```

### 5.4 Errors, interaction, logging

`error`: `code` is `enum_error`, `details` is optional free text. Only the
first error of a job counts (a `lock-required` error may be replaced).
`finished` must still follow. The exit code is derived as for compiled
backends.

```json
{"ev":"error","job":"42","code":"package-not-found","details":"No package matches 'foo'"}
```

`eula-required`: at most once per job, then `finished`; the daemon sets the
exit code `eula-required`. Once accepted, the id appears in
`ctx.accepted_eulas` of the retried job. `vendor_name` and
`license_agreement` are optional.

```json
{"ev":"eula-required","job":"42","eula_id":"vendor-eula-1","package_id":"foo;1.0;x86_64;main;",
 "vendor_name":"Vendor","license_agreement":"Full text…"}
```

`repo-signature-required`: at most once per job, then `finished`; exit code
`key-required`. `type` is `enum_sig_type`; everything but `type` is
optional.

```json
{"ev":"repo-signature-required","job":"42","package_id":"foo;1.0;x86_64;main;",
 "repository_name":"main","key_url":"https://example.org/key.gpg",
 "key_userid":"Example Signing Key <key@example.org>","key_id":"0123456789ABCDEF",
 "key_fingerprint":"…","key_timestamp":"2026-01-01T00:00:00Z","type":"gpg"}
```

`log`: a message for the daemon log, never shown to clients. `level` is
`debug` or `info`; anything else is logged as a warning. `job` may be
`null` outside a job. Replaces the old `message` and `data` lines.

```json
{"ev":"log","job":"42","level":"info","message":"Refreshing 3 repositories"}
```

### 5.5 `finished`

```json
{"ev":"finished","job":"42"}
```

Ends the job; later events for the same id are ignored.

## 6. Example session

```
→ {"op":"hello","protocol":1,"daemon_version":"2.0.0"}
← {"ev":"hello","protocol":1,"name":"example","roles":["search-name","install-packages"],"filters":["installed"],"groups":[],"mime_types":[],"parallel":false}
→ {"op":"run","job":"1","role":"search-name","filters":["~installed"],"values":["foo"],"ctx":{"locale":"C","uid":1000,"network":true,"background":false,"interactive":true,"cache_age":null,"details_with_deps_size":false,"frontend_socket":null,"proxy":{},"accepted_eulas":[]}}
← {"ev":"status","job":"1","status":"query"}
← {"ev":"packages","job":"1","items":[{"package_id":"foo;1.0;x86_64;main;","info":"available","summary":"A foo"}]}
← {"ev":"finished","job":"1"}
→ {"op":"run","job":"2","role":"install-packages","flags":["only-trusted"],"package_ids":["foo;1.0;x86_64;main;"],"ctx":{…}}
← {"ev":"status","job":"2","status":"download"}
← {"ev":"package-status","job":"2","package_id":"foo;1.0;x86_64;main;","info":"downloading"}
← {"ev":"percentage","job":"2","value":50}
→ {"op":"cancel","job":"2"}
← {"ev":"error","job":"2","code":"transaction-cancelled","details":"Cancelled by user"}
← {"ev":"finished","job":"2"}
   (5 s idle)
→ {"op":"exit"}
   (helper exits 0)
```

## 7. Writing a helper without the Python library

1. Read `PK_BACKEND_PROTOCOL_FD` and open it for reading and writing.
2. Read lines until EOF, parse each as JSON: answer `hello`, run `run`, set
   a cancel flag on `cancel`, leave the loop on `exit`.
3. Write one compact JSON object per line and flush after each.
4. Always end a job with `finished`, also after `error` and when the
   implementation throws. Never exit mid-job.
5. On `SIGTERM`, end the running job with `transaction-cancelled` and
   `finished` if that is quick, then exit.
