#!/bin/sh

$EDITOR docs/spec/pk-methods.xml src/pk-interface-transaction.xml src/pk-transaction.h src/pk-transaction.c  python/packagekit/backend.py python/packagekit/daemonBackend.py libpackagekit/pk-client.h libpackagekit/pk-client.c  libpackagekit/pk-enum.h libpackagekit/pk-enum.c client/pk-console.c backends/*/pk-*.c* src/pk-backend-dbus.c src/pk-backend-dbus.h contrib/*.bash src/pk-engine.c src/pk-backend.h docs/html/pk-faq.html ../gnome-packagekit/src/gpk-common.c

