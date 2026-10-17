# Backend for poldek, the PLD Linux package manager: a helper program the
# daemon spawns (docs/extra/backend-spawn-protocol.md)

pk_backend_poldek_dir = join_paths(pk_backends_dir, 'poldek')

cc = meson.get_compiler('c')
# poldek ships no pkg-config file, but trurlib, which comes with it, does; the
# poldek headers include each other relative to the poldek include directory
trurlib_dep = dependency('trurlib')
poldek_dep = declare_dependency(
    dependencies: [
        cc.find_library('poldek'),
        cc.find_library('poclidek'),
        cc.find_library('vfile'),
        trurlib_dep,
        dependency('rpm'),
    ],
    compile_args: ['-I' + join_paths(trurlib_dep.get_variable(pkgconfig: 'includedir'), 'poldek')],
)

# everything but main(), so the tests can link it
pk_backend_poldek_core = static_library(
    'pk_backend_poldek_core',
    'ctx.c',
    'errors.c',
    'filters.c',
    'groups.c',
    'job.c',
    'local.c',
    'messages.c',
    'pkgid.c',
    'progress.c',
    'protocol.c',
    'query.c',
    'repos.c',
    'transaction.c',
    'updates.c',
    'verify.c',
    dependencies: [
        libpackagekit_dep,
        jansson_dep,
        poldek_dep,
    ],
    c_args: [
        '-DG_LOG_DOMAIN="PackageKit-Poldek"',
    ],
)

pk_backend_poldek_exe = executable(
    'pk-backend-poldek',
    'main.c',
    link_with: pk_backend_poldek_core,
    dependencies: [
        libpackagekit_dep,
        jansson_dep,
        poldek_dep,
    ],
    c_args: [
        '-DG_LOG_DOMAIN="PackageKit-Poldek"',
    ],
    install: true,
    install_dir: pk_backend_poldek_dir,
)

# copied, so the build directory is a complete backend directory for the test root
configure_file(
    input: 'backend.conf',
    output: 'backend.conf',
    copy: true,
    install: true,
    install_dir: pk_backend_poldek_dir,
)

subdir('tests')
