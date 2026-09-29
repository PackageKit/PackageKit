dnf5_tests_exe = executable(
  'dnf5-tests',
  'dnf5-tests.cpp',
  'definitions.cpp',
  '../dnf5-backend-utils.cpp',
  '../' + dnf5_vendor_source,
  include_directories: [
    packagekit_src_include,
    include_directories('..'),
  ],
  dependencies: [
    dnf5_dep,
    packagekit_glib2_dep,
  ],
  cpp_args: dnf5_cpp_args,
  build_by_default: true,
  install: false,
)

test(
  'dnf5-backend-tests',
  dnf5_tests_exe,
  suite: 'dnf5',
)
