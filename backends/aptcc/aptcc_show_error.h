// aptcc_show_broken.h                  -*-c++-*-
//
//   Copyright 2004 Daniel Burrows
//   Copyright 2009 Daniel Nicoletti

#ifndef APTCC_SHOW_ERROR_H
#define APTCC_SHOW_ERROR_H

#include <pk-backend.h>
#include <apt-pkg/error.h>

/** \file aptcc_show_error.h
 */

/**
 * Call the Packagekit error dialog
 */
bool show_errors(PkBackend *backend,
		PkErrorEnum errorCode = PK_ERROR_ENUM_UNKNOWN);

/**
 * Call the Packagekit message dialog
 */
bool show_warnings(PkBackend *backend,
		PkMessageEnum message = PK_MESSAGE_ENUM_UNKNOWN);
#endif // APTCC_SHOW_ERROR
