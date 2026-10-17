# Dummy backend for testing purposes

pk_backend_modules += shared_module(
    'pk_backend_dummy',
    'pk-backend-dummy.c',
    include_directories: packagekit_src_include,
    dependencies: [
        libpackagekit_dep,
        gmodule_dep,
    ],
    c_args: [
        '-DG_LOG_DOMAIN="PackageKit-Dummy"',
    ],
    install: true,
    install_dir: pk_backends_dir,
)
