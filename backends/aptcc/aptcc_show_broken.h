// aptcc_show_broken.h                  -*-c++-*-
//
//   Copyright 2004 Daniel Burrows

#ifndef APTCC_SHOW_BROKEN_H
#define APTCC_SHOW_BROKEN_H

#include <pk-backend.h>

class aptcc;
/** \file aptcc_show_broken.h
 */

/** Shows a list of all broken packages together with their
 *  dependencies.  Similar to and based on the equivalent routine in
 *  apt-get.
 *
 *  Returns \b false if some packages are broken.
 */
bool show_broken(PkBackend *backend, aptcc *m_apt);

#endif // APTCC_SHOW_BROKEN
