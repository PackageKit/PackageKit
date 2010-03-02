// aptcc_show_broken.h                  -*-c++-*-
//
//   Copyright 2004 Daniel Burrows
//   Copyright 2009-2010 Daniel Nicoletti

#ifndef APTCC_SHOW_BROKEN_H
#define APTCC_SHOW_BROKEN_H

#include <pk-backend.h>

#include <apt-pkg/cachefile.h>

/** \file aptcc_show_broken.h
 */

/** Shows a list of all broken packages together with their
 *  dependencies.  Similar to and based on the equivalent routine in
 *  apt-get.
 */
void show_broken(PkBackend *backend, pkgCacheFile &cache, bool Now);

#endif // APTCC_SHOW_BROKEN
