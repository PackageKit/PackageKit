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

#pragma once

#include <nix/expr/get-drvs.hh>

#include <filesystem>

namespace nix
{

bool createUserEnv(
    EvalState &state,
    PackageInfos &elems,
    const std::filesystem::path &profile,
    bool keepDerivations,
    const std::string &lockToken);

PackageInfos queryInstalled(EvalState &state, const std::filesystem::path &userEnv);

} // namespace nix
