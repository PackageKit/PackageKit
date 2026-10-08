Title: Spawned Backends

## Introduction

If there is no C or C++ binding for a package manager, PackageKit can
run the backend as a separate helper process written in any language.
The daemon starts the helper, keeps it running between transactions,
and exchanges JSONL messages with it over a dedicated socket.
The helper's standard output and standard error are captured as log
output and are never interpreted as protocol data.

Even when using helpers, a compiled backend stub is still used for
two reasons:

- It is still needed for the dlopen internally in PackageKit.

- You can add cleverness in the C backend that you might not want to
  do in the scripted backend, for example using a hash table in C
  rather than checking all the names in awk.

Backends are typically open-programmable, which means we can define a
standard for what goes on stdin and stdout to try and maximise
the common code between the backends.

If you are unable to write scripts that conform to these specifications
then just launch a PkSpawn object in the compiled helper with stdout
callbacks and then try to do screenscraping in the backend.

Backends scripts are run with arguments and data is sent to standard out
and standard error asynchronously so that PackageKit can proxy this to D-Bus.
A method has command line arguments separated with tabs, and data is also
separated with tabs.

It is important to flush the standard output after each output, else
Linux will helpfully buffer the output into more efficient size chunks.
If you do not flush, then there will be a long IPC delay.
Flushing can be achieved in C using `fflush` or in python
using `sys.stdout.flush()`.

The "dispatcher" mode is where a command is used to startup the
backend, for instance `yumBackend.py search-name none power`
and then the backend then sits and waits for more standard input.
Further operations can be done on the loaded backend sending commands
to stdin, e.g. `search-name none power`.
If there are no more operations after a preset time (default 5 seconds)
then the backend is sent `exit` over stdin, and the
backend terminates.
The daemon will ensure the operations are serialised, and that backends
not sending `finished` are cleaned up properly.

The dispatcher mode does not have to implemented in python; any
language that can read from stdin can block and be used in this way.
