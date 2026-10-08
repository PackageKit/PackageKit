# Meson definition for the packagekit_backend Python library for spawned backends

pkpy_backend_install_dir = join_paths(pk_python_dir, 'packagekit_backend')
pkpy_backend_sources = files(
    '__init__.py',
    'backend.py',
    'protocol.py',
)

pk_backend_py = [
    custom_target(
        'packagekit_backend-enums.py',
        input: join_paths(source_root, 'lib', 'pk-enum.c'),
        output: 'enums.py',
        command: [
            find_program('python3'),
            files('../enum-converter.py'),
            '@INPUT@',
        ],
        capture: true,
        install: true,
        install_dir: pkpy_backend_install_dir,
    ),
]
install_data(
    pkpy_backend_sources,
    install_dir: pkpy_backend_install_dir,
)

# copy the sources next to the generated module, so the package is importable
# from the build tree
foreach src : pkpy_backend_sources
    pk_backend_py += fs.copyfile(src)
endforeach
