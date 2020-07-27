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

#include <nix/config.h>

#include <nix/util.hh>
#include <nix/derivations.hh>
#include <nix/store-api.hh>
#include <nix/globals.hh>
#include <nix/shared.hh>
#include <nix/eval.hh>
#include <nix/eval-inline.hh>
#include <nix/profiles.hh>

#include "nix-lib-plus.hh"

namespace nix {

DrvInfos queryInstalled(EvalState & state, const Path & userEnv)
{
    DrvInfos elems;
    if (pathExists(userEnv + "/manifest.json"))
        throw Error("profile '%s' is incompatible with 'nix-env'; please use 'nix profile' instead", userEnv);
    Path manifestFile = userEnv + "/manifest.nix";
    if (pathExists(manifestFile)) {
        Value v;
        state.evalFile(manifestFile, v);
        Bindings & bindings(*state.allocBindings(0));
        getDerivations(state, v, "", bindings, elems, false);
    }
    return elems;
}

bool createUserEnv(EvalState & state, DrvInfos & elems,
    const Path & profile, bool keepDerivations,
    const string & lockToken)
{
    /* Build the components in the user environment, if they don't
       exist already. */
    std::vector<StorePathWithOutputs> drvsToBuild;
    for (auto & i : elems)
        if (i.queryDrvPath() != "")
            drvsToBuild.push_back({state.store->parseStorePath(i.queryDrvPath())});

    debug(format("building user environment dependencies"));
    state.store->buildPaths(drvsToBuild, state.repair ? bmRepair : bmNormal);

    /* Construct the whole top level derivation. */
    StorePathSet references;
    Value manifest;
    state.mkList(manifest, elems.size());
    unsigned int n = 0;
    for (auto & i : elems) {
        /* Create a pseudo-derivation containing the name, system,
           output paths, and optionally the derivation path, as well
           as the meta attributes. */
        Path drvPath = keepDerivations ? i.queryDrvPath() : "";

        Value & v(*state.allocValue());
        manifest.listElems()[n++] = &v;
        state.mkAttrs(v, 16);

        mkString(*state.allocAttr(v, state.sType), "derivation");
        mkString(*state.allocAttr(v, state.sName), i.queryName());
        auto system = i.querySystem();
        if (!system.empty())
            mkString(*state.allocAttr(v, state.sSystem), system);
        mkString(*state.allocAttr(v, state.sOutPath), i.queryOutPath());
        if (drvPath != "")
            mkString(*state.allocAttr(v, state.sDrvPath), i.queryDrvPath());

        // Copy each output meant for installation.
        DrvInfo::Outputs outputs = i.queryOutputs(true);
        Value & vOutputs = *state.allocAttr(v, state.sOutputs);
        state.mkList(vOutputs, outputs.size());
        unsigned int m = 0;
        for (auto & j : outputs) {
            mkString(*(vOutputs.listElems()[m++] = state.allocValue()), j.first);
            Value & vOutputs = *state.allocAttr(v, state.symbols.create(j.first));
            state.mkAttrs(vOutputs, 2);
            mkString(*state.allocAttr(vOutputs, state.sOutPath), j.second);

            /* This is only necessary when installing store paths, e.g.,
               `nix-env -i /nix/store/abcd...-foo'. */
            state.store->addTempRoot(state.store->parseStorePath(j.second));
            state.store->ensurePath(state.store->parseStorePath(j.second));

            references.insert(state.store->parseStorePath(j.second));
        }

        // Copy the meta attributes.
        Value & vMeta = *state.allocAttr(v, state.sMeta);
        state.mkAttrs(vMeta, 16);
        StringSet metaNames = i.queryMetaNames();
        for (auto & j : metaNames) {
            Value * v = i.queryMeta(j);
            if (!v) continue;
            vMeta.attrs->push_back(Attr(state.symbols.create(j), v));
        }
        vMeta.attrs->sort();
        v.attrs->sort();

        if (drvPath != "") references.insert(state.store->parseStorePath(drvPath));
    }

    /* Also write a copy of the list of user environment elements to
       the store; we need it for future modifications of the
       environment. */
    auto manifestFile = state.store->addTextToStore("env-manifest.nix",
        fmt("%s", manifest), references);

    /* Get the environment builder expression. */
    Value envBuilder;
    state.eval(state.parseExprFromString(
        #include "buildenv.nix.gen.hh"
            , "/"), envBuilder);

    /* Construct a Nix expression that calls the user environment
       builder with the manifest as argument. */
    Value args, topLevel;
    state.mkAttrs(args, 3);
    mkString(*state.allocAttr(args, state.symbols.create("manifest")),
        state.store->printStorePath(manifestFile), {state.store->printStorePath(manifestFile)});
    args.attrs->push_back(Attr(state.symbols.create("derivations"), &manifest));
    args.attrs->sort();
    mkApp(topLevel, envBuilder, args);

    /* Evaluate it. */
    debug("evaluating user environment builder");
    state.forceValue(topLevel);
    PathSet context;
    Attr & aDrvPath(*topLevel.attrs->find(state.sDrvPath));
    auto topLevelDrv = state.store->parseStorePath(state.coerceToPath(aDrvPath.pos ? *(aDrvPath.pos) : noPos, *(aDrvPath.value), context));
    Attr & aOutPath(*topLevel.attrs->find(state.sOutPath));
    Path topLevelOut = state.coerceToPath(aOutPath.pos ? *(aOutPath.pos) : noPos, *(aOutPath.value), context);

    /* Realise the resulting store expression. */
    debug("building user environment");
    std::vector<StorePathWithOutputs> topLevelDrvs;
    topLevelDrvs.push_back({topLevelDrv});
    state.store->buildPaths(topLevelDrvs, state.repair ? bmRepair : bmNormal);

    /* Switch the current user environment to the output path. */
    auto store2 = state.store.dynamic_pointer_cast<LocalFSStore>();

    if (store2) {
        PathLocks lock;
        lockProfile(lock, profile);

        Path lockTokenCur = optimisticLockProfile(profile);
        if (lockToken != lockTokenCur) {
            printInfo("profile '%1%' changed while we were busy; restarting", profile);
            return false;
        }

        debug(format("switching to new user environment"));
        Path generation = createGeneration(ref<LocalFSStore>(store2), profile, topLevelOut);
        switchLink(profile, generation);
    }

    return true;
}

}
