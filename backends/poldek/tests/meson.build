pk_backend_poldek_test_unit = executable(
    'pk-poldek-test-unit',
    'pk-poldek-test-unit.c',
    link_with: pk_backend_poldek_core,
    include_directories: include_directories('..'),
    dependencies: [
        libpackagekit_dep,
        jansson_dep,
        poldek_dep,
    ],
)

test(
    'poldek-unit',
    pk_backend_poldek_test_unit,
    suite: 'poldek',
)

test(
    'poldek-protocol',
    find_program('pk-poldek-test-protocol.py'),
    args: [pk_backend_poldek_exe],
    suite: 'poldek',
    timeout: 120,
)

# the fixture repositories, built with rpmbuild only when the tests run
pk_poldek_fixtures_dir = join_paths(meson.current_build_dir(), 'fixtures')
pk_poldek_fixtures = custom_target(
    'poldek-fixtures',
    input: 'fixtures.json',
    output: 'fixtures.stamp',
    command: [
        find_program('pk-poldek-fixtures.py'),
        '@INPUT@',
        pk_poldek_fixtures_dir,
        '@OUTPUT@',
    ],
    build_by_default: false,
)

pk_poldek_test_env = [
    'PK_POLDEK_FIXTURES=' + pk_poldek_fixtures_dir,
    'PK_POLDEK_HELPER=' + pk_backend_poldek_exe.full_path(),
]

test(
    'poldek-query',
    find_program('pk-poldek-test-query.py'),
    depends: [pk_backend_poldek_exe, pk_poldek_fixtures],
    env: pk_poldek_test_env,
    suite: 'poldek',
    is_parallel: false,
    timeout: 300,
)

test(
    'poldek-updates',
    find_program('pk-poldek-test-updates.py'),
    depends: [pk_backend_poldek_exe, pk_poldek_fixtures],
    env: pk_poldek_test_env,
    suite: 'poldek',
    is_parallel: false,
    timeout: 300,
)

test(
    'poldek-transactions',
    find_program('pk-poldek-test-transactions.py'),
    depends: [pk_backend_poldek_exe, pk_poldek_fixtures],
    env: pk_poldek_test_env,
    suite: 'poldek',
    is_parallel: false,
    timeout: 600,
)

test(
    'poldek-local',
    find_program('pk-poldek-test-local.py'),
    depends: [pk_backend_poldek_exe, pk_poldek_fixtures],
    env: pk_poldek_test_env,
    suite: 'poldek',
    is_parallel: false,
    timeout: 300,
)

test(
    'poldek-system',
    find_program('pk-poldek-test-system.py'),
    depends: [pk_backend_poldek_exe, pk_poldek_fixtures],
    env: pk_poldek_test_env,
    suite: 'poldek',
    is_parallel: false,
    timeout: 300,
)
