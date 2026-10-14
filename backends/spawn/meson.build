# The generic module for spawned backends

pk_backend_modules += shared_module(
    'pk_backend_spawn',
    'pk-backend-spawn.c',
    include_directories: packagekit_src_include,
    dependencies: [
        libpackagekit_dep,
        gmodule_dep,
        config_dep,
    ],
    install: true,
    install_dir: pk_backends_dir,
)
