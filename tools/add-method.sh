#!/bin/sh

$EDITOR docs/spec/pk-methods.xml src/pk-interface.xml src/pk-engine.h src/pk-engine.c  src/pk-runner.h src/pk-runner.c python/packagekit/backend.py python/packagekit/daemonBackend.py libpackagekit/pk-client.h libpackagekit/pk-task-client.c client/pk-console.c backends/*/pk-*.c src/pk-backend-dbus.c src/pk-backend-dbus.h

