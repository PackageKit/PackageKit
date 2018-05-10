/*
	Taken from Nix source files, requires Nix libraries to build.

	Nix is a powerful package manager for Linux and other Unix systems that
	makes package management reliable and reproducible. It provides atomic
	upgrades and rollbacks, side-by-side installation of multiple versions of a
	package, multi-user package management and easy setup of build environments.

	Nix is licensed under the LGPL v2.1
	Additional source code is available at https://github.com/NixOS/nix
	For more information visit http://nixos.org/nix/
 */

#ifndef NIX_LIB_PLUS_HH
#define NIX_LIB_PLUS_HH

#include <nix/config.h>

#include <nix/nixexpr.hh>
#include <nix/shared.hh>
#include <nix/eval.hh>
#include <nix/eval-inline.hh>
#include <nix/derivations.hh>
#include <nix/store-api.hh>
#include <nix/get-drvs.hh>
#include <nix/names.hh>
#include <nix/profiles.hh>
#include <nix/globals.hh>
#include <nix/attr-path.hh>

using namespace nix;

DrvInfos queryInstalled(EvalState & state, const Path & userEnv);

bool createUserEnv(EvalState & state, DrvInfos & elems, const Path & profile, bool keepDerivations, const string & lockToken);

bool isNixExpr(const Path & path, const struct stat & st);

void loadSourceExpr(EvalState & state, const Path & path, Value & v);

void getAllExprs(EvalState & state, const Path & path, StringSet & strings, Value & v);

int getPriority (EvalState & state, DrvInfo & drv);

int comparePriorities (EvalState & state, DrvInfo & drv1, DrvInfo & drv2);

bool keep (DrvInfo & drv);

#endif
