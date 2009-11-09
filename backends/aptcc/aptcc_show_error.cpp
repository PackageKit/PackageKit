// aptcc_show_broken.cc
//
//   Copyright 2004 Daniel Burrows
//   Copyright 2009 Daniel Nicoletti

#include "aptcc_show_error.h"

#include <string>
#include <sstream>

using namespace std;

bool show_errors(PkBackend *backend, PkErrorEnum errorCode)
{
	stringstream errors;

	string Err;
	while (_error->empty() == false)
	{
		bool Type = _error->PopMessage(Err);
		if (Type == true) {
			errors << "E: " << Err << endl;
		} else {
			errors << "W: " << Err << endl;
		}
	}

	if (!errors.str().empty())
	{
		pk_backend_error_code(backend, errorCode, errors.str().c_str());
	}
}

bool show_warnings(PkBackend *backend, PkMessageEnum message)
{
	stringstream warnings;

	string Err;
	while (_error->empty() == false)
	{
		bool Type = _error->PopMessage(Err);
		if (Type == true) {
			warnings << "E: " << Err << endl;
		} else {
			warnings << "W: " << Err << endl;
		}
	}

	if (!warnings.str().empty())
	{
		pk_backend_message(backend, message, warnings.str().c_str());
	}
}
