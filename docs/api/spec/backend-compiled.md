Title: Compiled Backends

If you have a C or C++ binding for your package system then
you can use a compiled backend, which is more efficient than
using helpers as described below.
You can include the headers in the backend (with extra
libraries) and then just write the simple code to interface
with the methods of PackageKit.
A C example can be found in `backends/box` and a
C++ example in `backends/apt`.

You will have to use threading if your backend does not support async
operation as requests have to return immediately.
This is very important. Do any significant processing in a thread, and
certainly don't return package results without creating a thread.
By keeping the backends async we can make sure that there is no blocking
which means the command line and UI do not freeze.
