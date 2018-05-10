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

#include "nix-lib-plus.hh"

DrvInfos queryInstalled(EvalState & state, const Path & userEnv)
{
    DrvInfos elems;
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
    PathSet drvsToBuild;
    for (auto & i : elems)
        if (i.queryDrvPath() != "")
            drvsToBuild.insert(i.queryDrvPath());

    debug(format("building user environment dependencies"));
    state.store->buildPaths(drvsToBuild, state.repair ? bmRepair : bmNormal);

    /* Construct the whole top level derivation. */
    PathSet references;
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
            state.store->addTempRoot(j.second);
            state.store->ensurePath(j.second);

            references.insert(j.second);
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

        if (drvPath != "") references.insert(drvPath);
    }

    /* Also write a copy of the list of user environment elements to
       the store; we need it for future modifications of the
       environment. */
    Path manifestFile = state.store->addTextToStore("env-manifest.nix",
        (format("%1%") % manifest).str(), references);

    /* Get the environment builder expression. */
    Value envBuilder;
    state.evalFile(state.findFile("nix/buildenv.nix"), envBuilder);

    /* Construct a Nix expression that calls the user environment
       builder with the manifest as argument. */
    Value args, topLevel;
    state.mkAttrs(args, 3);
    mkString(*state.allocAttr(args, state.symbols.create("manifest")),
        manifestFile, {manifestFile});
    args.attrs->push_back(Attr(state.symbols.create("derivations"), &manifest));
    args.attrs->sort();
    mkApp(topLevel, envBuilder, args);

    /* Evaluate it. */
    debug("evaluating user environment builder");
    state.forceValue(topLevel);
    PathSet context;
    Attr & aDrvPath(*topLevel.attrs->find(state.sDrvPath));
    Path topLevelDrv = state.coerceToPath(aDrvPath.pos ? *(aDrvPath.pos) : noPos, *(aDrvPath.value), context);
    Attr & aOutPath(*topLevel.attrs->find(state.sOutPath));
    Path topLevelOut = state.coerceToPath(aOutPath.pos ? *(aOutPath.pos) : noPos, *(aOutPath.value), context);

    /* Realise the resulting store expression. */
    debug("building user environment");
    state.store->buildPaths({topLevelDrv}, state.repair ? bmRepair : bmNormal);

    /* Switch the current user environment to the output path. */
    auto store2 = state.store.dynamic_pointer_cast<LocalFSStore>();

    if (store2) {
        PathLocks lock;
        lockProfile(lock, profile);

        Path lockTokenCur = optimisticLockProfile(profile);
        if (lockToken != lockTokenCur) {
            printError(format("profile '%1%' changed while we were busy; restarting") % profile);
            return false;
        }

        debug(format("switching to new user environment"));
        Path generation = createGeneration(ref<LocalFSStore>(store2), profile, topLevelOut);
        switchLink(profile, generation);
    }

    return true;
}

bool isNixExpr(const Path & path, struct stat & st)
{
    return S_ISREG(st.st_mode) || (S_ISDIR(st.st_mode) && pathExists(path + "/default.nix"));
}


void getAllExprs(EvalState & state,
    const Path & path, StringSet & attrs, Value & v)
{
    StringSet namesSorted;
    for (auto & i : readDirectory(path)) namesSorted.insert(i.name);

    for (auto & i : namesSorted) {
        /* Ignore the manifest.nix used by profiles.  This is
           necessary to prevent it from showing up in channels (which
           are implemented using profiles). */
        if (i == "manifest.nix") continue;

        Path path2 = path + "/" + i;

        struct stat st;
        if (stat(path2.c_str(), &st) == -1)
            continue; // ignore dangling symlinks in ~/.nix-defexpr

        if (isNixExpr(path2, st) && (!S_ISREG(st.st_mode) || hasSuffix(path2, ".nix"))) {
            /* Strip off the `.nix' filename suffix (if applicable),
               otherwise the attribute cannot be selected with the
               `-A' option.  Useful if you want to stick a Nix
               expression directly in ~/.nix-defexpr. */
            string attrName = i;
            if (hasSuffix(attrName, ".nix"))
                attrName = string(attrName, 0, attrName.size() - 4);
            if (attrs.find(attrName) != attrs.end()) {
                printError(format("warning: name collision in input Nix expressions, skipping '%1%'") % path2);
                continue;
            }
            attrs.insert(attrName);
            /* Load the expression on demand. */
            Value & vFun = state.getBuiltin("import");
            Value & vArg(*state.allocValue());
            mkString(vArg, path2);
            if (v.attrs->size() == v.attrs->capacity())
                throw Error(format("too many Nix expressions in directory '%1%'") % path);
            mkApp(*state.allocAttr(v, state.symbols.create(attrName)), vFun, vArg);
        }
        else if (S_ISDIR(st.st_mode))
            /* `path2' is a directory (with no default.nix in it);
               recurse into it. */
            getAllExprs(state, path2, attrs, v);
    }
}

void loadSourceExpr(EvalState & state, const Path & path, Value & v)
{
    struct stat st;
    if (stat(path.c_str(), &st) == -1)
        throw SysError(format("getting information about '%1%'") % path);

    if (isNixExpr(path, st)) {
        state.evalFile(path, v);
        return;
    }

    /* The path is a directory.  Put the Nix expressions in the
       directory in a set, with the file name of each expression as
       the attribute name.  Recurse into subdirectories (but keep the
       set flat, not nested, to make it easier for a user to have a
       ~/.nix-defexpr directory that includes some system-wide
       directory). */
    if (S_ISDIR(st.st_mode)) {
        state.mkAttrs(v, 1024);
        state.mkList(*state.allocAttr(v, state.symbols.create("_combineChannels")), 0);
        StringSet attrs;
        getAllExprs(state, path, attrs, v);
        v.attrs->sort();
    }
}


static void loadDerivations(EvalState & state, Path nixExprPath,
    string systemFilter, Bindings & autoArgs,
    const string & pathPrefix, DrvInfos & elems)
{
    Value vRoot;
    loadSourceExpr(state, nixExprPath, vRoot);

    Value & v(*findAlongAttrPath(state, pathPrefix, autoArgs, vRoot));

    getDerivations(state, v, pathPrefix, autoArgs, elems, true);

    /* Filter out all derivations not applicable to the current
       system. */
    for (DrvInfos::iterator i = elems.begin(), j; i != elems.end(); i = j) {
        j = i; j++;
        if (systemFilter != "*" && i->querySystem() != systemFilter)
            elems.erase(i);
    }
}

int
getPriority (EvalState & state, DrvInfo & drv)
{
  return drv.queryMetaInt ("priority", 0);
}

int
comparePriorities (EvalState & state, DrvInfo & drv1, DrvInfo & drv2)
{
  return getPriority (state, drv2) - getPriority (state, drv1);
}

bool
keep (DrvInfo & drv)
{
  return drv.queryMetaBool ("keep", false);
}
