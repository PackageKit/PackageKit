# Backend modules that only exist to exercise the daemon in the test suite.
# They are linked into the fake test root.
foreach name : ['fail', 'nop', 'succeed', 'thread']
    pk_backend_modules += shared_module(
        'pk_backend_test_' + name,
        'pk-backend-test-' + name + '.c',
        include_directories: packagekit_src_include,
        dependencies: [
            libpackagekit_dep,
            gmodule_dep,
        ],
        c_args: [
            '-DG_LOG_DOMAIN="PackageKit-Test"',
        ],
        install: false,
    )
endforeach
