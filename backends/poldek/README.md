poldek PackageKit Backend
-------------------------

The backend for [poldek](https://github.com/poldek-pm/poldek), the package
manager of PLD Linux. It is a spawned backend
(see `docs/extra/backend-spawn-protocol.md`): the daemon runs the helper
`pk-backend-poldek`, written in C against libpoldek, libpoclidek and librpm,
as a process of its own.

poldek keeps process-wide state (signal handling, logging, download hooks)
and can abort the process on some errors; in a helper of its own neither
affects the daemon, and an aborted helper only fails the job it ran.

## Setup

* `DefaultBackend=poldek` in `PackageKit.conf`: `auto` does not pick
  spawned backends.
* The helper uses poldek's configuration, `/etc/poldek/poldek.conf`, and
  manages `/`. The proxies a client sets in PackageKit are used for its
  downloads. poldek 0.45 keeps the first proxy it used until the helper
  exits, though: until then the downloads of every client go through it,
  also of one that set another proxy or none. Descriptions come in the
  client's language.
* It sets poldek up for its first job and keeps it until the daemon stops
  it after `BackendShutdownTimeout` seconds idle (10 by default). Setting
  up again costs about half a second with up-to-date indexes, so a longer
  timeout saves that on a busy machine.
* `AllowSigkill=false`: the helper is not killed during a transaction. rpm,
  which poldek runs as a child, would finish it anyway, while the daemon
  reported it as failed and the next job ran into rpm's lock.

## What it does

Roles: search-name, search-details, search-file, search-group (with
`repo:NAME`), what-provides, resolve, get-packages, get-details, get-files,
get-dependencies, get-dependents, get-updates, get-update-detail, install-packages,
update-packages, remove-packages, download-packages, install-files,
get-details-local, get-files-local, refresh-cache, get-repo-list,
upgrade-system, and cancel.

Filters: installed, newest, arch, devel (names ending in -devel, -static,
-debuginfo or -debugsource) and their negations.

Transaction flags: simulate, only-download, only-trusted, allow-reinstall,
just-reinstall, allow-downgrade. The purge flag is ignored: rpm keeps a
modified configuration file as `.rpmsave` and has no option not to.

Package ids:

| Package           | origin        | data        |
|-------------------|---------------|-------------|
| available         | source name   |             |
| installed         |               | `installed` |
| a file given to install-files, get-details-local or get-files-local | `local` | |

## Behavior worth knowing

* **Repositories with the same package.** poldek keeps one copy of a
  name-version-release-arch, from the source with the lowest `pri` value
  (configuration order by default), and only that one can be installed. The
  backend shows only that one; an id naming another source is not found.
* **Updates** are the newest available version of an installed package with
  the same name and color (or arch), as `poldek --upgrade` and `ls -u` see
  them. A package matched by a `hold` pattern is `blocked`, so is one whose
  upgrade does not resolve, such as one that needs a package no repository
  has, or that poldek gives up on, as for a name installed in several
  versions. Updates that need a reboot (glibc, a kernel) or a new session (dbus)
  say so in their details, and installing them asks for it. PLD has no
  update advisories, so update details are the changelog entries newer than
  the installed package, of the main package for a subpackage; CVE ids in
  them become issues and make the update `update-security`.
* **Downgrades.** poldek resolves dependencies differently in downgrade
  mode, so allow-downgrade only switches it on for a transaction that only
  downgrades. One that both upgrades and downgrades is refused, so is a
  reinstall together with anything else.
* **only-trusted** checks the signature of every package of the transaction,
  dependencies included, with the keys in the rpm database of the managed
  root, before rpm runs; rpm then checks them again. The PLD Th key is
  shipped in `/etc/pki/rpm-gpg/` but not imported, so until it is
  (`rpmkeys --import /etc/pki/rpm-gpg/PLD-3.0-Th-GPG-key.asc`) every trusted
  install fails with a signature error and clients offer an untrusted one.
* **The rpm database** can be changed by anything else (poldek, rpm); the
  helper notices before each job and reloads the installed packages.
* **autoremove** removes nothing more: rpm does not record which packages
  were installed only as dependencies, and poldek's greedy removal would
  also take ones installed on their own.
* **what-provides** takes capability names and file paths, without version
  constraints.
* **get-dependencies and get-dependents** read rich dependencies as rpm does:
  `(a if b)` depends on `a` while `b` is installed, `(a or b)` on the
  alternatives the installed packages meet in full if any do, `(a with b)`
  on a package that is both.

Not supported: get-distro-upgrades (PLD is rolling), repo-enable (the
helper does not rewrite poldek's configuration; edit its `[source]`
sections), install-signature, repair-system, repo-set-data, repo-remove,
EULAs, AppStream data.

## Tests

`meson test --suite poldek` needs PLD Linux, poldek and rpm-build, and root
for `rpm --root`. The fixture repositories are built with rpmbuild from
`tests/fixtures.json` when the tests run; nothing binary is committed.

On any machine with podman:

    podman build -f tests/ci/Dockerfile-pld -t packagekit-pld .
    podman run --privileged -v $PWD:/build packagekit-pld \
        ./tests/ci/build-and-test.sh -Dpk-backend=poldek -Dgtk-module=false -Dgstreamer-plugin=false
    podman run --privileged -v $PWD:/build packagekit-pld \
        ./backends/poldek/tests/pk-poldek-smoke.py build

The last one runs packagekitd with the backend and drives it with pkgcli; it
changes the system it runs on, so only run it in a throwaway container.
